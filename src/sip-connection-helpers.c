/*
 * sip-connection-helpers.c - Helper routines used by SIPConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include "sip-sofia-decls.h"
#include <sofia-sip/sip.h>
#include <sofia-sip/sip_header.h>

#include "sip-connection-helpers.h"
#include "sip-connection-private.h"

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"

/* Default keepalive timeout in seconds,
 * a value obtained from Sofia-SIP documentation */
#define SIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL 120

/* The value of SIP_NH_EXPIRED. This can be anything that is neither NULL
 * nor a media channel */
NUA_HMAGIC_T * const _sip_nh_expired = (NUA_HMAGIC_T *)"";

static sip_to_t *priv_sip_to_url_make (SIPConnection *conn,
                                       su_home_t *home,
                                       TpHandle contact)
{
  TpHandleRepoIface *contact_repo;
  sip_to_t *to;
  const char *address;

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)conn, TP_HANDLE_TYPE_CONTACT);

  address = tp_handle_inspect (contact_repo, contact);
  if (address == NULL)
    return NULL;

  /* TODO: set display name bound to the handle using qdata? */

  to = sip_to_make (home, address);

  if (to &&
      url_sanitize(to->a_url) == 0) 
    return to;

  return NULL;
}

nua_handle_t *
sip_conn_create_register_handle (SIPConnection *conn,
                                 TpHandle contact)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  nua_handle_t *result = NULL;
  su_home_t temphome[1] = { SU_HOME_INIT(temphome) };
  sip_to_t *to;

  g_assert (priv->sofia_home != NULL);
  g_assert (priv->sofia_nua != NULL);

  to = priv_sip_to_url_make (conn, temphome, contact);

  if (to)
    result = nua_handle (priv->sofia_nua, NULL, SIPTAG_TO(to), TAG_END());

  su_home_deinit (temphome);

  return result;
}

nua_handle_t *
sip_conn_create_request_handle (SIPConnection *conn,
                                TpHandle contact)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  nua_handle_t *result = NULL;
  su_home_t temphome[1] = { SU_HOME_INIT(temphome) };
  sip_to_t *to;

  g_assert (priv->sofia_home != NULL);
  g_assert (priv->sofia_nua != NULL);

  to = priv_sip_to_url_make (conn, temphome, contact);

  /* TODO: Pass also SIPTAG_FROM updated from base->self_handle, to update the
   * display name possibly set by the client */

  if (to)
    result = nua_handle (priv->sofia_nua, NULL,
                         NUTAG_URL(to->a_url),
                         SIPTAG_TO(to),
                         TAG_END());

  su_home_deinit (temphome);

  return result;
}

static GHashTable*
priv_nua_get_outbound_options (nua_t* nua)
{
  const char* outbound = NULL;
  GHashTable* option_table;
  gchar** options;
  gchar* token;
  gboolean value;
  int i;

  option_table = g_hash_table_new_full (g_str_hash,
                                        g_str_equal,
                                        g_free,
                                        NULL);

  nua_get_params (nua, NUTAG_OUTBOUND_REF(outbound), TAG_END());
  if (outbound == NULL)
    return option_table;

  g_debug ("%s: got outbound options %s", G_STRFUNC, outbound);

  options = g_strsplit_set (outbound, " ", 0);

  for (i = 0; (token = options[i]) != NULL; i++)
    {
      value = TRUE;
      /* Look for the negation prefixes */
      if (g_ascii_strncasecmp (token, "no", 2) == 0)
        switch (token[2])
          {
	  case '-':
	  case '_':
	    token += 3;
	    value = FALSE;
	    break;
	  case 'n':
	  case 'N':
	    switch (token[3])
	      {
	      case '-':
	      case '_':
		token += 4;
		value = FALSE;
		break;
	      }
	    break;
          }

      g_hash_table_insert (option_table,
                           g_strdup (token),
                           GINT_TO_POINTER(value));
    }

  g_strfreev (options);

  return option_table;
}

static void
priv_nua_outbound_vectorize_walk (gpointer key,
                                  gpointer value,
                                  gpointer user_data)
{
  gchar ***pstrv = (gchar ***)user_data;
  const gchar *option = (const gchar *)key;
  *(*pstrv)++ = (value)? g_strdup (option) : g_strdup_printf ("no-%s", option);
}

static void
priv_nua_set_outbound_options (nua_t* nua, GHashTable* option_table)
{
  gchar* outbound;
  gchar** options;
  gchar** walker;

  /* construct the option string array */
  options = g_new(gchar*, g_hash_table_size (option_table) + 1);

  /* fill the array with option tokens and terminate with NULL */
  walker = options;
  g_hash_table_foreach (option_table, priv_nua_outbound_vectorize_walk, &walker);
  *walker = NULL;

  /* concatenate all tokens into a string */
  outbound = g_strjoinv (" ", options);

  g_strfreev (options);

  g_assert (outbound != NULL);

  /* deliver the option string to the stack */
  g_debug ("%s: setting outbound options %s", G_STRFUNC, outbound);
  nua_set_params (nua, NUTAG_OUTBOUND(outbound), TAG_NULL());

  g_free (outbound);
}

