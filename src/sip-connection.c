/*
 * sip-connection.c - Source for TpsipConnection
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include <tpsip/event-target.h>

#include "sip-connection.h"
#include "media-factory.h"
#include "text-factory.h"

#include "conn-aliasing.h"

#include "sip-connection-enumtypes.h"
#include "sip-connection-helpers.h"
#include "sip-connection-private.h"
#include "sip-connection-sofia.h"

#include <sofia-sip/msg_header.h>
#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "debug.h"

static void event_target_iface_init (gpointer, gpointer);
static void conn_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(TpsipConnection, tpsip_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, event_target_iface_init);
    G_IMPLEMENT_INTERFACE(TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
        tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        tpsip_conn_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION, conn_iface_init))

#define ERROR_IF_NOT_CONNECTED_ASYNC(BASE, CONTEXT) \
  if ((BASE)->status != TP_CONNECTION_STATUS_CONNECTED) \
    { \
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
          "Connection is disconnected" }; \
      DEBUG ("rejected request as disconnected"); \
      dbus_g_method_return_error ((CONTEXT), &e); \
      return; \
    }


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
  PROP_KEEPALIVE_MECHANISM, /**< keepalive mechanism as defined by TpsipConnectionKeepaliveMechanism */
  PROP_KEEPALIVE_INTERVAL, /**< keepalive interval in seconds */
  PROP_DISCOVER_BINDING,   /**< enable discovery of public binding */
  PROP_DISCOVER_STUN,      /**< Discover STUN server name using DNS SRV lookup */
  PROP_STUN_SERVER,        /**< STUN server address (if not set, derived
			        from public SIP address */
  PROP_STUN_PORT,          /**< STUN port */
  PROP_LOCAL_IP_ADDRESS,   /**< Local IP address (normally not needed, chosen by stack) */
  PROP_LOCAL_PORT,         /**< Local port for SIP (normally not needed, chosen by stack) */
  PROP_EXTRA_AUTH_USER,	   /**< User name to use for extra authentication challenges */
  PROP_EXTRA_AUTH_PASSWORD,/**< Password to use for extra authentication challenges */
  PROP_SOFIA_ROOT,         /**< Event root pointer from the Sofia-SIP stack */
  LAST_PROPERTY
};


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

static TpHandle
priv_handle_parse_from (const sip_t *sip,
                        TpHandleRepoIface *contact_repo)
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

static void
tpsip_create_handle_repos (TpBaseConnection *conn,
                           TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_CONTACT,
          "normalize-function", tpsip_handle_normalize,
          "default-normalize-context", conn,
          NULL);
}

static GPtrArray *
tpsip_connection_create_channel_factories (TpBaseConnection *base)
{
  TpsipConnection *self = TPSIP_CONNECTION (base);
  TpsipConnectionPrivate *priv;
  GPtrArray *factories = g_ptr_array_sized_new (2);

  g_assert (TPSIP_IS_CONNECTION (self));
  priv = TPSIP_CONNECTION_GET_PRIVATE (self);

  priv->text_factory = (TpChannelFactoryIface *)g_object_new (
      TPSIP_TYPE_TEXT_FACTORY, "connection", self, NULL);
  g_ptr_array_add (factories, priv->text_factory);

  priv->media_factory = (TpChannelFactoryIface *)g_object_new (
      TPSIP_TYPE_MEDIA_FACTORY, "connection", self, NULL);
  g_ptr_array_add (factories, priv->media_factory);

  return factories;
}

static void
tpsip_connection_init (TpsipConnection *self)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

  priv->sofia_home = su_home_new(sizeof (su_home_t));
  priv->auth_table = g_hash_table_new_full (g_direct_hash,
                                            g_direct_equal,
                                            NULL /* (GDestroyNotify) nua_handle_unref */,
                                            g_free);

  tp_contacts_mixin_init ((GObject *) self,
      G_STRUCT_OFFSET (TpsipConnection, contacts));

  tp_base_connection_register_with_contacts_mixin ((TpBaseConnection *) self);

  tpsip_conn_aliasing_init (self);
}

