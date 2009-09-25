/*
 * sip-connection-helpers.c - Helper routines used by TpsipConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2006-2008 Nokia Corporation
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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include "sip-connection-helpers.h"

#include <sofia-sip/sip.h>
#include <sofia-sip/sip_header.h>
#include <sofia-sip/tport_tag.h>

#include "sip-connection-private.h"

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "debug.h"

/* Default keepalive timeout in seconds,
 * a value obtained from Sofia-SIP documentation */
#define TPSIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL 120

/* The user is not allowed to set keepalive timeout to lower than that,
 * to avoid wasting traffic and device power */
#define TPSIP_CONNECTION_MINIMUM_KEEPALIVE_INTERVAL 30

/* The user is not allowed to set keepalive timeout to lower than that
 * for REGISTER keepalives, to avoid wasting traffic and device power.
 * REGISTER is special because it may tie resources on the server side */
#define TPSIP_CONNECTION_MINIMUM_KEEPALIVE_INTERVAL_REGISTER 50

static sip_to_t *
priv_sip_to_url_make (TpsipConnection *conn,
                      su_home_t *home,
                      TpHandle contact)
{
  const url_t *url;

  url = tpsip_conn_get_contact_url (conn, contact);
  return sip_to_create (home, (const url_string_t *) url);
}

static sip_from_t *
priv_sip_from_url_make (TpsipConnection *conn,
                        su_home_t *home)
{
  static GRegex *escape_regex = NULL;

  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  sip_from_t *from;
  gchar *alias = NULL;

  from = sip_from_create (home, (const url_string_t *) priv->account_url);

  if (from == NULL)
    return NULL;

  g_object_get (conn, "alias", &alias, NULL);
  if (alias != NULL)
    {
      gchar *escaped_alias;

      /* Make the alias into a quoted string, escaping all characters
       * that cannot go verbatim into a quoted string */

      if (escape_regex == NULL)
        {
          escape_regex = g_regex_new (
              "[\"\\\\[:cntrl:]]", G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
        }

      escaped_alias = g_regex_replace (escape_regex, alias, -1, 0,
          "\\\\\\0", 0, NULL);

      g_free (alias);

      from->a_display = su_strcat_all (home, "\"", escaped_alias, "\"", NULL);

      g_free (escaped_alias);
    }

  return from;
}


nua_handle_t *
tpsip_conn_create_register_handle (TpsipConnection *conn,
                                 TpHandle contact)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
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
tpsip_conn_create_request_handle (TpsipConnection *conn,
                                  TpHandle contact)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  nua_handle_t *result = NULL;
  su_home_t temphome[1] = { SU_HOME_INIT(temphome) };
  sip_from_t *from;
  sip_to_t *to;

  g_assert (priv->sofia_home != NULL);
  g_assert (priv->sofia_nua != NULL);

  to = priv_sip_to_url_make (conn, temphome, contact);
  from = priv_sip_from_url_make (conn, temphome);

  if (to != NULL && from != NULL)
    result = nua_handle (priv->sofia_nua, NULL,
                         NUTAG_URL(to->a_url),
                         SIPTAG_TO(to),
                         SIPTAG_FROM(from),
                         TAG_END());

  su_home_deinit (temphome);

  return result;
}

void
tpsip_conn_save_event (TpsipConnection *conn,
                     nua_saved_event_t ret_saved [1])
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  nua_save_event (priv->sofia_nua, ret_saved);
}

void
tpsip_conn_update_proxy_and_transport (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);

  if (priv->proxy_url != NULL)
    {
      su_home_t temphome[1] = { SU_HOME_INIT(temphome) };
      sip_route_t *route = NULL;
      const char *params = NULL;

      if (priv->loose_routing)
        {
          url_t *route_url;
          route_url = url_hdup (temphome, priv->proxy_url);
          g_return_if_fail (route_url != NULL);
          if (!url_has_param (route_url, "lr"))
            url_param_add (temphome, route_url, "lr");
          route = sip_route_create (temphome, route_url, NULL);
        }

      if (priv->transport != NULL && priv->proxy_url->url_type == url_sip)
        {
          if (g_ascii_strcasecmp (priv->transport, "tcp") == 0)
            params = "transport=tcp";
          else if (g_ascii_strcasecmp (priv->transport, "udp") == 0)
            params = "transport=udp";
          else
            g_warning ("unrecognized transport parameter value: %s", priv->transport);
        }

      nua_set_params (priv->sofia_nua,
                      TAG_IF(route, NUTAG_INITIAL_ROUTE(route)),
                      TAG_IF(!priv->loose_routing,
                             NUTAG_PROXY(priv->proxy_url)),
                      TAG_IF(params, NUTAG_M_PARAMS(params)),
                      TAG_NULL());

      su_home_deinit (temphome);
    }
}