void
sip_conn_update_nua_outbound (SIPConnection *conn)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  GHashTable *option_table;

  g_return_if_fail (priv->sofia_nua != NULL);

  option_table = priv_nua_get_outbound_options (priv->sofia_nua);

  /* Purge existing occurrences of the affected options */
  g_hash_table_remove (option_table, "options-keepalive");

  /* Set options that affect keepalive behavior */
  switch (priv->keepalive_mechanism)
    {
    case SIP_CONNECTION_KEEPALIVE_NONE:
    case SIP_CONNECTION_KEEPALIVE_REGISTER:
      /* For REGISTER keepalives, we use NUTAG_M_FEATURES */
      g_hash_table_insert (option_table,
                           g_strdup ("options-keepalive"),
                           GINT_TO_POINTER(FALSE));
      break;
    case SIP_CONNECTION_KEEPALIVE_OPTIONS:
      g_hash_table_insert (option_table,
                           g_strdup ("options-keepalive"),
                           GINT_TO_POINTER(TRUE));
      break;
    case SIP_CONNECTION_KEEPALIVE_STUN:
      /* Not supported */
      break;
    case SIP_CONNECTION_KEEPALIVE_AUTO:
    default:
      break;
    }

  g_hash_table_insert (option_table,
                       g_strdup ("use-rport"),
                       GINT_TO_POINTER(priv->discover_binding));

  /* Hand options back to the NUA */

  priv_nua_set_outbound_options (priv->sofia_nua, option_table);

  g_hash_table_destroy (option_table);
}

void
sip_conn_update_nua_keepalive_interval (SIPConnection *conn)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  long keepalive_interval;

  if (priv->keepalive_mechanism == SIP_CONNECTION_KEEPALIVE_NONE)
    keepalive_interval = 0;
  else if (priv->keepalive_interval == 0)
    /* XXX: figure out proper default timeouts depending on transport */
    keepalive_interval = SIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL;
  else
    keepalive_interval = (long)priv->keepalive_interval;
  keepalive_interval *= 1000;

  DEBUG("setting keepalive interval to %ld msec", keepalive_interval);

  nua_set_params (priv->sofia_nua,
                  NUTAG_KEEPALIVE(keepalive_interval),
                  TAG_NULL());
}

void
sip_conn_update_nua_contact_features (SIPConnection *conn)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  char *contact_features;
  guint timeout;

  if (priv->keepalive_mechanism != SIP_CONNECTION_KEEPALIVE_REGISTER)
    return;

  timeout = (priv->keepalive_interval > 0)
	? priv->keepalive_interval
	: SIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL;
  contact_features = g_strdup_printf ("expires=%u", timeout);
  nua_set_params(priv->sofia_nua,
		 NUTAG_M_FEATURES(contact_features),
		 TAG_NULL());
  g_free (contact_features);
}

void
sip_conn_update_stun_server (SIPConnection *conn)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  gchar *composed = NULL;

  if (priv->sofia_nua == NULL)
    {
      /* nothing to do */
      return;
    }

  if (priv->stun_server != NULL)
    {
      if (priv->stun_port == 0)
        composed = g_strdup (priv->stun_server);
      else
        composed = g_strdup_printf ("%s:%u",
                                    priv->stun_server, priv->stun_port);
      DEBUG("setting STUN server to %s", composed);
    }
  else
    DEBUG("clearing STUN server address");

  nua_set_params(priv->sofia_nua,
      STUNTAG_SERVER(composed), TAG_END());

  g_free (composed);
}

static void
sip_conn_set_stun_server_address (SIPConnection *conn, const gchar *address)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  g_free (priv->stun_server);
  priv->stun_server = g_strdup (address);
  sip_conn_update_stun_server (conn);
}

void
_stun_resolver_cb (sres_context_t *ctx, sres_query_t *query, sres_record_t **answers)
{
  SIPConnection *conn = SIP_CONNECTION (ctx);
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);

  if ((NULL != answers) && (NULL != answers[0]) && (0 == answers[0]->sr_record->r_status))
    sip_conn_set_stun_server_address (conn,
                                      inet_ntoa (answers[0]->sr_a->a_addr));

  sres_free_answers (priv->sofia_resolver, answers);
}

void
sip_conn_resolv_stun_server (SIPConnection *conn, const gchar *stun_server)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  struct in_addr test_addr;

  if (stun_server == NULL)
    {
      sip_conn_set_stun_server_address (conn, NULL);
      return;
    }

  if (inet_aton (stun_server, &test_addr))
    {
      sip_conn_set_stun_server_address (conn, stun_server);
      return;
    }
  
  if (NULL == priv->sofia_resolver)
    {
      priv->sofia_resolver =
        sres_resolver_create (priv->sofia->sofia_root, NULL, TAG_END());
    }
  g_return_if_fail (priv->sofia_resolver != NULL);

  DEBUG("creating a new resolver query for STUN host name %s", stun_server);

  sres_query (priv->sofia_resolver,
              _stun_resolver_cb,
              (sres_context_t *) conn,
              sres_type_a,
              stun_server);
}

