/*
 * sip-connection-manager.c - Source for SIPConnectionManager
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection-manager).
 *   @author See gabble-connection-manager.c
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <dbus/dbus-glib.h>
#include <dbus/dbus-protocol.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-connection-manager.h>

#include "sip-connection-manager.h"
#include "sip-connection.h"
#include "signals-marshal.h"
#include "sip-sofia-decls.h"

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"

G_DEFINE_TYPE(SIPConnectionManager, sip_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* private structure *//* typedef struct _SIPConnectionManagerPrivate SIPConnectionManagerPrivate; */

typedef struct {
    gchar *account;
    gchar *auth_user;
    gchar *password;
    gchar *registrar;
    gchar *proxy_host;
    guint port;
    gchar *transport;
    gboolean discover_binding;
    gboolean use_http_proxy;
    gchar *keepalive_mechanism;
    gint keepalive_interval;
    gboolean discover_stun;
    gchar *stun_server;
    guint stun_port;
    gchar *extra_auth_user;
    gchar *extra_auth_password;
    gboolean avoid_difficult;
} SIPConnParams;

static void *
alloc_params (void)
{
  return g_slice_new0 (SIPConnParams);
}

static void
free_params (void *p)
{
  SIPConnParams *params = (SIPConnParams *)p;

  g_free (params->account);
  g_free (params->auth_user);
  g_free (params->password);
  g_free (params->registrar);
  g_free (params->proxy_host);
  g_free (params->transport);
  g_free (params->keepalive_mechanism);
  g_free (params->stun_server);
  g_free (params->extra_auth_user);
  g_free (params->extra_auth_password);

  g_slice_free (SIPConnParams, params);
}

enum {
    SIP_CONN_PARAM_ACCOUNT = 0,
    SIP_CONN_PARAM_AUTH_USER,
    SIP_CONN_PARAM_PASSWORD,
    SIP_CONN_PARAM_REGISTRAR,
    SIP_CONN_PARAM_PROXY_HOST,
    SIP_CONN_PARAM_PORT,
    SIP_CONN_PARAM_TRANSPORT,
    SIP_CONN_PARAM_DISCOVER_BINDING,
    SIP_CONN_PARAM_USE_HTTP_PROXY,
    SIP_CONN_PARAM_KEEPALIVE_MECHANISM,
    SIP_CONN_PARAM_KEEPALIVE_INTERVAL,
    SIP_CONN_PARAM_DISCOVER_STUN,
    SIP_CONN_PARAM_STUN_SERVER,
    SIP_CONN_PARAM_STUN_PORT,
    SIP_CONN_PARAM_EXTRA_AUTH_USER,
    SIP_CONN_PARAM_EXTRA_AUTH_PASSWORD,
    SIP_CONN_PARAM_AVOID_DIFFICULT,
    N_SIP_CONN_PARAMS
};

static const TpCMParamSpec sip_params[] = {
    /* Account (a sip: URI) */
    { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
      NULL, G_STRUCT_OFFSET (SIPConnParams, account) },
    /* Username to register with, if different than in the account URI */
    { "auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, auth_user) },
    /* Password */
    { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, /* according to the .manager file this is 
      TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
      but in the code this is not the case */
      NULL, G_STRUCT_OFFSET (SIPConnParams, password) },
    /* Registrar */
    { "registrar", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, registrar) },
    /* Used to compose proxy URI */
    { "proxy-host", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, proxy_host) },
    /* Used to compose proxy URI */
    { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(SIP_DEFAULT_PORT),
      G_STRUCT_OFFSET (SIPConnParams, port) },
    /* Used to compose proxy URI */
    { "transport", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "auto",
      G_STRUCT_OFFSET (SIPConnParams, transport) },
    /* Not used */
    { "discover-binding", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE),
      G_STRUCT_OFFSET (SIPConnParams, discover_binding) },
    /* Not used */
    { "use-http-proxy", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      G_STRUCT_OFFSET (SIPConnParams, use_http_proxy) },
    /* Not used */
    { "keepalive-mechanism", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, keepalive_mechanism) },
    /* KA interval */
    { "keepalive-interval", DBUS_TYPE_INT32_AS_STRING, G_TYPE_INT,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, keepalive_interval) },
    /* Use SRV DNS lookup to discover STUN server */
    { "discover-stun", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE),
      G_STRUCT_OFFSET (SIPConnParams, discover_stun) },
    /* STUN server */
    { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, stun_server) },
    /* STUN port */
    { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
      GUINT_TO_POINTER(SIP_DEFAULT_STUN_PORT),
      G_STRUCT_OFFSET (SIPConnParams, stun_port) },
    /* Extra authentication */
    { "extra-auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, extra_auth_user) },
    { "extra-auth-password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, G_STRUCT_OFFSET (SIPConnParams, extra_auth_password) },
    /* Not used */
    { "avoid-difficult", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      G_STRUCT_OFFSET (SIPConnParams, avoid_difficult) },
    { NULL, NULL, 0, 0, NULL, 0 }
};

const TpCMProtocolSpec sofiasip_protocols[] = {
  { "sip", sip_params, alloc_params, free_params },
  { NULL, NULL }
};

struct _SIPConnectionManagerPrivate
{
  su_root_t *sofia_root;
};

#define SIP_CONNECTION_MANAGER_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_CONNECTION_MANAGER, SIPConnectionManagerPrivate))

static void
sip_connection_manager_init (SIPConnectionManager *obj)
{
  SIPConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
        SIP_TYPE_CONNECTION_MANAGER, SIPConnectionManagerPrivate);
  GSource *source;

  obj->priv = priv;

  priv->sofia_root = su_glib_root_create(obj);
  su_root_threading(priv->sofia_root, 0);
  source = su_root_gsource(priv->sofia_root);
  g_source_attach(source, NULL);
}

