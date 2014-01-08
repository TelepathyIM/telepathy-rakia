/*
 * sip-connection.c - Source for RakiaConnection
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection).
 *   @author See gabble-connection.c
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

#include <telepathy-glib/telepathy-glib-dbus.h>

#include <rakia/event-target.h>
#include <rakia/handles.h>
#include <rakia/connection-aliasing.h>
#include <rakia/media-manager.h>
#include <rakia/text-manager.h>
#include <rakia/util.h>

#include "sip-connection.h"

#include "sip-connection-enumtypes.h"
#include "sip-connection-helpers.h"
#include "sip-connection-private.h"

#include <sofia-sip/msg_header.h>
#include <sofia-sip/tport_tag.h>

#define DEBUG_FLAG RAKIA_DEBUG_CONNECTION
#include "rakia/debug.h"

G_DEFINE_TYPE_WITH_CODE (RakiaConnection, rakia_connection,
    RAKIA_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING1,
        rakia_connection_aliasing_svc_iface_init);
    G_IMPLEMENT_INTERFACE (RAKIA_TYPE_CONNECTION_ALIASING, NULL);
);


/* properties */
enum
{
  PROP_ADDRESS = 1,      /**< public SIP address (SIP URI) */
  PROP_AUTH_USER,        /**< account username (if different from public address userinfo part) */
  PROP_PASSWORD,         /**< account password (for registration) */
  PROP_ALIAS,               /* Display name for self */

  PROP_TRANSPORT,        /**< outbound transport */
  PROP_PROXY,            /**< outbound SIP proxy (SIP URI) */
  PROP_REGISTRAR,        /**< SIP registrar (SIP URI) */
  PROP_LOOSE_ROUTING,       /**< enable loose routing behavior */
  PROP_KEEPALIVE_MECHANISM, /**< keepalive mechanism as defined by RakiaConnectionKeepaliveMechanism */
  PROP_KEEPALIVE_INTERVAL, /**< keepalive interval in seconds */
  PROP_DISCOVER_BINDING,   /**< enable discovery of public binding */
  PROP_DISCOVER_STUN,      /**< Discover STUN server name using DNS SRV lookup */
  PROP_STUN_SERVER,        /**< STUN server address (if not set, derived
			        from public SIP address */
  PROP_STUN_PORT,          /**< STUN port */
  PROP_IMMUTABLE_STREAMS,  /**< If the session content is immutable once set up */
  PROP_LOCAL_IP_ADDRESS,   /**< Local IP address (normally not needed, chosen by stack) */
  PROP_LOCAL_PORT,         /**< Local port for SIP (normally not needed, chosen by stack) */
  PROP_EXTRA_AUTH_USER,	   /**< User name to use for extra authentication challenges */
  PROP_EXTRA_AUTH_PASSWORD,/**< Password to use for extra authentication challenges */
  PROP_IGNORE_TLS_ERRORS,  /**< If true, TLS errors will be ignored */
  PROP_SOFIA_NUA,          /**< Base class accessing nua_t */
  LAST_PROPERTY
};

static TpDBusPropertiesMixinPropImpl conn_aliasing_properties[] = {
    { "AliasFlags", GUINT_TO_POINTER (0), NULL },
    { NULL }
};

static void
conn_aliasing_properties_getter (GObject *object,
    GQuark interface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  g_value_set_uint (value, GPOINTER_TO_UINT (getter_data));
}

static void
priv_value_set_url_as_string (GValue *value, const url_t *url)
{
  if (url == NULL)
    {
      g_value_set_string (value, NULL);
    }
  else
    {
      su_home_t temphome[1] = { SU_HOME_INIT(temphome) };
      char *tempstr;
      tempstr = url_as_string (temphome, url);
      g_value_set_string (value, tempstr);
      su_home_deinit (temphome);
    }
}

static url_t *
priv_url_from_string_value (su_home_t *home, const GValue *value)
{
  const gchar *url_str;
  g_assert (home != NULL);
  url_str = g_value_get_string (value);
  return (url_str)? url_make (home, url_str) : NULL;
}

static void
rakia_create_handle_repos (TpBaseConnection *conn,
                           TpHandleRepoIface *repos[TP_NUM_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_CONTACT,
          "normalize-function", rakia_handle_normalize,
          "default-normalize-context", conn,
          NULL);
}