const url_t *
tpsip_conn_get_local_url (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  url_t *url;

  url = url_make (priv->sofia_home, "sip:*:*");

  if (url == NULL)
    return NULL;

  if (priv->proxy_url != NULL)
    {
      url->url_type = priv->proxy_url->url_type;
    }
  else
    {
      g_assert (priv->account_url != NULL);
      url->url_type = priv->account_url->url_type;
    }

  if (priv->local_ip_address == NULL)
    url->url_host = "*";
  else
    url->url_host = priv->local_ip_address;

  if (priv->local_port == 0)
    url->url_port = "*";
  else
    url->url_port = su_sprintf (priv->sofia_home, "%u", priv->local_port);

  if (url->url_type == url_sip && priv->transport != NULL)
    {
      if (!g_ascii_strcasecmp(priv->transport, "udp"))
        url->url_params = "transport=udp";
      else if (!g_ascii_strcasecmp(priv->transport, "tcp"))
        url->url_params = "transport=tcp";
    }

  /* url_sanitize (url); -- we're always sane B-] */

  DEBUG("local binding expressed as <" URL_PRINT_FORMAT ">", URL_PRINT_ARGS(url));

  return url;
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
tpsip_conn_update_nua_outbound (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  GHashTable *option_table;

  g_return_if_fail (priv->sofia_nua != NULL);

  option_table = priv_nua_get_outbound_options (priv->sofia_nua);

  /* Purge existing occurrences of the affected options */
  g_hash_table_remove (option_table, "options-keepalive");

  /* Set options that affect keepalive behavior */
  switch (priv->keepalive_mechanism)
    {
    case TPSIP_CONNECTION_KEEPALIVE_NONE:
    case TPSIP_CONNECTION_KEEPALIVE_REGISTER:
      /* For REGISTER keepalives, we use NUTAG_M_FEATURES */
      g_hash_table_insert (option_table,
                           g_strdup ("options-keepalive"),
                           GINT_TO_POINTER(FALSE));
      break;
    case TPSIP_CONNECTION_KEEPALIVE_OPTIONS:
      g_hash_table_insert (option_table,
                           g_strdup ("options-keepalive"),
                           GINT_TO_POINTER(TRUE));
      break;
    case TPSIP_CONNECTION_KEEPALIVE_STUN:
      /* Not supported */
      break;
    case TPSIP_CONNECTION_KEEPALIVE_AUTO:
    default:
      break;
    }

  g_hash_table_insert (option_table,
                       g_strdup ("natify"),
                       GINT_TO_POINTER(priv->discover_binding));
  g_hash_table_insert (option_table,
                       g_strdup ("use-rport"),
                       GINT_TO_POINTER(priv->discover_binding));

  /* Hand options back to the NUA */

  priv_nua_set_outbound_options (priv->sofia_nua, option_table);

  g_hash_table_destroy (option_table);
}

static void
priv_sanitize_keepalive_interval (TpsipConnectionPrivate *priv)
{
  guint minimum_interval;
  if (priv->keepalive_interval != 0)
    {
      minimum_interval =
              (priv->keepalive_mechanism == TPSIP_CONNECTION_KEEPALIVE_REGISTER)
              ? TPSIP_CONNECTION_MINIMUM_KEEPALIVE_INTERVAL_REGISTER
              : TPSIP_CONNECTION_MINIMUM_KEEPALIVE_INTERVAL;
      if (priv->keepalive_interval < minimum_interval)
        {
          g_warning ("keepalive interval is too low, pushing to %u", minimum_interval);
          priv->keepalive_interval = minimum_interval;
        }
    }
}

void
tpsip_conn_update_nua_keepalive_interval (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  long keepalive_interval;

  if (!priv->keepalive_interval_specified)
    return;

  if (priv->keepalive_mechanism == TPSIP_CONNECTION_KEEPALIVE_NONE)
    keepalive_interval = 0;
  else
    {
      priv_sanitize_keepalive_interval (priv);
      keepalive_interval = (long) priv->keepalive_interval;
    }
  keepalive_interval *= 1000;

  DEBUG("setting keepalive interval to %ld msec", keepalive_interval);

  nua_set_params (priv->sofia_nua,
                  NUTAG_KEEPALIVE(keepalive_interval),
                  TPTAG_KEEPALIVE(keepalive_interval),
                  TAG_NULL());
}

void
tpsip_conn_update_nua_contact_features (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  char *contact_features;
  guint timeout;

  if (priv->keepalive_mechanism != TPSIP_CONNECTION_KEEPALIVE_REGISTER)
    return;

  if (priv->keepalive_interval == 0)
    return;

  priv_sanitize_keepalive_interval (priv);
  timeout = priv->keepalive_interval_specified
      ? priv->keepalive_interval
      : TPSIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL;
  contact_features = g_strdup_printf ("expires=%u", timeout);
  nua_set_params(priv->sofia_nua,
		 NUTAG_M_FEATURES(contact_features),
		 TAG_NULL());
  g_free (contact_features);
}

static void
tpsip_conn_set_stun_server_address (TpsipConnection *conn, const gchar *address)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  g_return_if_fail (priv->media_factory != NULL);
  g_object_set (priv->media_factory,
                "stun-server", address,
                "stun-port", priv->stun_port,
                NULL);
}

