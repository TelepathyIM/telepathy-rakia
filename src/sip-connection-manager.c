/*
 * sip-connection-manager.c - Source for TpsipConnectionManager
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection-manager).
 *   @author See gabble-connection-manager.c
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
#include <string.h>

#include <dbus/dbus-protocol.h>

#include <telepathy-glib/debug-sender.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-connection-manager.h>

#include <tpsip/sofia-decls.h>
#include <sofia-sip/su_glib.h>

#include "sip-connection-manager.h"
#include "sip-connection.h"

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "debug.h"


G_DEFINE_TYPE(TpsipConnectionManager, tpsip_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* private structure *//* typedef struct _TpsipConnectionManagerPrivate TpsipConnectionManagerPrivate; */

typedef struct {
    gchar *account;
    gchar *auth_user;
    gchar *password;
    gchar *alias;
    gchar *registrar;
    gchar *proxy_host;
    guint port;
    gchar *transport;
    gboolean loose_routing;
    gboolean discover_binding;
    gchar *keepalive_mechanism;
    guint keepalive_interval;
    gboolean discover_stun;
    gchar *stun_server;
    guint stun_port;
    gboolean immutable_streams;
    gchar *local_ip_address;
    guint local_port;
    gchar *extra_auth_user;
    gchar *extra_auth_password;
} TpsipConnParams;

static void *
alloc_params (void)
{
  return g_slice_new0 (TpsipConnParams);
}

static void
free_params (void *p)
{
  TpsipConnParams *params = (TpsipConnParams *)p;

  g_free (params->account);
  g_free (params->auth_user);
  g_free (params->password);
  g_free (params->alias);
  g_free (params->registrar);
  g_free (params->proxy_host);
  g_free (params->transport);
  g_free (params->keepalive_mechanism);
  g_free (params->stun_server);
  g_free (params->local_ip_address);
  g_free (params->extra_auth_user);
  g_free (params->extra_auth_password);

  g_slice_free (TpsipConnParams, params);
}

enum {
    TPSIP_CONN_PARAM_ACCOUNT = 0,
    TPSIP_CONN_PARAM_AUTH_USER,
    TPSIP_CONN_PARAM_PASSWORD,
    TPSIP_CONN_PARAM_ALIAS,
    TPSIP_CONN_PARAM_REGISTRAR,
    TPSIP_CONN_PARAM_PROXY_HOST,
    TPSIP_CONN_PARAM_PORT,
    TPSIP_CONN_PARAM_TRANSPORT,
    TPSIP_CONN_PARAM_LOOSE_ROUTING,
    TPSIP_CONN_PARAM_DISCOVER_BINDING,
    TPSIP_CONN_PARAM_KEEPALIVE_MECHANISM,
    TPSIP_CONN_PARAM_KEEPALIVE_INTERVAL,
    TPSIP_CONN_PARAM_DISCOVER_STUN,
    TPSIP_CONN_PARAM_STUN_SERVER,
    TPSIP_CONN_PARAM_STUN_PORT,
    TPSIP_CONN_PARAM_IMMUTABLE_STREAMS,
    TPSIP_CONN_PARAM_LOCAL_IP_ADDRESS,
    TPSIP_CONN_PARAM_LOCAL_PORT,
    TPSIP_CONN_PARAM_EXTRA_AUTH_USER,
    TPSIP_CONN_PARAM_EXTRA_AUTH_PASSWORD,
    N_TPSIP_CONN_PARAMS
};