static GPtrArray *
rakia_connection_create_channel_managers (TpBaseConnection *conn)
{
  RakiaConnection *self = RAKIA_CONNECTION (conn);
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);
  GPtrArray *channel_managers = g_ptr_array_sized_new (2);

  g_ptr_array_add (channel_managers,
      g_object_new (RAKIA_TYPE_TEXT_MANAGER,
        "connection", self, NULL));

  priv->media_manager = g_object_new (RAKIA_TYPE_MEDIA_MANAGER,
        "connection", self, NULL);
  g_ptr_array_add (channel_managers, priv->media_manager);

  priv->password_manager = tp_simple_password_manager_new (
      conn);
  g_ptr_array_add (channel_managers, priv->password_manager);

  return channel_managers;
}

static void
rakia_connection_init (RakiaConnection *self)
{
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  priv->sofia_home = su_home_new(sizeof (su_home_t));
}

static void
rakia_connection_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  RakiaConnection *self = (RakiaConnection*) object;
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
  case PROP_ADDRESS: {
    /* just store the address, self_handle set in start_connecting */
    priv->address = g_value_dup_string (value);
    break;
  }
  case PROP_AUTH_USER: {
    g_free(priv->auth_user);
    priv->auth_user = g_value_dup_string (value);
    break;
  }
  case PROP_PASSWORD: {
    g_free(priv->password);
    priv->password = g_value_dup_string (value);
    break;
  }
  case PROP_ALIAS: {
    g_free(priv->alias);
    priv->alias = g_value_dup_string (value);
    break;
  }
  case PROP_TRANSPORT: {
    g_free(priv->transport);
    priv->transport = g_value_dup_string (value);
    break;
  }
  case PROP_PROXY: {
    priv->proxy_url = priv_url_from_string_value (priv->sofia_home, value);
    break;
  }
  case PROP_REGISTRAR: {
    priv->registrar_url = priv_url_from_string_value (priv->sofia_home, value);
    if (priv->sofia_nua)
      nua_set_params(priv->sofia_nua,
                     NUTAG_REGISTRAR(priv->registrar_url),
                     TAG_END());
    break;
  }
  case PROP_LOOSE_ROUTING: {
    priv->loose_routing = g_value_get_boolean (value);
    break;
  }
  case PROP_KEEPALIVE_MECHANISM: {
    RakiaConnectionKeepaliveMechanism mech = g_value_get_enum (value);
    if (priv->keepalive_interval_specified && priv->keepalive_interval == 0)
      {
        if (mech != RAKIA_CONNECTION_KEEPALIVE_NONE
            && mech != RAKIA_CONNECTION_KEEPALIVE_AUTO)
          WARNING ("keep-alive mechanism selection is ignored when interval is 0");
      }
    else
      {
        priv->keepalive_mechanism = mech;
        if (priv->sofia_nua != NULL)
          {
            rakia_conn_update_nua_outbound (self);
            rakia_conn_update_nua_keepalive_interval (self);
          }
      }
    break;
  }
  case PROP_KEEPALIVE_INTERVAL: {
    priv->keepalive_interval = g_value_get_uint (value);
    priv->keepalive_interval_specified = TRUE;
    if (priv->keepalive_interval == 0)
      {
        priv->keepalive_mechanism = RAKIA_CONNECTION_KEEPALIVE_NONE;
        if (priv->sofia_nua != NULL)
          rakia_conn_update_nua_outbound (self);
      }
    if (priv->sofia_nua)
      {
        rakia_conn_update_nua_keepalive_interval(self);
      }
    break;
  }
  case PROP_DISCOVER_BINDING: {
    priv->discover_binding = g_value_get_boolean (value);
    if (priv->sofia_nua)
      rakia_conn_update_nua_outbound (self);
    break;
  }
  case PROP_DISCOVER_STUN:
    priv->discover_stun = g_value_get_boolean (value);
    break;
  case PROP_STUN_PORT: {
    priv->stun_port = g_value_get_uint (value);
    break;
  }
  case PROP_STUN_SERVER: {
    g_free (priv->stun_host);
    priv->stun_host = g_value_dup_string (value);
    break;
  }
  case PROP_IMMUTABLE_STREAMS:
    priv->immutable_streams = g_value_get_boolean (value);
    break;
  case PROP_LOCAL_IP_ADDRESS: {
    g_free (priv->local_ip_address);
    priv->local_ip_address = g_value_dup_string (value);
    break;
  }
  case PROP_LOCAL_PORT: {
    priv->local_port = g_value_get_uint (value);
    break;
  }
  case PROP_EXTRA_AUTH_USER: {
    g_free((gpointer)priv->extra_auth_user);
    priv->extra_auth_user =  g_value_dup_string (value);
    break;
  }
  case PROP_EXTRA_AUTH_PASSWORD: {
    g_free((gpointer)priv->extra_auth_password);
    priv->extra_auth_password =  g_value_dup_string (value);
    break;
  }
  case PROP_IGNORE_TLS_ERRORS:
    priv->ignore_tls_errors = g_value_get_boolean (value);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void