static guchar *
_urlencode (su_home_t *home, const gchar *string)
{
    guchar *a, *b;
    guchar *new = su_zalloc (home, strlen (string) * 3 + 1);

    for (a = (guchar *) string, b = new; *a; a++, b++)
      {
        if (g_ascii_isalnum(*a) || (*a == '.') || (*a == '-') ||
            (*a == '+') || (*a == '_'))
          {
            *b = *a;
          }
        else
          {
            sprintf((gchar *) b, "%%%02x", (int) *a);
            b += 2;
          }
      }
    return new;
}

/* unescape characters that don't need escaping */
static guchar *
_urldecode (su_home_t *home, const gchar *string)
{
    guchar *a, *b;
    guchar *new = su_zalloc (home, strlen (string) + 1);

    for (a = (guchar *) string, b = new; *a; a++, b++)
      {
        if ((*a == '%') && g_ascii_isxdigit(a[1]) && g_ascii_isxdigit(a[2]))
          {
            gchar tmp[3] = { a[1], a[2], 0 };
            guchar x = (guchar) (strtoul(tmp, NULL, 16) % 256);
            if (g_ascii_isalnum(x) || (x == '.') || (x == '-') ||
                (x == '+') || (x == '_'))
              {
                *b = x;
                a += 2;
                continue;
              }
          }
        *b = *a;
      }
    return new;
}

static gboolean
_is_tel_num (const gchar *string)
{
    while (*string)
      {
        if (!g_ascii_isdigit (*string) &&
            !g_ascii_ispunct (*string) &&
            !g_ascii_isspace (*string))
                return FALSE;
        string++;
      }
    return TRUE;
}

static gchar *
_strip_tel_num (su_home_t *home, const gchar *string)
{
    gchar *a, *b;
    gchar *new = su_zalloc (home, strlen (string) + 1);

    for (a = (gchar *) string, b = new; *a; a++)
      {
        if (!g_ascii_isdigit(*a) && (*a != '-') && (*a != '+')) continue;
        *(b++) = *a;
      }
    *b = 0;
    return new;
}

gchar *
sip_conn_normalize_uri (SIPConnection *conn,
                        const gchar *sipuri,
                        GError **error)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);
  su_home_t home[1] = { SU_HOME_INIT (home) };
  url_t *url = NULL;;
  gchar *retval = NULL;
  char *c, *str;

  g_assert (home);

  url = url_make (home, sipuri);

  /* we got username or phone number, local to our domain */
  if ((url == NULL) ||
      ((url->url_scheme == NULL) && (url->url_user == NULL)))
    {
      if (priv->domain == NULL)
        {
          g_debug ("local uri specified and we don't know local domain yet");
          goto error;
        }

      if (_is_tel_num (sipuri))
        {
          url = url_format (home, "sip:%s@%s", _strip_tel_num (home, sipuri),
              priv->domain);
        }
      else
        {
          url = url_format (home, "sip:%s@%s", _urlencode (home, sipuri),
              priv->domain);
        }
      if (!url) goto error;
    }
  else
    {
      if ((url != NULL) && (url->url_user != NULL))
        {
          url->url_user = (char *) _urldecode (home, url->url_user);
        }
    }

  if (url_sanitize (url)) goto error;

  /* scheme should've been set by now */
  if (!url->url_scheme || (url->url_scheme[0] == 0))
      goto error;

  for (c = (char *) url->url_host; *c; c++)
    {
      /* check for illegal characters */
      if (!g_ascii_isalnum(*c) && (*c != '_') && (*c != '.') && (*c != '_'))
          goto error;

      /* convert host to lowercase */
      *c = g_ascii_tolower (*c);
    }
  /* check that the hostname isn't empty */
  if (c == url->url_host) goto error;

  /* check that if we have '@', the username isn't empty */
  if (url->url_user)
    {
      if (url->url_user[0] == 0) goto error;
    }

  str = url_as_string (home, url);
  if (NULL == str) goto error;

  retval = g_strdup (str);

error:
  if (NULL == retval)
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "invalid SIP URI");

  /* success */
  su_home_deinit (home);
  return retval;
}

gchar *
sip_conn_domain_from_uri (const gchar *str)
{
  su_home_t home[1] = { SU_HOME_INIT (home) };
  url_t *url;
  gchar *domain;

  g_assert (str != NULL);

  url = url_make (home, str);
  g_assert (url != NULL);

  domain = g_strdup (url->url_host);
  su_home_deinit (home);
  return domain;
}

