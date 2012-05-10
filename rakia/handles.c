/*
 * rakia/handles.c - Handler helpers
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2006-2011 Nokia Corporation
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include <stdlib.h>

#include <rakia/handles.h>
#include <sofia-sip/sip_header.h>

#define DEBUG_FLAG RAKIA_DEBUG_CONNECTION
#include "rakia/debug.h"

static GQuark
rakia_handle_url_quark (void)
{
  static GQuark quark = 0;

  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("rakia-handle-url");

  return quark;
}

const url_t*
rakia_handle_inspect_uri (TpBaseConnection *base,
                          TpHandle handle)
{
  TpHandleRepoIface *repo;
  GQuark url_quark;
  url_t *url;
  GError *error;

  repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (repo, handle, &error))
    {
      DEBUG("invalid handle %u: %s", handle, error->message);
      g_error_free (error);
      return NULL;
    }

  url_quark = rakia_handle_url_quark ();

  url = tp_handle_get_qdata (repo, handle, url_quark);

  if (url == NULL)
    {
      url = url_make (NULL, tp_handle_inspect (repo, handle));

      tp_handle_set_qdata (repo, handle, url_quark, url, free);
    }

  return url;
}

TpHandle
rakia_handle_ensure (TpBaseConnection *conn,
                     url_t const *uri,
                     char const *alias)
{
  TpHandleRepoIface *repo;
  gchar *str;
  TpHandle handle;

  g_return_val_if_fail (TP_IS_BASE_CONNECTION (conn), 0);
  g_return_val_if_fail (uri != NULL, 0);

  repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);

  str = url_as_string (NULL, uri);

  handle = tp_handle_ensure (repo, str, NULL, NULL);

  su_free (NULL, str);

  /* TODO: set qdata for the alias */

  return handle;
}

TpHandle
rakia_handle_by_requestor (TpBaseConnection *conn,
                           sip_t const *sip)
{
  url_t const *uri;
  char const *display;

  g_return_val_if_fail (sip != NULL, 0);
  g_return_val_if_fail (sip->sip_from != NULL, 0);

  uri = sip->sip_from->a_url;
  display = sip->sip_from->a_display;

  return rakia_handle_ensure (conn, uri, display);
}

void
rakia_handle_unref (TpBaseConnection *conn,
                    TpHandle handle)
{
  TpHandleRepoIface *repo;

  g_return_if_fail (TP_IS_BASE_CONNECTION (conn));
  g_return_if_fail (handle != 0);

  repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (repo, handle);
}

char const *
rakia_handle_inspect (TpBaseConnection *conn,
                      TpHandle handle)
{
  TpHandleRepoIface *repo;

  g_return_val_if_fail (TP_IS_BASE_CONNECTION (conn), NULL);
  g_return_val_if_fail (handle != 0, NULL);

  repo = tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);

  return tp_handle_inspect (repo, handle);
}

static gboolean
priv_is_host (const gchar* str)
{
  static GRegex *host_regex = NULL;

#define DOMAIN "[a-z0-9]([-a-z0-9]*[a-z0-9])?"
#define TLD "[a-z]([-a-z0-9]*[a-z0-9])?"

  if (host_regex == NULL)
    {
      GError *error = NULL;

      host_regex = g_regex_new ("^("
            "("DOMAIN"\\.)*"TLD"\\.?|"      /* host name */
            "[0-9]{1,3}(\\.[0-9]{1,3}){3}|" /* IPv4 address */
            "\\[[0-9a-f:.]\\]"              /* IPv6 address, sloppily */
          ")$",
          G_REGEX_CASELESS | G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, &error);

      if (error != NULL)
        g_error ("failed to compile the host regex: %s", error->message);
    }

#undef DOMAIN
#undef TLD

  return g_regex_match (host_regex, str, 0, NULL);
}

static gboolean
priv_is_tel_num (const gchar *str)
{
  static GRegex *tel_num_regex = NULL;

  if (tel_num_regex == NULL)
    {
      GError *error = NULL;

      tel_num_regex = g_regex_new (
          "^\\s*[\\+(]?\\s*[0-9][-.0-9()\\s]*$",
          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, &error);

      if (error != NULL)
        g_error ("failed to compile the telephone number regex: %s",
            error->message);
    }

  return g_regex_match (tel_num_regex, str, 0, NULL);
}