rakia_connection_get_property (GObject      *object,
                             guint         property_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
  RakiaConnection *self = (RakiaConnection *) object;
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
  case PROP_ADDRESS: {
    g_value_set_string (value, priv->address);
    break;
  }
  case PROP_AUTH_USER: {
    g_value_set_string (value, priv->auth_user);
    break;
  }
  case PROP_PASSWORD: {
    g_value_set_string (value, priv->password);
    break;
  }
  case PROP_ALIAS: {
    g_value_set_string (value, priv->alias);
    break;
  }
  case PROP_TRANSPORT: {
    g_value_set_string (value, priv->transport);
    break;
  }
  case PROP_PROXY: {
    priv_value_set_url_as_string (value, priv->proxy_url);
    break;
  }
  case PROP_REGISTRAR: {
    priv_value_set_url_as_string (value, priv->registrar_url);
    break;
  }
  case PROP_LOOSE_ROUTING: {
    g_value_set_boolean (value, priv->loose_routing);
    break;
  }
  case PROP_KEEPALIVE_MECHANISM: {
    g_value_set_enum (value, priv->keepalive_mechanism);
    break;
  }
  case PROP_KEEPALIVE_INTERVAL: {
    /* FIXME: get the keepalive interval from NUA in case anything
     * really retrieves this property */
    g_value_set_uint (value, priv->keepalive_interval);
    break;
  }
  case PROP_DISCOVER_BINDING: {
    g_value_set_boolean (value, priv->discover_binding);
    break;
  }
  case PROP_DISCOVER_STUN:
    g_value_set_boolean (value, priv->discover_stun);
    break;
  case PROP_STUN_SERVER: {
    g_value_set_string (value, priv->stun_host);
    break;
  }
  case PROP_STUN_PORT: {
    g_value_set_uint (value, priv->stun_port);
    break;
  }
  case PROP_IMMUTABLE_STREAMS:
    g_value_set_boolean (value, priv->immutable_streams);
    break;
  case PROP_LOCAL_IP_ADDRESS: {
    g_value_set_string (value, priv->local_ip_address);
    break;
  }
  case PROP_LOCAL_PORT: {
    g_value_set_uint (value, priv->local_port);
    break;
  }
  case PROP_IGNORE_TLS_ERRORS:
    g_value_set_boolean (value, priv->ignore_tls_errors);
    break;
  case PROP_SOFIA_NUA: {
    g_value_set_pointer (value, priv->sofia_nua);
    break;
  }
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void rakia_connection_dispose (GObject *object);
static void rakia_connection_finalize (GObject *object);

static gchar *
rakia_connection_unique_name (TpBaseConnection *base)
{
  RakiaConnection *conn = RAKIA_CONNECTION (base);
  RakiaConnectionPrivate *priv;

  g_assert (RAKIA_IS_CONNECTION (conn));
  priv = RAKIA_CONNECTION_GET_PRIVATE (conn);
  return g_strdup (priv->address);
}

static void rakia_connection_disconnected (TpBaseConnection *base);
static void rakia_connection_shut_down (TpBaseConnection *base);
static gboolean rakia_connection_start_connecting (TpBaseConnection *base,
    GError **error);

static const gchar *interfaces_always_present[] = {
    TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
    NULL };

const gchar **
rakia_connection_get_implemented_interfaces (void)
{
  /* we don't have any conditionally-implemented interfaces */
  return interfaces_always_present;
}

static GPtrArray *
get_interfaces_always_present (TpBaseConnection *base)
{
  GPtrArray *arr;
  const gchar **iter;

  arr = TP_BASE_CONNECTION_CLASS (
      rakia_connection_parent_class)->get_interfaces_always_present (base);

  for (iter = interfaces_always_present; *iter != NULL; iter++)
    g_ptr_array_add (arr, (gchar *) *iter);

  return arr;
}

static nua_handle_t *rakia_connection_create_nua_handle (RakiaBaseConnection *,
    TpHandle);
static void rakia_connection_add_auth_handler (RakiaBaseConnection *,
    RakiaEventTarget *);

static void
rakia_connection_fill_contact_attributes (TpBaseConnection *base,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  if (rakia_conn_aliasing_fill_contact_attributes (base,
        dbus_interface, handle, attributes))
    return;

  TP_BASE_CONNECTION_CLASS (rakia_connection_parent_class)->
    fill_contact_attributes (base, dbus_interface, handle, attributes);
}

static void
rakia_connection_class_init (RakiaConnectionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
  RakiaBaseConnectionClass *sip_class = RAKIA_BASE_CONNECTION_CLASS (klass);
  GParamSpec *param_spec;
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
          conn_aliasing_properties_getter,
          NULL,
          conn_aliasing_properties,
        },
        { NULL }
  };

  /* Implement pure-virtual methods */
  sip_class->create_handle = rakia_connection_create_nua_handle;
  sip_class->add_auth_handler = rakia_connection_add_auth_handler;

  base_class->create_handle_repos = rakia_create_handle_repos;
  base_class->get_unique_connection_name = rakia_connection_unique_name;
  base_class->create_channel_managers =
      rakia_connection_create_channel_managers;
  base_class->disconnected = rakia_connection_disconnected;
  base_class->start_connecting = rakia_connection_start_connecting;
  base_class->shut_down = rakia_connection_shut_down;
  base_class->get_interfaces_always_present = get_interfaces_always_present;
  base_class->fill_contact_attributes =
    rakia_connection_fill_contact_attributes;

  g_type_class_add_private (klass, sizeof (RakiaConnectionPrivate));

  object_class->dispose = rakia_connection_dispose;
  object_class->finalize = rakia_connection_finalize;

  object_class->set_property = rakia_connection_set_property;
  object_class->get_property = rakia_connection_get_property;

  klass->properties_class.interfaces = prop_interfaces;

