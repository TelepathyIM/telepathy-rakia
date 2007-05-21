/*
 * sip-connection.c - Source for SIPConnection
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection).
 *   @author See gabble-connection.c
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
#include <dbus/dbus-glib-lowlevel.h>

#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/svc-connection.h>

#include "media-factory.h"
#include "text-factory.h"
#include "sip-connection.h"

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"

static void conn_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(SIPConnection, sip_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION,
      conn_iface_init))

#include "sip-connection-enumtypes.h"
#include "sip-connection-helpers.h"
#include "sip-connection-private.h"
#include "sip-connection-sofia.h"

#define ERROR_IF_NOT_CONNECTED_ASYNC(BASE, CONTEXT) \
  if ((BASE)->status != TP_CONNECTION_STATUS_CONNECTED) \
    { \
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, \
          "Connection is disconnected" }; \
      DEBUG ("rejected request as disconnected"); \
      dbus_g_method_return_error ((CONTEXT), &e); \
      return; \
    }

static GObjectClass *parent_class=NULL;


/* properties */
enum
{
  PROP_ADDRESS = 1,      /**< public SIP address (SIP URI) */
  PROP_PASSWORD,         /**< account password (for registration) */

  PROP_PROXY,            /**< outbound SIP proxy (SIP URI) */
  PROP_REGISTRAR,        /**< SIP registrar (SIP URI) */

  PROP_KEEPALIVE_MECHANISM, /**< keepalive mechanism as defined by SIPConnectionKeepaliveMechanism */
  PROP_KEEPALIVE_INTERVAL, /**< keepalive interval in seconds */
  PROP_HTTP_PROXY,         /**< HTTP proxy URI; use HTTP-CONNECT to reach SIP servers */
  PROP_DISCOVER_BINDING,   /**< enable discovery of public binding */
  PROP_STUN_SERVER,        /**< STUN server address (if not set, derived
			        from public SIP address */
  PROP_STUN_PORT,          /**< STUN port */
  PROP_EXTRA_AUTH_USER,	   /**< User name to use for extra authentication challenges */
  PROP_EXTRA_AUTH_PASSWORD,/**< Password to use for extra authentication challenges */
  PROP_SOFIA_ROOT,         /**< Event root pointer from the Sofia-SIP stack */
  LAST_PROPERTY
};

/**
 * Returns a duplicated char array of a *user-provided* SIP 
 * URI, which might be missing the URI prefix, or some other 
 * required components.
 *
 * @return NULL if a valid URI cannot be created from 'input'
 */
static gchar *priv_sip_strdup(const gchar *input)
{
  if (input == NULL)
    return NULL;

  if (strncmp("sip:", input, 4) == 0 ||
      strncmp("sips:", input, 5) == 0)
    return g_strdup (input);

  return g_strdup_printf ("sip:%s", input);
}

static GObject *
sip_connection_constructor (GType                  type,
                            guint                  n_construct_properties,
                            GObjectConstructParam *construct_properties)
{
  GObject *obj;

  {
    /* Invoke parent constructor.
     * this calls our init, and then set_property with any
     * CONSTRUCT params
     */
    SIPConnectionClass *klass;
    klass = SIP_CONNECTION_CLASS (g_type_class_peek (SIP_TYPE_CONNECTION));
    parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
    obj = parent_class->constructor (type,
                                     n_construct_properties,
                                     construct_properties);
  }

  SIPConnection *self = SIP_CONNECTION (obj);
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self); 

  /* step: create home objects */
  priv->sofia_home = su_home_new(sizeof (su_home_t));

  /* the non-construct parameters will be empty */
  g_assert (priv->registrar == NULL);
  g_assert (priv->proxy == NULL);
  g_assert (priv->http_proxy == NULL);
  g_assert (priv->stun_server == NULL);

  g_assert (priv->sofia_nua == NULL);

  g_message ("SIPConnection constructed at %p", obj);
  return obj;
}

/* keep these two in sync */
enum
{
  LIST_HANDLE_PUBLISH = 1,
  LIST_HANDLE_SUBSCRIBE,
  LIST_HANDLE_KNOWN,
};
static const char *list_handle_strings[] = 
{
    "publish",    /* LIST_HANDLE_PUBLISH */
    "subscribe",  /* LIST_HANDLE_SUBSCRIBE */
    "known",      /* LIST_HANDLE_KNOWN */
    NULL
};