/* Strip the non-essential characters from a string regarded as
 * a telephone number */
static gchar *
priv_strip_tel_num (const gchar *fuzzy)
{
  static GRegex *cruft_regex = NULL;

  if (cruft_regex == NULL)
    {
      GError *error = NULL;

      cruft_regex = g_regex_new ("[^+0-9]+",
          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, &error);

      if (error != NULL)
        g_error ("failed to compile the non-essential "
            "telephone number cruft regex: %s", error->message);
    }

  return g_regex_replace_literal (cruft_regex, fuzzy, -1, 0, "", 0, NULL);
}

static const char *
priv_lowercase_url_part (su_home_t *home, const char *src)
{
  size_t n = 0;
  size_t i;
  char *res;

  for (i = 0; src[i]; i++)
    {
      if (g_ascii_isupper (src[i]))
        {
          n = i + strlen (src + i);
          break;
        }
    }

  if (!src[i])
    return src;

  res = su_alloc (home, n + 1);
  memcpy (res, src, i);
  for (; i < n; i++)
    res[i] = g_ascii_tolower (src[i]);
  res[i] = '\0';

  return (const char *) res;
}

#define RAKIA_RESERVED_CHARS_ALLOWED_IN_USERNAME "!*'()&=+$,;?/"

gchar *
rakia_normalize_contact (const gchar *sipuri,
    const url_t *base_url,
    const gchar *transport,
    GError **error)
{
  su_home_t home[1] = { SU_HOME_INIT(home) };
  url_t *url;
  gchar *retval = NULL;
  char *c;

  url = url_make (home, sipuri);

  if (url == NULL ||
      (url->url_scheme == NULL && url->url_user == NULL))
    {
      /* we got username or phone number, local to our domain */
      gchar *user;

      if (base_url == NULL || base_url->url_host == NULL)
        {
          WARNING ("bare name given, but no account URL is set");
          goto error;
        }

      if (priv_is_tel_num (sipuri))
        {
          user = priv_strip_tel_num (sipuri);
        }
      else
        {
          user = g_uri_escape_string (sipuri,
              RAKIA_RESERVED_CHARS_ALLOWED_IN_USERNAME, FALSE);
        }

      if (base_url->url_type == url_sips)
        url = url_format (home, "sips:%s@%s",
            user, base_url->url_host);
      else
        url = url_format (home, "sip:%s@%s",
            user, base_url->url_host);

      g_free (user);

      if (!url) goto error;
    }
  else if (url->url_scheme == NULL)
    {
      /* Set the scheme to SIP or SIPS accordingly to the connection's
       * transport preference */
      if (transport != NULL
          && g_ascii_strcasecmp (transport, "tls") == 0)
        {
          url->url_type = url_sips;
          url->url_scheme = "sips";
        }
      else
        {
          url->url_type = url_sip;
          url->url_scheme = "sip";
        }
    }

  if (url_sanitize (url) != 0) goto error;

  /* scheme should've been set by now */
  if (url->url_scheme == NULL || (url->url_scheme[0] == 0))
    goto error;

  /* convert the scheme to lowercase */
  /* Note: we can't do it in place because url->url_scheme may point to
   * a static string */
  url->url_scheme = priv_lowercase_url_part (home, url->url_scheme);

  /* Check that if we have '@', the username isn't empty.
   * Note that we rely on Sofia-SIP to canonize the user name */
  if (url->url_user)
    {
      if (url->url_user[0] == 0) goto error;
    }

  /* host should be set and valid */
  if (url->url_host == NULL || !priv_is_host (url->url_host))
      goto error;

  /* convert host to lowercase */
  for (c = (char *) url->url_host; *c; c++)
    {
      *c = g_ascii_tolower (*c);
    }

  retval = g_strdup (url_as_string (home, url));

error:
  if (retval == NULL)
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_HANDLE,
          "invalid SIP URI");

  su_home_deinit (home);
  return retval;
}