#define INST_PROP(x) \
  g_object_class_install_property (object_class,  x, param_spec)

  g_object_class_override_property (object_class, PROP_SOFIA_NUA, "sofia-nua");

  param_spec = g_param_spec_string ("address", "SIP address",
      "SIP AoR URI",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_ADDRESS);

  param_spec = g_param_spec_string ("auth-user", "Auth username",
      "Username to use when registering "
      "(if different than userinfo part of public SIP address)",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_AUTH_USER);

  param_spec = g_param_spec_string ("password", "SIP account password",
      "Password for SIP registration",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_PASSWORD);

  g_object_class_override_property (object_class, PROP_ALIAS, "alias");

  param_spec = g_param_spec_string ("transport", "Transport protocol",
      "Preferred transport protocol (auto, udp, tcp)",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_TRANSPORT);

  param_spec = g_param_spec_string ("proxy", "Outbound proxy",
      "SIP URI for outbound proxy",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_PROXY);

  param_spec = g_param_spec_string ("registrar", "Registrar",
      "SIP URI for registrar",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_REGISTRAR);

  param_spec = g_param_spec_boolean ("loose-routing", "Loose routing",
      "Enable loose routing as per RFC 3261",
      FALSE, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_LOOSE_ROUTING);

  param_spec = g_param_spec_enum ("keepalive-mechanism", "Keepalive mechanism",
      "Keepalive mechanism for SIP registration",
      RAKIA_TYPE_CONNECTION_KEEPALIVE_MECHANISM,
      RAKIA_CONNECTION_KEEPALIVE_AUTO, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_KEEPALIVE_MECHANISM);

  param_spec = g_param_spec_uint ("keepalive-interval", "Keepalive interval",
      "Interval between keepalive probes in seconds "
      "(0 = disabled, unset = use a default interval)",
      0, G_MAXUINT32, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_KEEPALIVE_INTERVAL);

  param_spec = g_param_spec_boolean ("discover-binding",
      "Discover public contact",
      "Enable discovery of public IP address beyond NAT",
      TRUE, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_DISCOVER_BINDING);

  param_spec = g_param_spec_boolean ("discover-stun", "Discover STUN server",
      "Enable discovery of STUN server host name using DNS SRV lookup",
      TRUE, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_DISCOVER_STUN);

  param_spec = g_param_spec_string ("stun-server", "STUN server address",
      "STUN server address (FQDN or IP address)",
      NULL,
      G_PARAM_READWRITE |  G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_STUN_SERVER);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
      "STUN server port",
      0, G_MAXUINT16,
      RAKIA_DEFAULT_STUN_PORT, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_STUN_PORT);

  param_spec = g_param_spec_boolean ("immutable-streams", "Immutable streams",
      "Set if additional streams cannot be requested on a media channel,"
      " nor existing streams can be removed",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_IMMUTABLE_STREAMS);

  param_spec = g_param_spec_string ("local-ip-address", "Local IP address",
      "Local IP address to use",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_LOCAL_IP_ADDRESS);

  param_spec = g_param_spec_uint ("local-port", "Local port",
      "Local port for SIP",
      0, G_MAXUINT16,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_LOCAL_PORT);

  param_spec = g_param_spec_string ("extra-auth-user", "Extra auth username",
      "Username to use for extra authentication challenges",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_EXTRA_AUTH_USER);

  param_spec = g_param_spec_string ("extra-auth-password", "Extra auth password",
      "Password to use for extra authentication challenges",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_EXTRA_AUTH_PASSWORD);

  param_spec = g_param_spec_boolean ("ignore-tls-errors", "Ignore TLS errors",
      "If true, the TLS verification errors will be ignored",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_IGNORE_TLS_ERRORS);