static void sip_connection_manager_finalize (GObject *object);
static TpBaseConnection *sip_connection_manager_new_connection (
    TpBaseConnectionManager *base, const gchar *proto,
    TpIntSet *params_present, void *parsed_params, GError **error);

static void
sip_connection_manager_class_init (SIPConnectionManagerClass *sip_connection_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_connection_manager_class);
  TpBaseConnectionManagerClass *base_class = 
    (TpBaseConnectionManagerClass *)sip_connection_manager_class;

  g_type_class_add_private (sip_connection_manager_class, sizeof (SIPConnectionManagerPrivate));

  object_class->finalize = sip_connection_manager_finalize;

  base_class->new_connection = sip_connection_manager_new_connection;
  base_class->cm_dbus_name = "sofiasip";
  base_class->protocol_params = sofiasip_protocols;
}

void
sip_connection_manager_finalize (GObject *object)
{
  SIPConnectionManager *self = SIP_CONNECTION_MANAGER (object);
  SIPConnectionManagerPrivate *priv = SIP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GSource *source;

  source = su_root_gsource(priv->sofia_root);
  g_source_destroy(source);
  su_root_destroy(priv->sofia_root);

  G_OBJECT_CLASS (sip_connection_manager_parent_class)->finalize (object);
}

static gchar *
priv_compose_proxy_uri (const gchar *host,
                        const gchar *transport,
                        guint port)
{
  const gchar *scheme = "sip";
  const gchar *params = "";

  if (host == NULL)
    return NULL;

  /* Encode transport */

  if (transport == NULL || !strcmp (transport, "auto")) {
    /*no mention of transport in the URI*/
  } else if (!strcmp (transport, "tcp")) {
    params = ";transport=tcp";
  } else if (!strcmp (transport, "udp")) {
    params = ";transport=udp";
  } else if (!strcmp (transport, "tls")) {
    scheme = "sips";
  } else {
    g_warning ("transport %s not recognized", transport);
  }

  /* Format the resulting URI */

  if (port) {
    return g_strdup_printf ("%s:%s:%u%s", scheme, host, port, params);
  } else {
    return g_strdup_printf ("%s:%s%s", scheme, host, params);
  }
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
            g_ascii_strncasecmp (transport, "auto", 4) == 0)
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

static SIPConnectionKeepaliveMechanism
priv_parse_keepalive (const gchar *str)
{
  if (str == NULL || strcmp (str, "auto") == 0)
    return SIP_CONNECTION_KEEPALIVE_AUTO;
  if (strcmp (str, "register") == 0)
    return SIP_CONNECTION_KEEPALIVE_REGISTER;
  if (strcmp (str, "options") == 0)
    return SIP_CONNECTION_KEEPALIVE_OPTIONS;
  if (strcmp (str, "stun") == 0)
    return SIP_CONNECTION_KEEPALIVE_STUN;
  if (strcmp (str, "off") == 0)
    return SIP_CONNECTION_KEEPALIVE_NONE;

  g_warning ("unsupported keepalive-method value \"%s\", falling back to auto", str);
  return SIP_CONNECTION_KEEPALIVE_AUTO;
}