static void
tpsip_connection_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  TpsipConnection *self = (TpsipConnection*) object;
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

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
    priv->keepalive_mechanism = g_value_get_enum (value);
    if (priv->sofia_nua) {
      tpsip_conn_update_nua_outbound (self);
      tpsip_conn_update_nua_keepalive_interval (self);
    }
    break;
  }
  case PROP_KEEPALIVE_INTERVAL: {
    priv->keepalive_interval = g_value_get_int (value);
    if (priv->sofia_nua
	&& priv->keepalive_mechanism != TPSIP_CONNECTION_KEEPALIVE_NONE) {
      tpsip_conn_update_nua_keepalive_interval(self);
    }
    break;
  }
  case PROP_DISCOVER_BINDING: {
    priv->discover_binding = g_value_get_boolean (value);
    if (priv->sofia_nua)
      tpsip_conn_update_nua_outbound (self);
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
  case PROP_SOFIA_ROOT: {
    priv->sofia_root = g_value_get_pointer (value);
    break;
  }
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void
tpsip_connection_get_property (GObject      *object,
                             guint         property_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
  TpsipConnection *self = (TpsipConnection *) object;
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

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
    g_value_set_int (value, priv->keepalive_interval);
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
  case PROP_LOCAL_IP_ADDRESS: {
    g_value_set_string (value, priv->local_ip_address);
    break;
  }
  case PROP_LOCAL_PORT: {
    g_value_set_uint (value, priv->local_port);
    break;
  }
  case PROP_SOFIA_ROOT: {
    g_value_set_pointer (value, priv->sofia_root);
    break;
  }
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void tpsip_connection_dispose (GObject *object);
static void tpsip_connection_finalize (GObject *object);

static gchar *
tpsip_connection_unique_name (TpBaseConnection *base)
{
  TpsipConnection *conn = TPSIP_CONNECTION (base);
  TpsipConnectionPrivate *priv;

  g_assert (TPSIP_IS_CONNECTION (conn));
  priv = TPSIP_CONNECTION_GET_PRIVATE (conn);
  return g_strdup (priv->address);
}

static void tpsip_connection_disconnected (TpBaseConnection *base);
static void tpsip_connection_shut_down (TpBaseConnection *base);
static gboolean tpsip_connection_start_connecting (TpBaseConnection *base,
    GError **error);

static void
tpsip_connection_class_init (TpsipConnectionClass *klass)
{
  static const gchar *interfaces_always_present[] = {
      TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      NULL };

  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionClass *base_class =
    (TpBaseConnectionClass *)klass;
  GParamSpec *param_spec;

  /* Implement pure-virtual methods */
  base_class->create_handle_repos = tpsip_create_handle_repos;
  base_class->get_unique_connection_name = tpsip_connection_unique_name;
  base_class->create_channel_factories =
    tpsip_connection_create_channel_factories;
  base_class->disconnected = tpsip_connection_disconnected;
  base_class->start_connecting = tpsip_connection_start_connecting;
  base_class->shut_down = tpsip_connection_shut_down;
  base_class->interfaces_always_present = interfaces_always_present;

  g_type_class_add_private (klass, sizeof (TpsipConnectionPrivate));

  object_class->dispose = tpsip_connection_dispose;
  object_class->finalize = tpsip_connection_finalize;

  object_class->set_property = tpsip_connection_set_property;
  object_class->get_property = tpsip_connection_get_property;

#define INST_PROP(x) \
  g_object_class_install_property (object_class,  x, param_spec)

  param_spec = g_param_spec_pointer ("sofia-root", "Sofia root",
      "Event root from Sofia-SIP stack",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_SOFIA_ROOT);

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

  param_spec = g_param_spec_string ("alias", "Alias",
      "User's display name",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_ALIAS);

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
      tpsip_connection_keepalive_mechanism_get_type (),
      TPSIP_CONNECTION_KEEPALIVE_AUTO, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_KEEPALIVE_MECHANISM);

  param_spec = g_param_spec_int ("keepalive-interval", "Keepalive interval",
      "Interval between keepalives in seconds "
      "(0 = internal default, -1 = let the stack decide)",
      -1, G_MAXINT32, -1,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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
      TPSIP_DEFAULT_STUN_PORT, /*default value*/
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  INST_PROP(PROP_STUN_PORT);

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

#undef INST_PROP

  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (TpsipConnectionClass, properties_class));

  tp_contacts_mixin_class_init (object_class,
      G_STRUCT_OFFSET (TpsipConnectionClass, contacts_class));
}

static gboolean
priv_handle_auth (TpsipConnection* self,
                  int status,
                  nua_handle_t *nh,
                  const sip_t *sip,
                  gboolean home_realm)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  sip_www_authenticate_t const *wa;
  sip_proxy_authenticate_t const *pa;
  const char *method = NULL;
  const char *realm = NULL;
  const char *user =  NULL;
  const char *password =  NULL;
  gchar *auth = NULL;

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
      g_warning ("no realm presented for authentication");
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
          g_message ("registrar realm changed from %s to %s", priv->registrar_realm, realm);
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

      DEBUG("using the extra auth credentials");
    }

  if (user == NULL)
    {
      sip_from_t const *sipfrom = sip->sip_from;
      if (sipfrom && sipfrom->a_url[0].url_user)
        /* or use the userpart in "From" header */
        user = sipfrom->a_url[0].url_user;
    }

  if (password == NULL)
    password = "";

  /* step: if all info is available, create an authorization response */
  g_assert (realm != NULL);
  if (user && method) {
    if (realm[0] == '"')
      auth = g_strdup_printf ("%s:%s:%s:%s", 
                              method, realm, user, password);
    else
      auth = g_strdup_printf ("%s:\"%s\":%s:%s", 
                              method, realm, user, password);

    DEBUG("%s authenticating user='%s' realm=%s",
          wa ? "server" : "proxy", user, realm);
  }

  if (auth == NULL)
    {
      g_warning ("authentication data are incomplete");
      return FALSE;
    }

  /* step: authenticate */
  nua_authenticate(nh, NUTAG_AUTH(auth), TAG_END());

  g_free (auth);

  return TRUE;
}