static gchar *normalize_sipuri (TpHandleRepoIface *repo, const gchar *sipuri,
    gpointer context, GError **error);

static void
sip_create_handle_repos (TpBaseConnection *conn,
                         TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
  repos[TP_HANDLE_TYPE_CONTACT] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_DYNAMIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_CONTACT,
          "normalize-function", normalize_sipuri,
          NULL);
  repos[TP_HANDLE_TYPE_LIST] =
      (TpHandleRepoIface *)g_object_new (TP_TYPE_STATIC_HANDLE_REPO,
          "handle-type", TP_HANDLE_TYPE_LIST,
          "handle-names", list_handle_strings, NULL);
}

static GPtrArray *
sip_connection_create_channel_factories (TpBaseConnection *base)
{
  SIPConnection *self = SIP_CONNECTION (base);
  SIPConnectionPrivate *priv;
  GPtrArray *factories = g_ptr_array_sized_new (2);

  g_assert (SIP_IS_CONNECTION (self));
  priv = SIP_CONNECTION_GET_PRIVATE (self);

  priv->text_factory = (TpChannelFactoryIface *)g_object_new (
      SIP_TYPE_TEXT_FACTORY, "connection", self, NULL);
  g_ptr_array_add (factories, priv->text_factory);

  priv->media_factory = (TpChannelFactoryIface *)g_object_new (
      SIP_TYPE_MEDIA_FACTORY, "connection", self, NULL);
  g_ptr_array_add (factories, priv->media_factory);

  return factories;
}

static void
sip_connection_init (SIPConnection *obj)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (obj);
  priv->sofia = sip_connection_sofia_new (obj);
}

static void
sip_connection_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  SIPConnection *self = (SIPConnection*) object;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
  case PROP_ADDRESS: {
    /* just store the address, self_handle set in start_connecting */
    priv->address = priv_sip_strdup (g_value_get_string (value));
    break;
  }
  case PROP_PASSWORD: {
    g_free(priv->password);
    priv->password = g_value_dup_string (value);
    break;
  }
  case PROP_PROXY: {
    g_free(priv->proxy);
    priv->proxy = priv_sip_strdup (g_value_get_string (value));
    if (priv->sofia_nua) 
      nua_set_params(priv->sofia_nua, NUTAG_PROXY(priv->proxy), TAG_END());
    break;
  }
  case PROP_REGISTRAR: {
    g_free((gpointer)priv->registrar);
    priv->registrar = priv_sip_strdup(g_value_get_string (value));
    if (priv->sofia_nua) 
      nua_set_params(priv->sofia_nua, NUTAG_REGISTRAR(priv->registrar), TAG_END());
    break;
  }
  case PROP_KEEPALIVE_MECHANISM: {
    priv->keepalive_mechanism = g_value_get_enum (value);
    if (priv->sofia_nua) {
      sip_conn_update_nua_outbound (self);
      sip_conn_update_nua_keepalive_interval (self);
    }
    break;
  }
  case PROP_KEEPALIVE_INTERVAL: {
    priv->keepalive_interval = g_value_get_int (value);
    if (priv->sofia_nua
	&& priv->keepalive_mechanism != SIP_CONNECTION_KEEPALIVE_NONE) {
      sip_conn_update_nua_keepalive_interval(self);
    }
    break;
  }
  case PROP_HTTP_PROXY: {
    g_free((gpointer)priv->http_proxy);
    priv->http_proxy = g_value_dup_string (value);
    if (priv->sofia_nua) 
      nua_set_params(priv->sofia_nua, TPTAG_HTTP_CONNECT(priv->http_proxy), TAG_END());
    break;
  }
  case PROP_DISCOVER_BINDING: {
    priv->discover_binding = g_value_get_boolean (value);
    if (priv->sofia_nua)
      sip_conn_update_nua_outbound (self);
    break;
  }
  case PROP_STUN_PORT: {
    priv->stun_port = g_value_get_uint (value);
    sip_conn_update_stun_server (self);
    break;
  }
  case PROP_STUN_SERVER: {
    sip_conn_resolv_stun_server (self, g_value_get_string (value));
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
    g_return_if_fail (priv->sofia != NULL);
    priv->sofia->sofia_root = g_value_get_pointer (value);
    break;
  }
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void
sip_connection_get_property (GObject      *object,
                             guint         property_id,
                             GValue       *value,
                             GParamSpec   *pspec)
{
  SIPConnection *self = (SIPConnection *) object;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  switch (property_id) {
  case PROP_ADDRESS: {
    g_value_set_string (value, priv->address);
    break;
  }
  case PROP_PASSWORD: {
    g_value_set_string (value, priv->password);
    break;
  }
  case PROP_PROXY: {
    g_value_set_string (value, priv->proxy);
    break;
  }
  case PROP_REGISTRAR: {
    g_value_set_string (value, priv->registrar);
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
  case PROP_HTTP_PROXY: {
    g_value_set_string (value, priv->http_proxy);
    break;
  }
  case PROP_DISCOVER_BINDING: {
    g_value_set_boolean (value, priv->discover_binding);
    break;
  }
  case PROP_STUN_SERVER: {
    g_value_set_string (value, priv->stun_server);
    break;
  }
  case PROP_STUN_PORT: {
    g_value_set_uint (value, priv->stun_port);
    break;
  }
  case PROP_SOFIA_ROOT: {
    g_return_if_fail (priv->sofia != NULL);
    g_value_set_pointer (value, priv->sofia->sofia_root);
    break;
  }
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object,property_id,pspec);
    break;
  }
}