static void
priv_stun_resolver_cb (sres_context_t *ctx, sres_query_t *query, sres_record_t **answers)
{
  TpsipConnection *conn = TPSIP_CONNECTION (ctx);
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  sres_a_record_t *ans = NULL;

  if (NULL != answers)
    {
      int i;
      GPtrArray *items = g_ptr_array_sized_new (1);

      for (i = 0; NULL != answers[i]; i++)
        {
          if ((0 == answers[i]->sr_record->r_status)
              && (sres_type_a == answers[i]->sr_record->r_type))
            {
              g_ptr_array_add (items, answers[i]->sr_a);
            }
        }

      if (items->len > 0)
        {
          ans = g_ptr_array_index (items, g_random_int_range (0, items->len));
        }

      g_ptr_array_free (items, TRUE);
    }

  if (NULL != ans)
    tpsip_conn_set_stun_server_address (conn,
                                        inet_ntoa (ans->a_addr));
  else
    g_debug ("Couldn't resolv STUN server address, ignoring.");

  sres_free_answers (priv->sofia_resolver, answers);
}

void
tpsip_conn_resolv_stun_server (TpsipConnection *conn, const gchar *stun_host)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  struct in_addr test_addr;

  if (stun_host == NULL)
    {
      tpsip_conn_set_stun_server_address (conn, NULL);
      return;
    }

  if (inet_aton (stun_host, &test_addr))
    {
      tpsip_conn_set_stun_server_address (conn, stun_host);
      return;
    }
  
  if (NULL == priv->sofia_resolver)
    {
      priv->sofia_resolver =
        sres_resolver_create (priv->sofia_root, NULL, TAG_END());
    }
  g_return_if_fail (priv->sofia_resolver != NULL);

  DEBUG("creating a new resolver query for STUN host name %s", stun_host);

  sres_query (priv->sofia_resolver,
              priv_stun_resolver_cb,
              (sres_context_t *) conn,
              sres_type_a,
              stun_host);
}