#define SET_PROPERTY_IF_PARAM_SET(prop, param, member) \
  if (tp_intset_is_member (params_present, param)) \
    { \
      g_object_set (connection, prop, member, NULL); \
    }

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

/**
 * sip_connection_manager_request_connection
 *
 * Implements DBus method RequestConnection
 * on interface org.freedesktop.Telepathy.ConnectionManager
 */
static TpBaseConnection *
sip_connection_manager_new_connection (TpBaseConnectionManager *base,
                                       const gchar *proto,
                                       TpIntSet *params_present,
                                       void *parsed_params,
                                       GError **error)
{
  SIPConnectionManager *obj = SIP_CONNECTION_MANAGER (base);
  TpBaseConnection *connection = NULL;
  SIPConnParams *params = (SIPConnParams *)parsed_params;
  gchar *proxy = NULL;
  SIPConnectionKeepaliveMechanism keepalive_mechanism;

  if (strcmp (proto, "sip")) {
    g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "This connection manager only implements protocol 'sip', not '%s'",
        proto);
    return NULL;
  }

  /* TODO: retrieve global HTTP proxy settings if
   * "use-http-proxy" parameter is enabled */

  /* TpBaseConnectionManager code has already checked that required params
   * are present (but not that they are non-empty, if we're using >= 0.5.8)
   */
  g_assert (params->account);

  /* FIXME: validate account SIP URI properly, using appropriate RFCs */
  if (!check_not_empty_if_present ("account", params->account, error))
    return FALSE;
  /* FIXME: validate registrar SIP URI properly, using appropriate RFCs */
  if (!check_not_empty_if_present ("registrar", params->registrar, error))
    return FALSE;
  /* FIXME: validate proxy host properly */
  if (!check_not_empty_if_present ("proxy-host", params->proxy_host, error))
    return FALSE;
  /* FIXME: check against the list (which presumably exists) of valid
   * transports */
  if (!check_not_empty_if_present ("transport", params->transport, error))
    return FALSE;
  /* FIXME: check against the list (which presumably exists) of valid
   * KA mechanisms */
  if (!check_not_empty_if_present ("keepalive-mechanism",
        params->keepalive_mechanism, error))
    return FALSE;
  /* FIXME: validate STUN server properly */
  if (!check_not_empty_if_present ("stun-server", params->stun_server,
        error))
    return FALSE;

  DEBUG("New SIP connection to %s", params->account);

  connection = (TpBaseConnection *)g_object_new(SIP_TYPE_CONNECTION,
                            "protocol", "sip",
			    "sofia-root", obj->priv->sofia_root,
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

  SET_PROPERTY_IF_PARAM_SET ("auth-user", SIP_CONN_PARAM_AUTH_USER,
      params->auth_user);

  SET_PROPERTY_IF_PARAM_SET ("password", SIP_CONN_PARAM_PASSWORD,
      params->password);

  SET_PROPERTY_IF_PARAM_SET ("registrar", SIP_CONN_PARAM_REGISTRAR,
      params->registrar);

  SET_PROPERTY_IF_PARAM_SET ("discover-binding", SIP_CONN_PARAM_DISCOVER_BINDING,
      params->discover_binding);

  SET_PROPERTY_IF_PARAM_SET ("discover-stun", SIP_CONN_PARAM_DISCOVER_STUN,
      params->discover_stun);

  SET_PROPERTY_IF_PARAM_SET ("stun-server", SIP_CONN_PARAM_STUN_SERVER,
      params->stun_server);

  SET_PROPERTY_IF_PARAM_SET ("stun-port", SIP_CONN_PARAM_STUN_PORT,
      params->stun_port);

  SET_PROPERTY_IF_PARAM_SET ("keepalive-interval",
      SIP_CONN_PARAM_KEEPALIVE_INTERVAL, params->keepalive_interval);

  keepalive_mechanism = priv_parse_keepalive (params->keepalive_mechanism);
  g_object_set (connection, "keepalive-mechanism", keepalive_mechanism, NULL);

  SET_PROPERTY_IF_PARAM_SET ("extra-auth-user", SIP_CONN_PARAM_EXTRA_AUTH_USER,
      params->extra_auth_user);
  SET_PROPERTY_IF_PARAM_SET ("extra-auth-password", SIP_CONN_PARAM_EXTRA_AUTH_PASSWORD,
      params->extra_auth_password);

  return connection;
}