static const TpCMParamSpec tpsip_params[] = {
    /* Account (a sip: URI) */
    { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
      NULL, G_STRUCT_OFFSET (TpsipConnParams, account) },
    /* Username to register with, if different than in the account URI */
    { "auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, auth_user) },
    /* Password */
    { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_SECRET,
      NULL, G_STRUCT_OFFSET (TpsipConnParams, password) },
    /* Display name for self */
    { "alias", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
      G_STRUCT_OFFSET(TpsipConnParams, alias),
      /* setting a 0-length alias makes no sense */
      tp_cm_param_filter_string_nonempty, NULL },
    /* Registrar */
    { "registrar", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, registrar) },
    /* Used to compose proxy URI */
    { "proxy-host", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, proxy_host) },
    /* Used to compose proxy URI */
    { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(SIP_DEFAULT_PORT),
      G_STRUCT_OFFSET (TpsipConnParams, port) },
    /* Used to compose proxy URI */
    { "transport", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "auto",
      G_STRUCT_OFFSET (TpsipConnParams, transport) },
    /* Enables loose routing as per RFC 3261 */
    { "loose-routing", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      G_STRUCT_OFFSET (TpsipConnParams, loose_routing) },
    /* Used to enable proactive NAT traversal techniques */
    { "discover-binding", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE),
      G_STRUCT_OFFSET (TpsipConnParams, discover_binding) },
    /* Mechanism used for connection keepalive maintenance */
    { "keepalive-mechanism", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "auto",
      G_STRUCT_OFFSET (TpsipConnParams, keepalive_mechanism) },
    /* Keep-alive interval */
    { "keepalive-interval", DBUS_TYPE_UINT32_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(0),
      G_STRUCT_OFFSET (TpsipConnParams, keepalive_interval) },
    /* Use SRV DNS lookup to discover STUN server */
    { "discover-stun", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE),
      G_STRUCT_OFFSET (TpsipConnParams, discover_stun) },
    /* STUN server */
    { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, stun_server) },
    /* STUN port */
    { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
      GUINT_TO_POINTER(TPSIP_DEFAULT_STUN_PORT),
      G_STRUCT_OFFSET (TpsipConnParams, stun_port) },
    /* If the session content cannot be modified once initially set up */
    { "immutable-streams", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      G_STRUCT_OFFSET (TpsipConnParams, immutable_streams) },
    /* Local IP address to use, workaround purposes only */
    { "local-ip-address", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, local_ip_address) },
    /* Local port for SIP, workaround purposes only */
    { "local-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, local_port) },
    /* Extra-realm authentication */
    { "extra-auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (TpsipConnParams, extra_auth_user) },
    { "extra-auth-password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_SECRET,
      NULL, G_STRUCT_OFFSET (TpsipConnParams, extra_auth_password) },
    { NULL, NULL, 0, 0, NULL, 0 }
};

const TpCMProtocolSpec tpsip_protocols[] = {
  { "sip", tpsip_params, alloc_params, free_params },
  { NULL, NULL }
};

struct _TpsipConnectionManagerPrivate
{
  su_root_t *sofia_root;
  TpDebugSender *debug_sender;
};

#define TPSIP_CONNECTION_MANAGER_GET_PRIVATE(obj) ((obj)->priv)

static void
tpsip_connection_manager_init (TpsipConnectionManager *obj)
{
  TpsipConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
        TPSIP_TYPE_CONNECTION_MANAGER, TpsipConnectionManagerPrivate);
  GSource *source;

  obj->priv = priv;

  priv->sofia_root = su_glib_root_create(obj);
  su_root_threading(priv->sofia_root, 0);
  source = su_root_gsource(priv->sofia_root);
  g_source_attach(source, NULL);

  priv->debug_sender = tp_debug_sender_dup ();
  g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);

#ifdef HAVE_LIBIPHB
  su_root_set_max_defer (priv->sofia_root, TPSIP_DEFER_TIMEOUT * 1000L);
#endif
}

static void tpsip_connection_manager_finalize (GObject *object);
static TpBaseConnection *tpsip_connection_manager_new_connection (
    TpBaseConnectionManager *base, const gchar *proto,
    TpIntSet *params_present, void *parsed_params, GError **error);

static void
tpsip_connection_manager_class_init (TpsipConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionManagerClass *base_class = 
    (TpBaseConnectionManagerClass *)klass;

  g_type_class_add_private (klass, sizeof (TpsipConnectionManagerPrivate));

  object_class->finalize = tpsip_connection_manager_finalize;

  base_class->new_connection = tpsip_connection_manager_new_connection;
  base_class->cm_dbus_name = "sofiasip";
  base_class->protocol_params = tpsip_protocols;
}

void
tpsip_connection_manager_finalize (GObject *object)
{
  TpsipConnectionManager *self = TPSIP_CONNECTION_MANAGER (object);
  TpsipConnectionManagerPrivate *priv = TPSIP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GSource *source;

  source = su_root_gsource(priv->sofia_root);
  g_source_destroy(source);
  su_root_destroy(priv->sofia_root);

  if (priv->debug_sender != NULL)
    {
      g_object_unref (priv->debug_sender);
      priv->debug_sender = NULL;
    }

  tpsip_debug_free ();

  G_OBJECT_CLASS (tpsip_connection_manager_parent_class)->finalize (object);
}