static gboolean
tpsip_connection_auth_cb (TpsipEventTarget *target,
                          const TpsipNuaEvent *ev,
                          tagi_t            tags[],
                          TpsipConnection  *self)
{
  return priv_handle_auth (self,
                           ev->status,
                           ev->nua_handle,
                           ev->sip,
                           FALSE);
}

void
tpsip_connection_connect_auth_handler (TpsipConnection *self,
                                       TpsipEventTarget *target)
{
  g_signal_connect_object (target,
                           "nua-event",
                           G_CALLBACK (tpsip_connection_auth_cb),
                           self,
                           0);
}

static gboolean
tpsip_connection_nua_r_register_cb (TpsipConnection     *self,
                                    const TpsipNuaEvent *ev,
                                    tagi_t               tags[],
                                    gpointer             foo)
{
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
          DEBUG("succesfully registered to the network");
          conn_status = TP_CONNECTION_STATUS_CONNECTED;
          reason = TP_CONNECTION_STATUS_REASON_REQUESTED;
        }
    }

  tp_base_connection_change_status ((TpBaseConnection *) self,
                                    conn_status, reason);

  return TRUE;
}

static gboolean
tpsip_connection_nua_i_invite_cb (TpsipConnection   *self,
                                  const TpsipNuaEvent  *ev,
                                  tagi_t             tags[],
                                  gpointer           foo)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  TpsipMediaChannel *channel;
  TpHandleRepoIface *contact_repo;
  TpHandle handle;
  GError *error = NULL;

  /* figure out a handle for the identity */

  contact_repo = tp_base_connection_get_handles ((TpBaseConnection *)self,
                                                 TP_HANDLE_TYPE_CONTACT);

  handle = priv_handle_parse_from (ev->sip, contact_repo);
  if (!handle)
    {
      g_message ("incoming INVITE with invalid sender information");
      nua_respond (ev->nua_handle, 400, "Invalid From address", TAG_END());
      return TRUE;
    }

  DEBUG("Got incoming invite from <%s>", 
        tp_handle_inspect (contact_repo, handle));

  channel = tpsip_media_factory_new_channel (
                TPSIP_MEDIA_FACTORY (priv->media_factory),
                NULL,
                TP_HANDLE_TYPE_NONE,
                0,
                &error);
  if (channel)
    {
      tpsip_media_channel_receive_invite (channel, ev->nua_handle, handle);
    }
  else
    {
      g_warning ("creation of SIP media channel failed: %s", error->message);
      g_error_free (error);
      nua_respond (ev->nua_handle, SIP_500_INTERNAL_SERVER_ERROR, TAG_END());
    }

  tp_handle_unref (contact_repo, handle);

  return TRUE;
}