#undef INST_PROP

  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (RakiaConnectionClass, properties_class));
}

typedef struct {
  RakiaConnection* self;
  nua_handle_t *nh;
  gchar *method;
  gchar *realm;
  gchar *user;
} PrivHandleAuthData;

static PrivHandleAuthData *
priv_handle_auth_data_new (RakiaConnection* self,
                           nua_handle_t *nh,
                           const gchar *method,
                           const gchar *realm,
                           const gchar *user)
{
  PrivHandleAuthData *data = g_slice_new (PrivHandleAuthData);

  data->self = g_object_ref (self);
  data->nh = nua_handle_ref (nh);
  data->method = g_strdup (method);
  data->realm = g_strdup (realm);
  data->user = g_strdup (user);

  return data;
}

static void
priv_handle_auth_data_free (PrivHandleAuthData *data)
{
  g_object_unref (data->self);
  nua_handle_unref (data->nh);
  g_free (data->method);
  g_free (data->realm);
  g_free (data->user);

  g_slice_free (PrivHandleAuthData, data);
}

static void priv_password_manager_prompt_cb (GObject *source_object,
                                             GAsyncResult *result,
                                             gpointer user_data);
static void priv_handle_auth_continue (RakiaConnection* self,
                                       nua_handle_t *nh,
                                       const gchar *method,
                                       const gchar *realm,
                                       const gchar *user,
                                       const gchar *password);