static void
priv_stun_discover_cb (sres_context_t *ctx,
                       sres_query_t *query,
                       sres_record_t **answers)
{
  TpsipConnection *conn = TPSIP_CONNECTION (ctx);
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  sres_srv_record_t *sel = NULL;
  int n_sel_items = 0;
  int i;

  if (answers == NULL)
    return;

  for (i = 0; NULL != answers[i]; i++)
    {
      if (answers[i]->sr_record->r_status != 0)
        continue;
      if (G_UNLIKELY (answers[i]->sr_record->r_type != sres_type_srv))
        continue;

      if (sel == NULL
          || (answers[i]->sr_srv->srv_priority < sel->srv_priority))
        {
          sel = answers[i]->sr_srv;
          n_sel_items = 1;
        }
      else if (answers[i]->sr_srv->srv_priority == sel->srv_priority)
        {
          n_sel_items++;
        }
    }

  if (n_sel_items > 1)
    {
      /* Random selection procedure as recommended in RFC 2782 */
      GArray *items = g_array_sized_new (FALSE,
                                         TRUE,
                                         sizeof (sres_srv_record_t *),
                                         n_sel_items);
      int sum = 0;
      int dice;
      sres_srv_record_t *rec;

      g_assert (sel != NULL);

      for (i = 0; NULL != answers[i]; i++)
        {
          if (answers[i]->sr_record->r_status != 0)
            continue;
          if (G_UNLIKELY (answers[i]->sr_record->r_type != sres_type_srv))
            continue;

          rec = answers[i]->sr_srv; 
          if (rec->srv_priority != sel->srv_priority)
            continue;

          if (rec->srv_weight == 0)
            g_array_prepend_val (items, rec);
          else
            g_array_append_val (items, rec);
        }

      g_assert (n_sel_items == items->len);

      for (i = 0; i < n_sel_items; i++)
        {
          rec = g_array_index (items, sres_srv_record_t *, i);
          sum = (rec->srv_weight += sum);
        }

      dice = g_random_int_range (0, sum + 1);

      for (i = 0; i < n_sel_items; i++)
        {
          rec = g_array_index (items, sres_srv_record_t *, i);
          if (rec->srv_weight >= dice)
            {
              sel = rec;
              break;
            }
        }

      g_array_free (items, TRUE);
    }

  if (sel != NULL)
    {
      DEBUG ("discovery got STUN server %s:%u",
             sel->srv_target, sel->srv_port);
      priv->stun_port = sel->srv_port;
      tpsip_conn_resolv_stun_server (conn, sel->srv_target);
    }

  sres_free_answers (priv->sofia_resolver, answers);
}

void
tpsip_conn_discover_stun_server (TpsipConnection *conn)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  char *srv_domain;

  if ((NULL == priv->account_url) || (NULL == priv->account_url->url_host))
    {
      DEBUG("unknown domain, not making STUN SRV lookup");
      return;
    }

  if (NULL == priv->sofia_resolver)
    {
      priv->sofia_resolver =
        sres_resolver_create (priv->sofia_root, NULL, TAG_END());
    }
  g_return_if_fail (priv->sofia_resolver != NULL);

  DEBUG("creating a new STUN SRV query for domain %s", priv->account_url->url_host);

  srv_domain = g_strdup_printf ("_stun._udp.%s", priv->account_url->url_host);

  sres_query (priv->sofia_resolver,
              priv_stun_discover_cb,
              (sres_context_t *) conn,
              sres_type_srv,
              srv_domain);

  g_free (srv_domain);
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
        g_error ("failed to compile the telephone number regex: %s", error->message);
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
        g_error ("failed to compile the non-essential telephone number cruft regex: %s", error->message);
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

#define TPSIP_RESERVED_CHARS_ALLOWED_IN_USERNAME "!*'()&=+$,;?/"

gchar *
tpsip_handle_normalize (TpHandleRepoIface *repo,
                        const gchar *sipuri,
                        gpointer context,
                        GError **error)
{
  TpsipConnection *conn = TPSIP_CONNECTION (context);
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  const url_t *base_url = priv->account_url;
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
          g_warning ("bare name given, but no account URL is set");
          goto error;
        }

      if (priv_is_tel_num (sipuri))
        {
          user = priv_strip_tel_num (sipuri);
        }
      else
        {
          user = g_uri_escape_string (sipuri,
              TPSIP_RESERVED_CHARS_ALLOWED_IN_USERNAME, FALSE);
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
      if (priv->transport != NULL
          && g_ascii_strcasecmp (priv->transport, "tls") == 0)
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
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
          "invalid SIP URI");

  su_home_deinit (home);
  return retval;
}

static GQuark
tpsip_handle_url_quark ()
{
  static GQuark quark = 0;

  if (G_UNLIKELY (quark == 0))
    quark = g_quark_from_static_string ("tpsip-handle-url");

  return quark;
}