static void sip_connection_dispose (GObject *object);
static void sip_connection_finalize (GObject *object);

static gchar *
sip_connection_unique_name (TpBaseConnection *base)
{
  SIPConnection *conn = SIP_CONNECTION (base);
  SIPConnectionPrivate *priv;

  g_assert (SIP_IS_CONNECTION (conn));
  priv = SIP_CONNECTION_GET_PRIVATE (conn);
  return g_strdup (priv->address);
}

static void sip_connection_disconnected (TpBaseConnection *base);
static void sip_connection_shut_down (TpBaseConnection *base);
static gboolean sip_connection_start_connecting (TpBaseConnection *base,
    GError **error);

static void
sip_connection_class_init (SIPConnectionClass *sip_connection_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_connection_class);
  TpBaseConnectionClass *base_class =
    (TpBaseConnectionClass *)sip_connection_class;
  GParamSpec *param_spec;

#define INST_PROP(x) \
  g_object_class_install_property (object_class,  x, param_spec)

  /* Implement pure-virtual methods */
  base_class->create_handle_repos = sip_create_handle_repos;
  base_class->get_unique_connection_name = sip_connection_unique_name;
  base_class->create_channel_factories =
    sip_connection_create_channel_factories;
  base_class->disconnected = sip_connection_disconnected;
  base_class->start_connecting = sip_connection_start_connecting;
  base_class->shut_down = sip_connection_shut_down;

  g_type_class_add_private (sip_connection_class, sizeof (SIPConnectionPrivate));

  object_class->constructor = sip_connection_constructor;

  object_class->dispose = sip_connection_dispose;
  object_class->finalize = sip_connection_finalize;

  object_class->set_property = sip_connection_set_property;
  object_class->get_property = sip_connection_get_property;

  param_spec = g_param_spec_pointer("sofia-root",
                                    "Sofia root",
                                    "Event root from Sofia-SIP stack",
                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  INST_PROP(PROP_SOFIA_ROOT);

  param_spec = g_param_spec_string("address",
                                   "SIPConnection construction property",
                                   "Public SIP address",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  INST_PROP(PROP_ADDRESS);

  param_spec = g_param_spec_string("password",
                                   "SIP account password",
                                   "Password for SIP registration",
                                   "", /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_PASSWORD);

  param_spec = g_param_spec_string("proxy",
                                   "Outbound proxy",
                                   "SIP URI for outbound proxy (e.g. 'sip:sipproxy.myprovider.com') [optional]",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_PROXY);

  param_spec = g_param_spec_string("registrar",
                                   "Registrar",
                                   "SIP URI for registrar (e.g. 'sip:sip.myprovider.com') [optional]",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_REGISTRAR);

  param_spec = g_param_spec_enum ("keepalive-mechanism",
                                  "Keepalive mechanism",
                                  "SIP registration keepalive mechanism",
                                  sip_connection_keepalive_mechanism_get_type (),
                                  SIP_CONNECTION_KEEPALIVE_AUTO,
                                  G_PARAM_READWRITE);
  INST_PROP(PROP_KEEPALIVE_MECHANISM);

  param_spec = g_param_spec_int("keepalive-interval", 
				"Keepalive interval",
				"Interval between keepalives in seconds (0 = disable, -1 = let stack decide.",
				-1, G_MAXINT32, -1,
				G_PARAM_READWRITE);
  INST_PROP(PROP_KEEPALIVE_INTERVAL);

  param_spec = g_param_spec_string("http-proxy",
                                   "HTTP proxy URI",
                                   "Use HTTP-CONNECT to reach the SIP servers, empty to disable [optional]",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_HTTP_PROXY);

  param_spec = g_param_spec_boolean("discover-binding",
                                    "Discover public contact",
                                    "Enable discovery of public IP address beyond NAT",
                                    TRUE, /*default value*/
                                    G_PARAM_READWRITE);
  INST_PROP(PROP_DISCOVER_BINDING);

  param_spec = g_param_spec_string("extra-auth-user",
                                   "Extra auth username",
                                   "Username to use for extra authentication challenges",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_EXTRA_AUTH_USER);

  param_spec = g_param_spec_string("extra-auth-password",
                                   "Extra auth password",
                                   "Password to use for extra authentication challenges",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_EXTRA_AUTH_PASSWORD);

  param_spec = g_param_spec_string("stun-server",
                                   "STUN server address",
                                   "STUN server address (FQDN or IP address, "
                                   "e.g. 'stun.myprovider.com') [optional]",
                                   NULL, /*default value*/
                                   G_PARAM_READWRITE);
  INST_PROP(PROP_STUN_SERVER);

  param_spec = g_param_spec_uint ("stun-port",
                                  "STUN port",
                                  "STUN port.",
                                  0, G_MAXUINT16, SIP_DEFAULT_STUN_PORT,
                                  G_PARAM_CONSTRUCT |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  INST_PROP(PROP_STUN_PORT);
}

static void
sip_connection_shut_down (TpBaseConnection *base)
{
  SIPConnection *self = SIP_CONNECTION (base);
  SIPConnectionPrivate *priv;

  DEBUG ("enter");

  g_assert (SIP_IS_CONNECTION (self));
  priv = SIP_CONNECTION_GET_PRIVATE (self);

  /* this shouldn't happen - shut_down is called when we change
   * state to DISCONNECTED, and that should never happen unless we've
   * started connecting
   */
  g_return_if_fail (priv->sofia_nua != NULL);

  /* We disposed of the REGISTER handle in the disconnected method */
  g_assert (priv->register_op == NULL);

  g_assert (priv->sofia != NULL);

  /* Detach the Sofia adapter and let it destroy the NUA handle and itself
   * in the shutdown callback */
  priv->sofia->conn = NULL;
  priv->sofia = NULL;

  nua_shutdown (priv->sofia_nua);
  priv->sofia_nua = NULL;

  tp_base_connection_finish_shutdown (base);
}

void
sip_connection_dispose (GObject *object)
{
  SIPConnection *self = SIP_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *)self;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  DEBUG ("Disposing of SIPConnection %p", self);

  /* these are borrowed refs, the real ones are owned by the superclass */
  priv->media_factory = NULL;
  priv->text_factory = NULL;

  /* the base class is responsible for unreffing the self handle when we
   * disconnect */
  g_assert (base->status == TP_CONNECTION_STATUS_DISCONNECTED
      || base->status == TP_INTERNAL_CONNECTION_STATUS_NEW);
  g_assert (base->self_handle == 0);

  if (G_OBJECT_CLASS (sip_connection_parent_class)->dispose)
    G_OBJECT_CLASS (sip_connection_parent_class)->dispose (object);
}

void
sip_connection_finalize (GObject *obj)
{
  SIPConnection *self = SIP_CONNECTION (obj);
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  DEBUG("enter");

  if (NULL != priv->sofia_resolver)
    {
      g_debug ("destroying sofia resolver");
      sres_resolver_destroy (priv->sofia_resolver);
      priv->sofia_resolver = NULL;
    }

  su_home_unref (priv->sofia_home);

  g_free (priv->address);
  g_free (priv->proxy);
  g_free (priv->registrar);
  g_free (priv->http_proxy);
  g_free (priv->stun_server);
  g_free (priv->extra_auth_user);
  g_free (priv->extra_auth_password);

  g_free (priv->registrar_realm);
  g_free (priv->last_auth);

  G_OBJECT_CLASS (sip_connection_parent_class)->finalize (obj);
}


/**
 * sip_connection_connect
 *
 * Implements DBus method Connect
 * on interface org.freedesktop.Telepathy.Connection
 */
static gboolean
sip_connection_start_connecting (TpBaseConnection *base,
                                 GError **error)
{
  SIPConnection *self = SIP_CONNECTION (base);
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  su_root_t *sofia_root;
  TpHandleRepoIface *contact_repo;
  const gchar *sip_address;

  g_message("%s: Connection %p ref-count=%u (obj)", G_STRFUNC, self,
      G_OBJECT(self)->ref_count);

  g_assert (base->status == TP_INTERNAL_CONNECTION_STATUS_NEW);

  g_assert (priv->sofia != NULL);

  /* the construct parameters will be non-empty */
  sofia_root = priv->sofia->sofia_root;
  g_assert (sofia_root != NULL);
  g_return_val_if_fail (priv->address != NULL, FALSE);

  /* FIXME: we should defer setting the self handle until we've found out from
   * the stack what handle we actually got, at which point we set it; and
   * not tell Telepathy that connection has succeeded until we've done so
   */
  contact_repo = tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
  base->self_handle = tp_handle_ensure (contact_repo, priv->address,
      NULL, error);
  if (base->self_handle == 0)
    {
      g_warning ("our requested SIP address '%s' is invalid, not connecting",
          priv->address);
      return FALSE;
    }
  DEBUG("self_handle = %d", base->self_handle);

  sip_address = tp_handle_inspect(contact_repo, base->self_handle);

  /* step: create stack instance */
  priv->sofia_nua = nua_create (sofia_root,
      sip_connection_sofia_callback,
      priv->sofia,
      SOATAG_AF(SOA_AF_IP4_IP6),
      SIPTAG_FROM_STR(sip_address),
      NUTAG_USER_AGENT("Telepathy-SofiaSIP/" TELEPATHY_SIP_VERSION),
      NUTAG_ENABLEMESSAGE(1),
      NUTAG_ENABLEINVITE(1),
      NUTAG_AUTOALERT(0),
      NUTAG_AUTOANSWER(0),
      TAG_NULL());
  if (priv->sofia_nua == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Unable to create SIP stack");
      return FALSE;
    }

  /* Take care about SIPS */
  if (priv->proxy != NULL)
    {
      nua_set_params (priv->sofia_nua,
                      NUTAG_PROXY(priv->proxy),
                      TAG_IF(g_ascii_strncasecmp(priv->proxy, "sips:", 5) == 0,
                                                 NUTAG_SIPS_URL("sips:*")),
                      TAG_NULL());
    }
  else if (g_ascii_strncasecmp(sip_address, "sips:", 5) == 0)
    {
      nua_set_params (priv->sofia_nua,
                      NUTAG_SIPS_URL("sips:*"),
                      TAG_NULL());
    }

  sip_conn_update_nua_outbound (self);
  sip_conn_update_nua_keepalive_interval (self);
  sip_conn_update_nua_contact_features (self);
  sip_conn_update_stun_server (self);

  g_message ("Sofia-SIP NUA at address %p (SIP URI: %s)", 
	     priv->sofia_nua, sip_address);

  /* XXX: should there be configuration option to disable use
   *      of outbound proxy, any use-cases? */

  /* for debugging purposes, request a dump of stack configuration
   * at registration time */
  nua_get_params(priv->sofia_nua, TAG_ANY(), TAG_NULL());

  priv->register_op = sip_conn_create_register_handle (self,
                                                       base->self_handle);
  nua_register(priv->register_op, TAG_NULL());

  DEBUG("exit");

  return TRUE;
}


/**
 * sip_connection_disconnected
 *
 * Called after the connection becomes disconnected.
 */
static void
sip_connection_disconnected (TpBaseConnection *base)
{
  SIPConnection *obj = SIP_CONNECTION (base);
  SIPConnectionPrivate *priv;

  g_assert (SIP_IS_CONNECTION (obj));
  priv = SIP_CONNECTION_GET_PRIVATE (obj);

  DEBUG("enter");

  /* Dispose of the register use */
  if (priv->register_op != NULL)
    {
      if (priv->register_succeeded)
        {
          DEBUG("unregistering");
          nua_unregister (priv->register_op, TAG_NULL());
          nua_handle_unref (priv->register_op);
        }
      else
        {
          DEBUG("don't let the incomplete registration linger any longer");
          nua_handle_destroy (priv->register_op);
        }

      priv->register_op = NULL;
    }
}

/**
 * sip_connection_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Connection
 */
static void
sip_connection_get_interfaces (TpSvcConnection *iface,
                               DBusGMethodInvocation *context)
{
  SIPConnection *self = SIP_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)self;
  const char *interfaces[] = {
      TP_IFACE_PROPERTIES_INTERFACE,
      NULL };

  DEBUG ("called");

  ERROR_IF_NOT_CONNECTED_ASYNC (base, context)
  tp_svc_connection_return_from_get_interfaces (context, interfaces);
}