static gboolean
priv_handle_auth (RakiaConnection* self,
                  int status,
                  nua_handle_t *nh,
                  const sip_t *sip,
                  gboolean home_realm)
{
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);
  sip_www_authenticate_t const *wa;
  sip_proxy_authenticate_t const *pa;
  const char *method = NULL;
  const char *realm = NULL;
  const char *user =  NULL;
  const char *password =  NULL;

  if (status != 401 && status != 407)
    return FALSE;

  DEBUG("response presents an authentication challenge");

  g_return_val_if_fail (sip != NULL, FALSE);

  wa = sip->sip_www_authenticate;
  pa = sip->sip_proxy_authenticate;

  /* step: figure out the realm of the challenge */
  if (wa) {
    realm = msg_header_find_param ((msg_common_t *) wa, "realm=");
    method = wa->au_scheme;
  }
  else if (pa) {
    realm = msg_header_find_param ((msg_common_t *) pa, "realm=");
    method = pa->au_scheme;
  }

  if (realm == NULL)
    {
      WARNING ("no realm presented for authentication");
      return FALSE;
    }

  if (method == NULL)
    {
      WARNING ("no method presented for authentication");
      return FALSE;
    }

  /* step: determine which set of credentials to use */
  if (home_realm)
    {
      /* Save the realm presented by the registrar */
      if (priv->registrar_realm == NULL)
        priv->registrar_realm = g_strdup (realm);
      else if (wa && strcmp(priv->registrar_realm, realm) != 0)
        {
          MESSAGE ("registrar realm changed from %s to %s", priv->registrar_realm, realm);
          g_free (priv->registrar_realm);
          priv->registrar_realm = g_strdup (realm);
        }
    }
  else if (priv->registrar_realm != NULL
           && strcmp(priv->registrar_realm, realm) == 0)
    home_realm = TRUE;

  if (home_realm)
    {
      /* use authentication username if provided */
      user = priv->auth_user;
      password = priv->password;

      DEBUG("using the primary auth credentials");
    }
  else
    {
      if (priv->extra_auth_user)
        user = priv->extra_auth_user;
      else
        /* fall back to the main username */
        user = priv->auth_user;
      password = priv->extra_auth_password;
      if (password == NULL)
        /* note that this prevents asking the user for a password */
        password = "";

      DEBUG("using the extra auth credentials");
    }

  if (user == NULL)
    {
      sip_from_t const *sipfrom = sip->sip_from;
      if (sipfrom && sipfrom->a_url[0].url_user)
        /* or use the userpart in "From" header */
        user = sipfrom->a_url[0].url_user;
      else
        return FALSE;
    }

  if (password == NULL)
    {
      PrivHandleAuthData *data = NULL;

      DEBUG("asking the user for a password.");
      data = priv_handle_auth_data_new (self, nh, method, realm,
                                        user);
      tp_simple_password_manager_prompt_async (priv->password_manager,
          priv_password_manager_prompt_cb, data);
      /* Promise that we'll handle it eventually, even if we end up just
       * handling it with a blank password. */
      return TRUE;
    }

  priv_handle_auth_continue (self, nh, method, realm,
                             user, password);
  return TRUE;
}

static void
priv_password_manager_prompt_cb (GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data)
{
  PrivHandleAuthData *data = user_data;
  GError *error = NULL;
  const GString *password_string;
  const gchar *password;

  password_string = tp_simple_password_manager_prompt_finish (
      TP_SIMPLE_PASSWORD_MANAGER (source_object), result, &error);

  if (error != NULL)
    {
      /* we promised to handle the auth challenge in priv_handle_auth() by
       * returning TRUE, so we need to handle it anyway, even if it means
       * doing it with a blank password.
       */
      DEBUG ("Auth channel failed: %s. Using blank password.", error->message);

      password = "";

      g_error_free (error);
    }
  else
    {
      RakiaConnectionPrivate *priv =
          RAKIA_CONNECTION_GET_PRIVATE (data->self);

      password = password_string->str;
      /* also save it for later. */
      g_free (priv->password);
      priv->password = g_strdup (password);
    }

  priv_handle_auth_continue (data->self, data->nh, data->method, data->realm,
                             data->user, password);

  priv_handle_auth_data_free (data);
}

static void
priv_handle_auth_continue (RakiaConnection* self,
    nua_handle_t *nh,
    const gchar *method,
    const gchar *realm,
    const gchar *user,
    const gchar *password)
{
  gchar *auth = NULL;

  /* step: if all info is available, create an authorization response */
  g_assert (realm != NULL);
  g_assert (method != NULL);
  g_assert (user != NULL);
  g_assert (password != NULL);

  if (realm[0] == '"')
    auth = g_strdup_printf ("%s:%s:%s:%s",
                            method, realm, user, password);
  else
    auth = g_strdup_printf ("%s:\"%s\":%s:%s",
                            method, realm, user, password);

  DEBUG ("%s-authenticating user='%s' realm=%s",
         method, user, realm);

  /* step: authenticate */
  nua_authenticate(nh, NUTAG_AUTH(auth), TAG_END());

  g_free (auth);
}

static gboolean
rakia_connection_auth_cb (RakiaEventTarget *target,
                          const RakiaNuaEvent *ev,
                          tagi_t            tags[],
                          RakiaConnection  *self)
{
  return priv_handle_auth (self,
                           ev->status,
                           ev->nua_handle,
                           ev->sip,
                           FALSE);
}