const url_t*
tpsip_conn_get_contact_url (TpsipConnection *self,
                            TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;
  GQuark url_quark;
  url_t *url;
  GError *error;

  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_handles, handle, &error))
    {
      DEBUG("invalid handle %u: %s", handle, error->message);
      g_error_free (error);
      return NULL;
    }

  url_quark = tpsip_handle_url_quark ();

  url = tp_handle_get_qdata (contact_handles, handle, url_quark);

  if (url == NULL)
    {
      url = url_make (priv->sofia_home,
          tp_handle_inspect (contact_handles, handle));

      tp_handle_set_qdata (contact_handles, handle, url_quark, url, NULL);
    }

  return url;
}

TpHandle
tpsip_handle_parse_from (TpHandleRepoIface *contact_repo,
                         const sip_t *sip)
{
  TpHandle handle = 0;
  gchar *url_str;

  g_return_val_if_fail (sip != NULL, 0);

  if (sip->sip_from)
    {
      su_home_t tmphome[1] = { SU_HOME_INIT(tmphome) };

      url_str = url_as_string (tmphome, sip->sip_from->a_url);

      handle = tp_handle_ensure (contact_repo, url_str, NULL, NULL);

      /* TODO: set qdata for the display name */

      su_home_deinit (tmphome);
    }

  return handle;
}

#ifdef HAVE_LIBIPHB

static int
heartbeat_wakeup (su_root_magic_t *foo,
                  su_wait_t *wait,
                  void *user_data)
{
  TpsipConnection *self = (TpsipConnection *) user_data;
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  gint keepalive_earliest; 

  DEBUG("tick");

  g_assert (priv->heartbeat != NULL);

  if ((wait->revents & (SU_WAIT_IN | SU_WAIT_HUP | SU_WAIT_ERR)) != SU_WAIT_IN)
    {
      g_warning ("heartbeat descriptor invalidated prematurely with event mask %hd", wait->revents);
      tpsip_conn_heartbeat_shutdown (self);
      return 0;
    }

  keepalive_earliest = (int) priv->keepalive_interval - TPSIP_DEFER_TIMEOUT;
  if (keepalive_earliest < 0)
    keepalive_earliest = 0;

  if (iphb_wait (priv->heartbeat,
        (gushort) keepalive_earliest,
        (gushort) MIN(priv->keepalive_interval, G_MAXUSHORT),
        0) < 0)
    {
      g_warning ("iphb_wait failed");
      tpsip_conn_heartbeat_shutdown (self);
      return 0;
    }

  return 0;
}

#endif /* HAVE_LIBIPHB */

void
tpsip_conn_heartbeat_init (TpsipConnection *self)
{
#ifdef HAVE_LIBIPHB
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  int wait_id;
  int reference_interval = 0;

  g_assert (priv->heartbeat == NULL);

  priv->heartbeat = iphb_open (&reference_interval);

  if (priv->heartbeat == NULL)
    {
      g_warning ("opening IP heartbeat failed: %s", strerror (errno));
      return;
    }

  DEBUG("heartbeat opened with reference interval %d", reference_interval);

  su_wait_init (priv->heartbeat_wait);
  if (su_wait_create (priv->heartbeat_wait,
                      iphb_get_fd (priv->heartbeat),
                      SU_WAIT_IN) != 0)
    g_critical ("could not create a wait object");

  wait_id = su_root_register (priv->sofia_root,
      priv->heartbeat_wait, heartbeat_wakeup, self, 0);

  g_return_if_fail (wait_id > 0);
  priv->heartbeat_wait_id = wait_id;

  /* Prime the heartbeat for the first time.
   * The minimum wakeup timeout is 0 to fall in step with other
   * clients using the same interval */
  if (iphb_wait (priv->heartbeat,
        0, (gushort) MIN(priv->keepalive_interval, G_MAXUSHORT), 0) < 0)
    {
      g_warning ("iphb_wait failed");
      tpsip_conn_heartbeat_shutdown (self);
    }

#endif /* HAVE_LIBIPHB */
}

void
tpsip_conn_heartbeat_shutdown (TpsipConnection *self)
{
#ifdef HAVE_LIBIPHB
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

  if (priv->heartbeat_wait_id == 0)
    return;

  su_root_deregister (priv->sofia_root, priv->heartbeat_wait_id);
  priv->heartbeat_wait_id = 0;

  su_wait_destroy (priv->heartbeat_wait);

  iphb_close (priv->heartbeat);
  priv->heartbeat = NULL;
#endif /* HAVE_LIBIPHB */
}