static gboolean
tpsip_connection_nua_i_message_cb (TpsipConnection   *self,
                                   const TpsipNuaEvent  *ev,
                                   tagi_t             tags[],
                                   gpointer           foo)
{
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  TpsipTextChannel *channel;
  TpHandleRepoIface *contact_repo;
  TpHandle handle;
  const sip_t *sip = ev->sip;
  char *text = NULL;

  /* Block anything else except text/plain messages (like isComposings) */
  if (sip->sip_content_type
      && (g_ascii_strcasecmp ("text/plain", sip->sip_content_type->c_type)))
    {
      nua_respond (ev->nua_handle,
                   SIP_415_UNSUPPORTED_MEDIA,
                   SIPTAG_ACCEPT_STR("text/plain"),
                   NUTAG_WITH_THIS(ev->nua),
                   TAG_END());
      goto end;
    }

  /* If there is some text, assure it's in UTF-8 encoding */
  if (sip->sip_payload && sip->sip_payload->pl_len > 0)
    {
      const char *charset = NULL;
      if (sip->sip_content_type && sip->sip_content_type->c_params != 0)
        {
          charset = msg_params_find (sip->sip_content_type->c_params, "charset=");
        }

      /* Default charset is UTF-8, we only need to convert if it's a different one */
      if (charset && g_ascii_strcasecmp (charset, "UTF-8"))
        {
          GError *error;
          gsize in_len, out_len;
          text = g_convert (sip->sip_payload->pl_data, sip->sip_payload->pl_len,
              "UTF-8", charset, &in_len, &out_len, &error);

          if (text == NULL)
            {
              gint status;
              gchar *message;

              status = (error->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE)
                       ? 400 : 500;
              message = g_strdup_printf ("Character set conversion failed"
                                                " for the message body: %s",
                                         error->message);
              nua_respond (ev->nua_handle,
                           status, message,
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());

              g_free (message);
              g_error_free (error);
              goto end;
            }
          if (in_len != sip->sip_payload->pl_len)
            {
              nua_respond (ev->nua_handle,
                           400, "Incomplete character sequence at the "
                                "end of the message body",
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());
              goto end;
            }
        }
      else
        {
          if (!g_utf8_validate (sip->sip_payload->pl_data,
                                sip->sip_payload->pl_len,
                                NULL))
            {
              nua_respond (ev->nua_handle,
                           400, "Invalid characters in the message body",
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());
              goto end;
            }
          text = g_strndup (sip->sip_payload->pl_data, sip->sip_payload->pl_len);
        }
    }
  else
    {
      text = g_strdup ("");
    }

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)self, TP_HANDLE_TYPE_CONTACT);

  handle = priv_handle_parse_from (sip, contact_repo);

  if (handle)
    {
      DEBUG("Got incoming message from <%s>", 
            tp_handle_inspect (contact_repo, handle));

      channel = tpsip_text_factory_lookup_channel (priv->text_factory, handle);

      if (!channel)
        {
          channel = tpsip_text_factory_new_channel (priv->text_factory, handle,
              NULL);
          g_assert (channel != NULL);
        }

      tpsip_text_channel_receive (channel, handle, text);

      tp_handle_unref (contact_repo, handle);

      nua_respond (ev->nua_handle,
                   SIP_200_OK,
                   NUTAG_WITH_THIS(ev->nua),
                   TAG_END());
    }
  else
    {
      nua_respond (ev->nua_handle,
                   400, "Invalid From address",
                   NUTAG_WITH_THIS(ev->nua),
                   TAG_END());
    }

end:
  g_free (text);

  return TRUE;
}

static void
tpsip_connection_shut_down (TpBaseConnection *base)
{
  TpsipConnection *self = TPSIP_CONNECTION (base);
  TpsipConnectionPrivate *priv;

  DEBUG ("enter");

  priv = TPSIP_CONNECTION_GET_PRIVATE (self);

  /* We disposed of the REGISTER handle in the disconnected method */
  g_assert (priv->register_op == NULL);

  if (priv->sofia_nua != NULL)
    nua_shutdown (priv->sofia_nua);

  priv->sofia_nua = NULL;

  tp_base_connection_finish_shutdown (base);
}

void
tpsip_connection_dispose (GObject *object)
{
  TpsipConnection *self = TPSIP_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *)self;
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  DEBUG("disposing of TpsipConnection %p", self);

  /* these are borrowed refs, the real ones are owned by the superclass */
  priv->media_factory = NULL;
  priv->text_factory = NULL;

  /* may theoretically involve NUA handle unrefs */
  g_hash_table_destroy (priv->auth_table);

  /* the base class is responsible for unreffing the self handle when we
   * disconnect */
  g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED
      || base->status == TP_INTERNAL_CONNECTION_STATUS_NEW);
  g_assert (base->self_handle == 0);

  if (G_OBJECT_CLASS (tpsip_connection_parent_class)->dispose)
    G_OBJECT_CLASS (tpsip_connection_parent_class)->dispose (object);
}

void
tpsip_connection_finalize (GObject *obj)
{
  TpsipConnection *self = TPSIP_CONNECTION (obj);
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);

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

  tp_contacts_mixin_finalize (obj);

  G_OBJECT_CLASS (tpsip_connection_parent_class)->finalize (obj);
}

