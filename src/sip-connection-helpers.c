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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib.h>

#include <sofia-sip/sip.h>
#include <sofia-sip/sip_header.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include "sip-media-channel.h"
#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "sip-connection-enumtypes.h"
#include "sip-connection-private.h"

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"


/* Default keepalive timeout in seconds,
 * a value obtained from Sofia-SIP documentation */
#define SIP_CONNECTION_DEFAULT_KEEPALIVE_INTERVAL 120

/* The value of SIP_NH_EXPIRED. This can be anything that is neither NULL
 * nor a media channel */
void *_sip_nh_expired = (void *)"";

nua_t *sip_conn_sofia_nua(SIPConnection *obj)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (obj);

  return priv->sofia_nua;
}

su_home_t *sip_conn_sofia_home (SIPConnection *conn)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (conn);

  return priv->sofia_home;
}

static sip_to_t *priv_sip_to_url_make (su_home_t *home, const char *address)
{
  sip_to_t *to = sip_to_make (home, address);

  if (to &&
      url_sanitize(to->a_url) == 0) 
    return to;

  return NULL;
}

nua_handle_t *
sip_conn_create_register_handle (nua_t *nua,
                                 su_home_t *home,
                                 const char *address)
{
  sip_to_t *to;
  nua_handle_t *result = NULL;

  to = priv_sip_to_url_make (home, address);

  if (to) 
    result = nua_handle (nua, NULL, SIPTAG_TO(to), TAG_END());
  else
    g_warning ("nua: Unable to create register handle for <%s>", address);

  return result;
}

nua_handle_t *
sip_conn_create_request_handle (nua_t *nua,
                                su_home_t *home,
                                const char *address)
{
  sip_to_t *to;
  nua_handle_t *result = NULL;

  to = priv_sip_to_url_make (home, address);

  if (to) 
    result = nua_handle (nua, NULL, NUTAG_URL(to->a_url), SIPTAG_TO(to),
        TAG_END());
  else
    g_warning ("nua: Unable to create SIP request handle for <%s>", address);

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

  g_debug ("%s: setting keepalive interval to %ld msec", G_STRFUNC, keepalive_interval);

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

  if (!priv->sofia_nua)
    {
      /* nothing to do */
      return;
    }

  if (priv->stun_server != NULL)
    {

      if (priv->stun_port == 0)
        {
          composed = g_strdup (priv->stun_server);
        }
      else
        {
          composed = g_strdup_printf ("%s:%u", priv->stun_server,
              priv->stun_port);
        }
    }

  nua_set_params(priv->sofia_nua,
      STUNTAG_SERVER(composed), TAG_END());

  g_free (composed);
}