static void
rakia_connection_add_auth_handler (RakiaBaseConnection *self,
                                   RakiaEventTarget *target)
{
  g_signal_connect_object (target,
                           "nua-event",
                           G_CALLBACK (rakia_connection_auth_cb),
                           self,
                           0);
}

static nua_handle_t *
rakia_connection_create_nua_handle (RakiaBaseConnection *base, TpHandle handle)
{
  return rakia_conn_create_request_handle (RAKIA_CONNECTION (base), handle);
}

static gboolean
rakia_connection_nua_r_register_cb (RakiaConnection     *self,
                                    const RakiaNuaEvent *ev,
                                    tagi_t               tags[],
                                    gpointer             foo)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpConnectionStatus conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
  TpConnectionStatusReason reason = 0;

  if (ev->status < 200)
    return TRUE;

  if (priv_handle_auth (self, ev->status, ev->nua_handle, ev->sip, TRUE))
    return TRUE;

  switch (ev->status)
    {
    case 401:
    case 403:
    case 407:
    case 904:   /* XXX: used by the stack to report auth loops */
      DEBUG("REGISTER failed, possibly wrong credentials, disconnecting");
      conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
      reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
      break;
    default:
      if (ev->status >= 300)
        {
          DEBUG("REGISTER failed, disconnecting");
          conn_status = TP_CONNECTION_STATUS_DISCONNECTED;
          reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
        }
      else /* if (ev->status == 200) */
        {
          if (tp_base_connection_get_status (base) != TP_CONNECTION_STATUS_CONNECTING)
            return TRUE;

          DEBUG("successfully registered to the network");
          conn_status = TP_CONNECTION_STATUS_CONNECTED;
          reason = TP_CONNECTION_STATUS_REASON_REQUESTED;

          rakia_conn_heartbeat_init (self);
        }
    }

  tp_base_connection_change_status (base, conn_status, reason);

  return TRUE;
}

static void
rakia_connection_shut_down (TpBaseConnection *base)
{
  RakiaConnection *self = RAKIA_CONNECTION (base);
  RakiaConnectionPrivate *priv;

  DEBUG ("enter");

  priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  /* We disposed of the REGISTER handle in the disconnected method */
  g_assert (priv->register_op == NULL);

  rakia_conn_heartbeat_shutdown (self);

  if (priv->sofia_nua != NULL)
    nua_shutdown (priv->sofia_nua);

  priv->sofia_nua = NULL;

  tp_base_connection_finish_shutdown (base);
}

void
rakia_connection_dispose (GObject *object)
{
  RakiaConnection *self = RAKIA_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *)self;
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  DEBUG("disposing of RakiaConnection %p", self);

  /* the base class is responsible for unreffing the self handle when we
   * disconnect */
  g_assert (tp_base_connection_get_status (base) ==
      TP_CONNECTION_STATUS_DISCONNECTED);

  /* the base class owns channel factories/managers,
   * here we just nullify the references */
  priv->media_manager = NULL;

  if (G_OBJECT_CLASS (rakia_connection_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_connection_parent_class)->dispose (object);
}

void
rakia_connection_finalize (GObject *obj)
{
  RakiaConnection *self = RAKIA_CONNECTION (obj);
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  DEBUG("enter");

  if (NULL != priv->sofia_resolver)
    {
      DEBUG("destroying sofia resolver");
      sres_resolver_destroy (priv->sofia_resolver);
      priv->sofia_resolver = NULL;
    }

  su_home_unref (priv->sofia_home);

  g_free (priv->address);
  g_free (priv->auth_user);
  g_free (priv->password);
  g_free (priv->alias);
  g_free (priv->transport);
  g_free (priv->stun_host);
  g_free (priv->local_ip_address);
  g_free (priv->extra_auth_user);
  g_free (priv->extra_auth_password);

  g_free (priv->registrar_realm);

  G_OBJECT_CLASS (rakia_connection_parent_class)->finalize (obj);
}