static gchar *
normalize_sipuri (TpHandleRepoIface *repo,
                  const gchar *sipuri,
                  gpointer context,
                  GError **error)
{
  /* FIXME:
   * - guess whether it's a phone number or a SIP URI
   * - prepend sip: etc.
   * - perform case normalization etc.
   */

  if (strchr (sipuri, ':') == NULL)
    {
      g_debug ("%s has no ':', assuming user meant sip:%s", sipuri, sipuri);
      return g_strdup_printf ("sip:%s", sipuri);
    }

  if (strncmp (sipuri, "sip:", 4) &&
      strncmp (sipuri, "sips:", 5) &&
      strncmp (sipuri, "tel:", 4))
    {
      g_debug ("%s: not a valid sip/sips/tel URI (%s)", G_STRFUNC, sipuri);

      if (error)
        *error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                              "invalid SIP URI");

      return NULL;
    }

  return g_strdup (sipuri);
}


/**
 * sip_connection_request_handles
 *
 * Implements DBus method RequestHandles
 * on interface org.freedesktop.Telepathy.Connection
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
sip_connection_request_handles (TpSvcConnection *iface,
                                guint handle_type,
                                const gchar **names,
                                DBusGMethodInvocation *context)
{
  SIPConnection *obj = SIP_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *)obj;
  gint count = 0;
  gint i;
  const gchar **h;
  GArray *handles;
  GError *error = NULL;
  const gchar *client_name;
  SIPConnectionPrivate *priv;
  TpHandleRepoIface *repo = tp_base_connection_get_handles (base, handle_type);

  priv = SIP_CONNECTION_GET_PRIVATE (obj);

  DEBUG("enter");

  ERROR_IF_NOT_CONNECTED_ASYNC (base, context)

  if (!tp_handle_type_is_valid (handle_type, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }
  
  if (repo == NULL)
    {
      tp_g_set_error_unsupported_handle_type (handle_type, &error);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  for (h = names; *h != NULL; h++)
    ++count;

  handles = g_array_sized_new(FALSE, FALSE, sizeof(guint), count);

  client_name = dbus_g_method_get_sender (context);

  for (i = 0; i < count; i++) {
    TpHandle handle;

    handle = tp_handle_ensure (repo, names[i], NULL, &error);

    if (handle == 0)
      {
        DEBUG("requested handle %s was invalid", names[i]);
        goto ERROR_IN_LOOP;
      }

    DEBUG("verify handle '%s' => %u (%s)", names[i],
        handle, tp_handle_inspect (repo, handle));

    if (!tp_handle_client_hold (repo, client_name, handle, &error))
      {
        /* oops */
        tp_handle_unref (repo, handle);
        goto ERROR_IN_LOOP;
      }

    /* now the client owns the handle, so we can drop our reference */
    tp_handle_unref (repo, handle);

    g_array_append_val(handles, handle);
    continue;

ERROR_IN_LOOP:
    for (; i >= 0; --i)
      {
        tp_handle_client_release (repo, client_name,
            (TpHandle) g_array_index (handles, guint, i),
            NULL);
      }

    dbus_g_method_return_error (context, error);
    g_error_free (error);
    g_array_free (handles, TRUE);
    return;
  }

  tp_svc_connection_return_from_request_handles (context, handles);
  g_array_free (handles, TRUE);
}

static void
conn_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionClass *klass = (TpSvcConnectionClass *)g_iface;

#define IMPLEMENT(x) tp_svc_connection_implement_##x (klass,\
    sip_connection_##x)
  IMPLEMENT(get_interfaces);
  IMPLEMENT(request_handles);
#undef IMPLEMENT
}