static gchar *
priv_compose_proxy_uri (const gchar *host,
                        const gchar *transport,
                        guint port)
{
  const gchar *scheme = "sip";

  if (host == NULL)
    return NULL;

  /* Set scheme to SIPS if transport is TLS */

  if (transport != NULL && !g_ascii_strcasecmp (transport, "tls"))
    scheme = "sips";

  /* Format the resulting URI */

  if (port)
    return g_strdup_printf ("%s:%s:%u", scheme, host, port);
  else
    return g_strdup_printf ("%s:%s", scheme, host);
}

/**
 * Returns a default SIP proxy address based on the public 
 * SIP address 'sip_address' and . For instance
 * "sip:first.surname@company.com" would result in "sip:company.com".
 * The SIP stack will further utilize DNS lookups to find the IP address 
 * for the SIP server responsible for the domain "company.com".
 */
static gchar *
priv_compose_default_proxy_uri (const gchar *sip_address,
                                const gchar *transport)
{
  char *result = NULL;
  char *host;
  char *found;

  g_return_val_if_fail (sip_address != NULL, NULL);

  /* skip sip and sips prefixes, updating transport if necessary */
  found = strchr (sip_address, ':');
  if (found != NULL) {
    if (g_ascii_strncasecmp ("sip:", sip_address, 4) == 0)
      ;
    else if (g_ascii_strncasecmp ("sips:", sip_address, 5) == 0)
      {
        if (transport == NULL ||
            strcmp (transport, "auto") == 0)
          transport = "tls";
      }
    else
      {
        /* error, unknown URI prefix */
        return NULL;
      }

    sip_address = found + 1;
  }

  /* skip userinfo */
  found = strchr (sip_address, '@');
  if (found != NULL)
    sip_address = found + 1;

  /* copy rest of the string */
  host = g_strdup (sip_address);

  /* mark end (before uri-parameters defs or headers) */
  found = strchr (host, ';');
  if (found == NULL)
    found = strchr (host, '?');
  if (found != NULL)
    *found = '\0';

  result = priv_compose_proxy_uri (host, transport, 0);

  g_free (host);

  return result;
}