static gboolean
tpsip_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
  TpsipConnection *self = TPSIP_CONNECTION (base);
  TpsipConnectionPrivate *priv = TPSIP_CONNECTION_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo;
  const gchar *sip_address;
  const url_t *local_url;

  g_assert (base->status == TP_INTERNAL_CONNECTION_STATUS_NEW);

  /* the construct parameters will be non-empty */
  g_assert (priv->sofia_root != NULL);
  g_return_val_if_fail (priv->address != NULL, FALSE);

  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  base->self_handle = tp_handle_ensure (contact_repo, priv->address,
      NULL, error);
  if (base->self_handle == 0)
    {
      return FALSE;
    }

  sip_address = tp_handle_inspect(contact_repo, base->self_handle);

  DEBUG("self_handle = %d, sip_address = %s", base->self_handle, sip_address);

  priv->account_url = tpsip_conn_get_contact_url (self, base->self_handle);
  if (priv->account_url == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Failed to create the account URI");
      return FALSE;
    }

  local_url = tpsip_conn_get_local_url (self);

  /* step: create stack instance */
  priv->sofia_nua = nua_create (priv->sofia_root,
      tpsip_connection_sofia_callback,
      self,
      SOATAG_AF(SOA_AF_IP4_IP6),
      SIPTAG_FROM_STR(sip_address),
      NUTAG_URL(local_url),
      /* TAG_IF(local_url && local_url->url_type == url_sips,
             NUTAG_SIPS_URL(local_url)), */
      NUTAG_M_USERNAME(priv->account_url->url_user),
      NUTAG_USER_AGENT("Telepathy-SofiaSIP/" TELEPATHY_SIP_VERSION),
      NUTAG_ENABLEMESSAGE(1),
      NUTAG_ENABLEINVITE(1),
      NUTAG_AUTOALERT(0),
      NUTAG_AUTOANSWER(0),
      NUTAG_APPL_METHOD("MESSAGE"),
      SIPTAG_ALLOW_STR("INVITE, ACK, BYE, CANCEL, OPTIONS, PRACK, MESSAGE, UPDATE"),
      TAG_NULL());
  if (priv->sofia_nua == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Unable to create SIP stack");
      return FALSE;
    }

  /* Set configuration-dependent tags */
  tpsip_conn_update_proxy_and_transport (self);
  tpsip_conn_update_nua_outbound (self);
  tpsip_conn_update_nua_keepalive_interval (self);
  tpsip_conn_update_nua_contact_features (self);

  if (priv->stun_host != NULL)
    tpsip_conn_resolv_stun_server (self, priv->stun_host);
  else if (priv->discover_stun)
    tpsip_conn_discover_stun_server (self);

  DEBUG("initialized a Sofia-SIP NUA at address %p", priv->sofia_nua);

  /* for debugging purposes, request a dump of stack configuration
   * at registration time */
  nua_get_params (priv->sofia_nua, TAG_ANY(), TAG_NULL());

  g_signal_connect (self,
                    "nua-event::nua_i_invite",
                    G_CALLBACK (tpsip_connection_nua_i_invite_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_message",
                    G_CALLBACK (tpsip_connection_nua_i_message_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_r_register",
                    G_CALLBACK (tpsip_connection_nua_r_register_cb),
                    NULL);

  priv->register_op = tpsip_conn_create_register_handle (self,
                                                         base->self_handle);
  if (priv->register_op == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Unable to create registration handle for address %s", sip_address);
      return FALSE;
    }

  tpsip_event_target_attach (priv->register_op, (GObject *) self);

  nua_register (priv->register_op, TAG_NULL());

  return TRUE;
}


/**
 * tpsip_connection_disconnected
 *
 * Called after the connection becomes disconnected.
 */
static void
tpsip_connection_disconnected (TpBaseConnection *base)
{
  TpsipConnection *obj = TPSIP_CONNECTION (base);
  TpsipConnectionPrivate *priv;

  priv = TPSIP_CONNECTION_GET_PRIVATE (obj);

  DEBUG("enter");

  /* Dispose of the register use */
  if (priv->register_op != NULL)
    {
      DEBUG("unregistering");
      nua_unregister (priv->register_op, TAG_NULL());
      nua_handle_unref (priv->register_op);
      priv->register_op = NULL;
    }
}

static void
conn_iface_init(gpointer g_iface, gpointer iface_data)
{
}

static void
event_target_iface_init (gpointer g_iface, gpointer iface_data)
{
}