static gboolean
rakia_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
  RakiaConnection *self = RAKIA_CONNECTION (base);
  RakiaBaseConnection *rbase = RAKIA_BASE_CONNECTION (self);
  RakiaConnectionPrivate *priv = RAKIA_CONNECTION_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo;
  const gchar *sip_address;
  const url_t *local_url;
  su_root_t *root = NULL;
  TpHandle self_handle;

  g_assert (tp_base_connection_get_status (base) ==
      TP_CONNECTION_STATUS_DISCONNECTED);

  /* the construct parameters will be non-empty */
  g_object_get (self, "sofia-root", &root, NULL);
  g_assert (root != NULL);
  g_return_val_if_fail (priv->address != NULL, FALSE);

  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  self_handle = tp_handle_ensure (contact_repo, priv->address,
      NULL, error);
  if (self_handle == 0)
    {
      return FALSE;
    }

  tp_base_connection_set_self_handle (base, self_handle);

  sip_address = tp_handle_inspect(contact_repo, self_handle);

  DEBUG("self_handle = %d, sip_address = %s", self_handle, sip_address);

  priv->account_url = rakia_base_connection_handle_to_uri (rbase,
      tp_base_connection_get_self_handle (base));
  if (priv->account_url == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Failed to create the account URI");
      return FALSE;
    }

  local_url = rakia_conn_get_local_url (self);

  /* step: create stack instance */
  priv->sofia_nua = nua_create (root,
      rakia_base_connection_sofia_callback,
      RAKIA_BASE_CONNECTION (self),
      SOATAG_AF(SOA_AF_IP4_IP6),
      SIPTAG_FROM_STR(sip_address),
      NUTAG_URL(local_url),
      /* TAG_IF(local_url && local_url->url_type == url_sips,
             NUTAG_SIPS_URL(local_url)), */
      NUTAG_M_USERNAME(priv->account_url->url_user),
      NUTAG_USER_AGENT(rakia_version_string ()),
      NUTAG_ENABLEMESSAGE(1),
      NUTAG_ENABLEINVITE(1),
      NUTAG_AUTOALERT(0),
      NUTAG_AUTOANSWER(0),
      NUTAG_APPL_METHOD("MESSAGE"),
      SIPTAG_ALLOW_STR("INVITE, ACK, BYE, CANCEL, OPTIONS, PRACK, MESSAGE, UPDATE"),
      TAG_IF(!priv->ignore_tls_errors,
             TPTAG_TLS_VERIFY_POLICY(TPTLS_VERIFY_ALL)),
      TAG_NULL());
  if (priv->sofia_nua == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Unable to create SIP stack");
      return FALSE;
    }

  /* Set configuration-dependent tags */
  rakia_conn_update_proxy_and_transport (self);
  rakia_conn_update_nua_outbound (self);
  rakia_conn_update_nua_keepalive_interval (self);
  rakia_conn_update_nua_contact_features (self);

  if (priv->discover_stun)
    rakia_conn_discover_stun_server (self);
  else if (priv->stun_host != NULL)
    rakia_conn_resolv_stun_server (self, priv->stun_host);

  DEBUG("initialized a Sofia-SIP NUA at address %p", priv->sofia_nua);

  /* for debugging purposes, request a dump of stack configuration
   * at registration time */
  nua_get_params (priv->sofia_nua, TAG_ANY(), TAG_NULL());

  g_signal_connect (self,
                    "nua-event::nua_r_register",
                    G_CALLBACK (rakia_connection_nua_r_register_cb),
                    NULL);

  priv->register_op = rakia_conn_create_register_handle (self, self_handle);
  if (priv->register_op == NULL)
    {
      g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Unable to create registration handle for address %s", sip_address);
      return FALSE;
    }

  rakia_event_target_attach (priv->register_op, (GObject *) self);

  nua_register (priv->register_op, TAG_NULL());

  return TRUE;
}


/**
 * rakia_connection_disconnected
 *
 * Called after the connection becomes disconnected.
 */
static void
rakia_connection_disconnected (TpBaseConnection *base)
{
  RakiaConnection *obj = RAKIA_CONNECTION (base);
  RakiaConnectionPrivate *priv;

  priv = RAKIA_CONNECTION_GET_PRIVATE (obj);

  DEBUG("enter");

  /* Dispose of the register use */
  if (priv->register_op != NULL)
    {
      DEBUG("unregistering");
      nua_unregister (priv->register_op, TAG_NULL());
      nua_handle_unref (priv->register_op);
      priv->register_op = NULL;
    }

  /* we know that RakiaBaseConnection does implement this */
  TP_BASE_CONNECTION_CLASS (rakia_connection_parent_class)->disconnected (base);
}