static TpsipConnectionKeepaliveMechanism
priv_parse_keepalive (const gchar *str)
{
  if (str == NULL || strcmp (str, "auto") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_AUTO;
  if (strcmp (str, "register") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_REGISTER;
  if (strcmp (str, "options") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_OPTIONS;
  if (strcmp (str, "stun") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_STUN;
  if (strcmp (str, "off") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_NONE;

  WARNING ("unsupported keepalive-method value \"%s\", falling back to auto", str);
  return TPSIP_CONNECTION_KEEPALIVE_AUTO;
}

#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if (tp_intset_is_member (params_present, param)) \
    { \
      g_object_set (connection, prop, member, NULL); \
    }

#define NULLIFY_IF_EMPTY(param) \
  if ((param) != NULL && (param)[0] == '\0') \
    param = NULL;

static gboolean
check_not_empty_if_present (const gchar *name,
                            const gchar *value,
                            GError **error)
{
  if (value != NULL && value[0] == '\0')
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "If supplied, '%s' account parameter may not be empty", name);
      return FALSE;
    }
  return TRUE;
}

static TpBaseConnection *
tpsip_connection_manager_new_connection (TpBaseConnectionManager *base,
                                         const gchar *proto,
                                         TpIntSet *params_present,
                                         void *parsed_params,
                                         GError **error)
{
  TpsipConnectionManager *self = TPSIP_CONNECTION_MANAGER (base);
  TpsipConnectionManagerPrivate *priv = TPSIP_CONNECTION_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *connection = NULL;
  TpsipConnParams *params = (TpsipConnParams *)parsed_params;
  gchar *proxy = NULL;
  TpsipConnectionKeepaliveMechanism keepalive_mechanism;

  if (strcmp (proto, "sip")) {
    g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "This connection manager only implements protocol 'sip', not '%s'",
        proto);
    return NULL;
  }

  /* TpBaseConnectionManager code has already checked that required params
   * are present (but not that they are non-empty, if we're using >= 0.5.8)
   */
  g_assert (params->account);

  /* FIXME: validate account SIP URI properly, using appropriate RFCs */
  if (!check_not_empty_if_present ("account", params->account, error))
    return FALSE;
  NULLIFY_IF_EMPTY (params->auth_user);
  NULLIFY_IF_EMPTY (params->password);
  /* FIXME: validate registrar SIP URI properly, using appropriate RFCs */
  NULLIFY_IF_EMPTY (params->registrar);
  /* FIXME: validate proxy host properly */
  NULLIFY_IF_EMPTY (params->proxy_host);
  /* FIXME: check against the list (which presumably exists) of valid
   * transports */
  NULLIFY_IF_EMPTY (params->transport);
  /* FIXME: check against the list (which presumably exists) of valid
   * KA mechanisms */
  NULLIFY_IF_EMPTY (params->keepalive_mechanism);
  /* FIXME: validate STUN server properly */
  NULLIFY_IF_EMPTY (params->stun_server);
  /* FIXME: validate local IP address properly */
  NULLIFY_IF_EMPTY (params->local_ip_address);
  NULLIFY_IF_EMPTY (params->extra_auth_user);
  NULLIFY_IF_EMPTY (params->extra_auth_password);

  DEBUG("New SIP connection to %s", params->account);

  connection = (TpBaseConnection *)g_object_new(TPSIP_TYPE_CONNECTION,
                            "protocol", "sip",
			    "sofia-root", priv->sofia_root,
			    "address", params->account,
                            NULL);

  if (params->proxy_host == NULL) {
    proxy = priv_compose_default_proxy_uri (params->account,
                                            params->transport);
    DEBUG("set outbound proxy address to <%s>, based on <%s>", proxy, params->account);
  } else
    proxy = priv_compose_proxy_uri (params->proxy_host,
                                    params->transport,
                                    params->port);

  g_object_set (connection, "proxy", proxy, NULL);
  g_free (proxy);

  if (params->transport != NULL && strcmp (params->transport, "auto") != 0)
    g_object_set (connection, "transport", params->transport, NULL);

  SET_PROPERTY_IF_PARAM_SET ("auth-user", TPSIP_CONN_PARAM_AUTH_USER,
      params->auth_user);

  SET_PROPERTY_IF_PARAM_SET ("password", TPSIP_CONN_PARAM_PASSWORD,
      params->password);

  SET_PROPERTY_IF_PARAM_SET ("alias", TPSIP_CONN_PARAM_ALIAS,
      params->alias);

  SET_PROPERTY_IF_PARAM_SET ("registrar", TPSIP_CONN_PARAM_REGISTRAR,
      params->registrar);

  SET_PROPERTY_IF_PARAM_SET ("loose-routing", TPSIP_CONN_PARAM_LOOSE_ROUTING,
      params->loose_routing);

  SET_PROPERTY_IF_PARAM_SET ("discover-binding", TPSIP_CONN_PARAM_DISCOVER_BINDING,
      params->discover_binding);

  SET_PROPERTY_IF_PARAM_SET ("discover-stun", TPSIP_CONN_PARAM_DISCOVER_STUN,
      params->discover_stun);

  SET_PROPERTY_IF_PARAM_SET ("stun-server", TPSIP_CONN_PARAM_STUN_SERVER,
      params->stun_server);

  SET_PROPERTY_IF_PARAM_SET ("stun-port", TPSIP_CONN_PARAM_STUN_PORT,
      params->stun_port);

  SET_PROPERTY_IF_PARAM_SET ("immutable-streams", TPSIP_CONN_PARAM_IMMUTABLE_STREAMS,
      params->immutable_streams);

  SET_PROPERTY_IF_PARAM_SET ("keepalive-interval",
      TPSIP_CONN_PARAM_KEEPALIVE_INTERVAL, params->keepalive_interval);

  keepalive_mechanism = priv_parse_keepalive (params->keepalive_mechanism);
  g_object_set (connection, "keepalive-mechanism", keepalive_mechanism, NULL);

  SET_PROPERTY_IF_PARAM_SET ("local-ip-address", TPSIP_CONN_PARAM_LOCAL_IP_ADDRESS,
      params->local_ip_address);

  SET_PROPERTY_IF_PARAM_SET ("local-port", TPSIP_CONN_PARAM_LOCAL_PORT,
      params->local_port);

  SET_PROPERTY_IF_PARAM_SET ("extra-auth-user", TPSIP_CONN_PARAM_EXTRA_AUTH_USER,
      params->extra_auth_user);
  SET_PROPERTY_IF_PARAM_SET ("extra-auth-password", TPSIP_CONN_PARAM_EXTRA_AUTH_PASSWORD,
      params->extra_auth_password);

  return connection;
}
