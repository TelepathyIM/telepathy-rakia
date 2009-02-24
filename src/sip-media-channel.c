/*
 * sip-media-channel.c - Source for TpsipMediaChannel
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-channel).
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "sip-media-channel.h"

#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>

#include <tpsip/event-target.h>

#define DEBUG_FLAG TPSIP_DEBUG_MEDIA
#include "debug.h"
#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "sip-media-session.h"

static void event_target_init (gpointer, gpointer);
static void channel_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);
static void dtmf_iface_init (gpointer, gpointer);
static void call_state_iface_init (gpointer, gpointer);
static void hold_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (TpsipMediaChannel, tpsip_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, event_target_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DTMF,
      dtmf_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CALL_STATE,
      call_state_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_HOLD,
      hold_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const gchar *tpsip_media_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
    TP_IFACE_CHANNEL_INTERFACE_DTMF,
    TP_IFACE_CHANNEL_INTERFACE_CALL_STATE,
    TP_IFACE_CHANNEL_INTERFACE_HOLD,
    TP_IFACE_PROPERTIES_INTERFACE,
    NULL
};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_INITIATOR,
  PROP_INITIATOR_ID,
  PROP_PEER,
  PROP_REQUESTED,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  /* Telepathy properties (see below too) */
  PROP_NAT_TRAVERSAL,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  LAST_PROPERTY
};

/* TP channel properties */
enum
{
  TP_PROP_NAT_TRAVERSAL = 0,
  TP_PROP_STUN_SERVER,
  TP_PROP_STUN_PORT,
  NUM_TP_PROPS
};

static const TpPropertySignature media_channel_property_signatures[NUM_TP_PROPS] =
{
    { "nat-traversal",          G_TYPE_STRING },
    { "stun-server",            G_TYPE_STRING },
    { "stun-port",              G_TYPE_UINT },
};

/* private structure */
typedef struct _TpsipMediaChannelPrivate TpsipMediaChannelPrivate;

struct _TpsipMediaChannelPrivate
{
  TpsipConnection *conn;
  TpsipMediaSession *session;
  gchar *object_path;
  TpHandle handle;
  TpHandle initiator;
  GHashTable *call_states;

  gboolean closed;
  gboolean dispose_has_run;
};

#define TPSIP_MEDIA_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_MEDIA_CHANNEL, TpsipMediaChannelPrivate))

/***********************************************************************
 * Set: Gobject interface
 ***********************************************************************/

static void
tpsip_media_channel_init (TpsipMediaChannel *self)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  /* allocate any data required by the object here */
  priv->call_states = g_hash_table_new (NULL, NULL);

  /* initialise the properties mixin *before* GObject
   * sets the construct-time properties */
  tp_properties_mixin_init (G_OBJECT (self),
      G_STRUCT_OFFSET (TpsipMediaChannel, properties));
}

static void
tpsip_media_channel_constructed (GObject *obj)
{
  TpsipMediaChannel *chan = TPSIP_MEDIA_CHANNEL (obj);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (chan);
  TpBaseConnection *conn = (TpBaseConnection *)(priv->conn);
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (tpsip_media_channel_parent_class);
  DBusGConnection *bus;
  TpHandleRepoIface *contact_repo;
  TpIntSet *set;

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);

  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  if (priv->handle != 0)
    tp_handle_ref (contact_repo, priv->handle);

  /* register object on the bus */
  bus = tp_get_bus ();

  DEBUG("registering object to dbus path=%s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  /* initialize group mixin */
  tp_group_mixin_init (obj,
                       G_STRUCT_OFFSET (TpsipMediaChannel, group),
                       contact_repo,
                       conn->self_handle);

  /* automatically add initiator to channel, but also ref them again (because
   * priv->initiator is the InitiatorHandle) */
  g_assert (priv->initiator != 0);
  tp_handle_ref (contact_repo, priv->initiator);

  set = tp_intset_new ();
  tp_intset_add (set, priv->initiator);

  tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL, 0, 0);

  tp_intset_destroy (set);

  /* Allow member adding; also, we implement the 0.17.6 properties */
  tp_group_mixin_change_flags (obj,
      TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_PROPERTIES, 0);
}

static void tpsip_media_channel_dispose (GObject *object);
static void tpsip_media_channel_finalize (GObject *object);
static void tpsip_media_channel_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec);
static void tpsip_media_channel_set_property (GObject     *object,
					    guint        property_id,
					    const GValue *value,
					    GParamSpec   *pspec);

static void priv_create_session (TpsipMediaChannel *channel,
                                 nua_handle_t *nh,
                                 TpHandle peer);
static void priv_destroy_session(TpsipMediaChannel *channel);

static void priv_outbound_call (TpsipMediaChannel *channel,
                                TpHandle peer);

static gboolean tpsip_media_channel_remove_with_reason (
                                                 GObject *iface,
                                                 TpHandle handle,
                                                 const gchar *message,
                                                 guint reason,
                                                 GError **error);

static void
tpsip_media_channel_class_init (TpsipMediaChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "InitiatorHandle", "initiator", NULL },
      { "InitiatorID", "initiator-id", NULL },
      { "Requested", "requested", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { NULL }
  };

  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  DEBUG("enter");

  g_type_class_add_private (klass, sizeof (TpsipMediaChannelPrivate));

  object_class->constructed = tpsip_media_channel_constructed;
  object_class->dispose = tpsip_media_channel_dispose;
  object_class->finalize = tpsip_media_channel_finalize;

  object_class->get_property = tpsip_media_channel_get_property;
  object_class->set_property = tpsip_media_channel_set_property;

  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_object ("connection", "TpsipConnection object",
      "SIP connection object that owns this SIP media channel object.",
      TPSIP_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("nat-traversal", "NAT traversal mechanism",
      "A string representing the type of NAT traversal that should be "
      "performed for streams on this channel.",
      "none",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL, param_spec);

  param_spec = g_param_spec_string ("stun-server", "STUN server",
      "IP or address of STUN server.", NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
      "UDP port of STUN server.", 0, G_MAXUINT16, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Addition Channel.Interface.* interfaces", G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("target-id", "Target SIP URI",
      "Currently empty, because this channel always has handle 0.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator", "Channel initiator",
      "The TpHandle representing the contact who created the channel.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Creator URI",
      "The URI obtained by inspecting the initiator handle.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  tp_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (TpsipMediaChannelClass, properties_class),
      media_channel_property_signatures, NUM_TP_PROPS, NULL);

  klass->dbus_props_class.interfaces =
      prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (TpsipMediaChannelClass, dbus_props_class));

  tp_group_mixin_class_init (object_class,
                             G_STRUCT_OFFSET (TpsipMediaChannelClass, group_class),
                             _tpsip_media_channel_add_member,
                             NULL);
  tp_group_mixin_class_set_remove_with_reason_func(object_class,
                             tpsip_media_channel_remove_with_reason);
  tp_group_mixin_init_dbus_properties (object_class);

}

static void
tpsip_media_channel_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  TpsipMediaChannel *chan = TPSIP_MEDIA_CHANNEL (object);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (chan);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, priv->handle);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, priv->handle?
          TP_HANDLE_TYPE_CONTACT : TP_HANDLE_TYPE_NONE);
      break;
    case PROP_TARGET_ID:
      if (priv->handle != 0)
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
        }
      else
        g_value_set_static_string (value, "");
      break;
    case PROP_PEER:
      {
        TpHandle peer = 0;

        if (priv->handle != 0)
          peer = priv->handle;
        else if (priv->session != NULL)
          g_object_get (priv->session,
              "peer", &peer,
              NULL);

        g_value_set_uint (value, peer);
        break;
      }
    case PROP_INITIATOR:
      g_value_set_uint (value, priv->initiator);
      break;
    case PROP_INITIATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value, tp_handle_inspect (repo, priv->initiator));
        }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, (priv->initiator == base_conn->self_handle));
      break;
    case PROP_INTERFACES:
      g_value_set_static_boxed (value, tpsip_media_channel_interfaces);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, priv->closed);
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
              TP_IFACE_CHANNEL, "ChannelType",
              TP_IFACE_CHANNEL, "TargetHandleType",
              TP_IFACE_CHANNEL, "TargetHandle",
              TP_IFACE_CHANNEL, "TargetID",
              TP_IFACE_CHANNEL, "InitiatorHandle",
              TP_IFACE_CHANNEL, "InitiatorID",
              TP_IFACE_CHANNEL, "Requested",
              TP_IFACE_CHANNEL, "Interfaces",
              NULL));
      break;
    default:
      /* Some properties live in the mixin */
      {
        const gchar *param_name;
        guint tp_property_id;
        GValue *tp_property_value;

        param_name = g_param_spec_get_name (pspec);
        if (G_LIKELY (tp_properties_mixin_has_property (object, param_name,
                        &tp_property_id)))
          {
            tp_property_value =
              chan->properties.properties[tp_property_id].value;

            if (G_LIKELY (tp_property_value != NULL))
              {
                g_value_copy (tp_property_value, value);
                return;
              }
          }
      }

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
tpsip_media_channel_set_property (GObject     *object,
				guint        property_id,
				const GValue *value,
				GParamSpec   *pspec)
{
  TpsipMediaChannel *chan = TPSIP_MEDIA_CHANNEL (object);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_HANDLE_TYPE:
    case PROP_CHANNEL_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = TPSIP_CONNECTION (g_value_dup_object (value));
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE:
      /* XXX: this property is defined as writable,
       * but don't set it after construction, mmkay? */
      /* we don't ref it here because we don't necessarily have access to the
       * contact repo yet - instead we ref it in constructed. */
      priv->handle = g_value_get_uint (value);
      break;
    case PROP_INITIATOR:
      /* similarly we can't ref this yet */
      priv->initiator = g_value_get_uint (value);
      break;
    default:
      /* some properties live in the mixin */
      {
        const gchar *param_name = g_param_spec_get_name (pspec);
        guint tp_property_id;

        if (G_LIKELY (tp_properties_mixin_has_property (object, param_name,
                        &tp_property_id)))
          {
            tp_properties_mixin_change_value (object, tp_property_id,
                value, NULL);
            tp_properties_mixin_change_flags (object, tp_property_id,
                TP_PROPERTY_FLAG_READ, 0, NULL);
            return;
          }
      }

      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
tpsip_media_channel_dispose (GObject *object)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (object);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  if (priv->dispose_has_run)
    return;

  DEBUG("enter");

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    tpsip_media_channel_close (self);

  contact_handles = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (priv->conn), TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (contact_handles, priv->initiator);
  priv->initiator = 0;

  g_object_unref (priv->conn);

  if (G_OBJECT_CLASS (tpsip_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (tpsip_media_channel_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
tpsip_media_channel_finalize (GObject *object)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (object);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  g_hash_table_destroy (priv->call_states);

  g_free (priv->object_path);

  tp_group_mixin_finalize (object);

  tp_properties_mixin_finalize (object);

  G_OBJECT_CLASS (tpsip_media_channel_parent_class)->finalize (object);

  DEBUG("exit");
}

/***********************************************************************
 * Set: Channel interface implementation (same for 0.12/0.13)
 ***********************************************************************/

/**
 * tpsip_media_channel_close_async
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_media_channel_dbus_close (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);

  tpsip_media_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

void
tpsip_media_channel_close (TpsipMediaChannel *obj)
{
  TpsipMediaChannelPrivate *priv;

  DEBUG("enter");

  g_assert (TPSIP_IS_MEDIA_CHANNEL (obj));
  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (obj);

  if (priv->closed)
    return;

  priv->closed = TRUE;

  if (priv->session) {
    tpsip_media_session_terminate (priv->session);
    g_assert (priv->session == NULL);
  }

  tp_svc_channel_emit_closed ((TpSvcChannel *)obj);

  return;
}

/**
 * tpsip_media_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_media_channel_get_channel_type (TpSvcChannel *obj,
                                    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
}


/**
 * tpsip_media_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_media_channel_get_handle (TpSvcChannel *iface,
                              DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}

/**
 * tpsip_media_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_media_channel_get_interfaces (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      tpsip_media_channel_interfaces);
}

/***********************************************************************
 * Set: Channel.Interface.MediaSignalling Telepathy-0.13 interface
 ***********************************************************************/

/**
 * tpsip_media_channel_get_session_handlers
 *
 * Implements DBus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Interface.MediaSignalling
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
tpsip_media_channel_get_session_handlers (TpSvcChannelInterfaceMediaSignalling *iface,
                                        DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GPtrArray *ret;
  GValue handler = { 0 };

  DEBUG("enter");

  g_assert (TPSIP_IS_MEDIA_CHANNEL (self));

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  ret = g_ptr_array_new ();

  if (priv->session)
    {
      GType handler_type;
      gchar *path;

      g_object_get (priv->session,
                    "object-path", &path,
                    NULL);

      handler_type = dbus_g_type_get_struct ("GValueArray",
                                             DBUS_TYPE_G_OBJECT_PATH,
                                             G_TYPE_STRING,
                                             G_TYPE_INVALID);

      g_value_init (&handler, handler_type);
      g_value_take_boxed (&handler,
                          dbus_g_type_specialized_construct (handler_type));

      dbus_g_type_struct_set (&handler,
                              0, path,
                              1, "rtp",
                              G_MAXUINT);

      g_free (path);

      g_ptr_array_add (ret, g_value_get_boxed (&handler));
    }

  tp_svc_channel_interface_media_signalling_return_from_get_session_handlers (
      context, ret);

  if (G_IS_VALUE(&handler))
    g_value_unset (&handler);

  g_ptr_array_free (ret, TRUE);
}


/***********************************************************************
 * Set: Channel.Type.StreamedMedia Telepathy-0.13 interface
 ***********************************************************************/

/**
 * tpsip_media_channel_list_streams
 *
 * Implements D-Bus method ListStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
tpsip_media_channel_list_streams (TpSvcChannelTypeStreamedMedia *iface,
                                DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GPtrArray *ret = NULL;

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  ret = g_ptr_array_new ();

  if (priv->session != NULL)
    tpsip_media_session_list_streams (priv->session, ret);

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);

  g_boxed_free (TP_ARRAY_TYPE_MEDIA_STREAM_INFO_LIST, ret);
}

/**
 * tpsip_media_channel_remove_streams
 *
 * Implements D-Bus method RemoveStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
tpsip_media_channel_remove_streams (TpSvcChannelTypeStreamedMedia *iface,
                                  const GArray *streams,
                                  DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GError *error = NULL;

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session != NULL)
    {
       tpsip_media_session_remove_streams(priv->session,
                                        streams,
                                        &error);
    }
  else
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                           "No session is available");
    }

  if (error != NULL)
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_channel_type_streamed_media_return_from_remove_streams (context);
}

/**
 * tpsip_media_channel_request_stream_direction
 *
 * Implements D-Bus method RequestStreamDirection
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
tpsip_media_channel_request_stream_direction (TpSvcChannelTypeStreamedMedia *iface,
                                            guint stream_id,
                                            guint stream_direction,
                                            DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GError *error = NULL;

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session != NULL)
    {
      tpsip_media_session_request_stream_direction (priv->session,
                                                  stream_id,
                                                  stream_direction,
                                                  &error);
    }
  else
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                           "The media session is not available");
    }

  if (error == NULL)
    {
      tp_svc_channel_type_streamed_media_return_from_request_stream_direction (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


/**
 * tpsip_media_channel_request_streams
 *
 * Implements D-Bus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
tpsip_media_channel_request_streams (TpSvcChannelTypeStreamedMedia *iface,
                                   guint contact_handle,
                                   const GArray *types,
                                   DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  GError *error = NULL;
  GPtrArray *ret = NULL;
  TpsipMediaChannelPrivate *priv;
  TpHandleRepoIface *contact_repo;

  DEBUG("enter");

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_repo, contact_handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  priv_outbound_call (self, contact_handle);

  ret = g_ptr_array_sized_new (types->len);

  if (tpsip_media_session_request_streams (priv->session, types, ret, &error))
    {
      g_assert (types->len == ret->len);
      tp_svc_channel_type_streamed_media_return_from_request_streams (context,
                                                                      ret);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }

  g_boxed_free (TP_ARRAY_TYPE_MEDIA_STREAM_INFO_LIST, ret);

  DEBUG ("exit");
}

/***********************************************************************
 * Set: sip-media-channel API towards sip-connection
 ***********************************************************************/

/**
 * Handle an incoming INVITE, normally called just after the channel
 * has been created with initiator handle of the sender.
 */
void
tpsip_media_channel_receive_invite (TpsipMediaChannel *self,
                                    nua_handle_t *nh)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);

  g_assert (priv->initiator != conn->self_handle);
  g_assert (priv->session == NULL);

  /* Start the local stream-engine; once the local
   * media are ready, reply with nua_respond() */
  priv_create_session (self, nh, priv->initiator);

  g_assert (priv->session != NULL);
  tpsip_media_session_receive_invite (priv->session);
}

static gboolean
priv_nua_i_invite_cb (TpsipMediaChannel *self,
                      const TpsipNuaEvent  *ev,
                      tagi_t             tags[],
                      gpointer           foo)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  /* nua_i_invite delivered for a bound handle means a re-INVITE */

  g_return_val_if_fail (priv->session != NULL, FALSE);

  tpsip_media_session_receive_reinvite (priv->session);

  return TRUE;
}

static void
tpsip_media_channel_peer_error (TpsipMediaChannel *self,
                                TpHandle peer,
                                guint status,
                                const char* message)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  TpIntSet *set;
  guint reason = TP_CHANNEL_GROUP_CHANGE_REASON_ERROR;

  switch (status)
    {
    case 410:
    case 604:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_INVALID_CONTACT;
      break;
    case 486:
    case 600:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_BUSY;
      break;
    case 408:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER;
      break;
    case 404:
    case 480:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE;
      break;
    case 603:
      /* No reason means roughly "rejected" */
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
      break;
    case 403:
    case 401:
    case 407:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED;
      break;
    }

  if (message == NULL || !g_utf8_validate (message, -1, NULL))
    message = "";

  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_intset_add (set, mixin->self_handle);
  tp_group_mixin_change_members ((GObject *)self, message,
      NULL, set, NULL, NULL, peer, reason);
  tp_intset_destroy (set);
}

guint
tpsip_media_channel_change_call_state (TpsipMediaChannel *self,
                                       TpHandle peer,
                                       guint flags_add,
                                       guint flags_remove)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  gpointer key = GUINT_TO_POINTER (peer);
  guint old_state;
  guint new_state;

  /* XXX: check if the peer is a member? */

  old_state = GPOINTER_TO_UINT (g_hash_table_lookup (priv->call_states, key));
  new_state = (old_state | flags_add) & ~flags_remove;

  if (new_state != old_state)
    {
      DEBUG ("setting call state %u for peer %u", new_state, peer);
      if (new_state == 0)
        g_hash_table_remove (priv->call_states, key);
      else
        g_hash_table_replace (priv->call_states, key,
                              GUINT_TO_POINTER (new_state));

      tp_svc_channel_interface_call_state_emit_call_state_changed (self,
                                                                   peer,
                                                                   new_state);
    }

  return new_state;
}

static gboolean
priv_nua_i_bye_cb (TpsipMediaChannel *self,
                   const TpsipNuaEvent  *ev,
                   tagi_t             tags[],
                   gpointer           foo)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  TpIntSet *set;
  TpHandle peer;

  g_return_val_if_fail (priv->session != NULL, FALSE);

  peer = tpsip_media_session_get_peer (priv->session);
  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_intset_add (set, mixin->self_handle);

  tp_group_mixin_change_members ((GObject *) self,
                                 "",
                                 NULL, /* add */
                                 set,  /* remove */
                                 NULL,
                                 NULL,
                                 peer,
                                 TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_intset_destroy (set);

  return TRUE;
}

static gboolean
priv_nua_i_cancel_cb (TpsipMediaChannel *self,
                      const TpsipNuaEvent  *ev,
                      tagi_t             tags[],
                      gpointer           foo)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  TpIntSet *set;
  TpHandle actor = 0;
  TpHandle peer;
  const sip_reason_t *reason;
  guint cause = 0;
  const gchar *message = NULL;

  g_return_val_if_fail (priv->session != NULL, FALSE);

  /* FIXME: implement cancellation of an incoming re-INVITE, if ever
   * found in real usage and not caused by a request timeout */

  if (ev->sip != NULL)
    for (reason = ev->sip->sip_reason;
         reason != NULL;
         reason = reason->re_next)
      {
        const char *protocol = reason->re_protocol;
        if (protocol == NULL || strcmp (protocol, "SIP") != 0)
          continue;
        if (reason->re_cause != NULL)
          {
            cause = (guint) g_ascii_strtoull (reason->re_cause, NULL, 10);
            message = reason->re_text;
            break;
          }
      }

  peer = tpsip_media_session_get_peer (priv->session);

  switch (cause)
    {
    case 200:
    case 603:
      /* The user must have acted on another branch of the forked call */
      actor = mixin->self_handle;
      break;
    default:
      actor = peer;
    }

  if (message == NULL || !g_utf8_validate (message, -1, NULL))
    message = "";

  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_intset_add (set, mixin->self_handle);

  tp_group_mixin_change_members ((GObject *) self,
                                 message,
                                 NULL, /* add */
                                 set,  /* remove */
                                 NULL,
                                 NULL,
                                 actor,
                                 TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

  tp_intset_destroy (set);

  return TRUE;
}

static gboolean
priv_nua_i_state_cb (TpsipMediaChannel *self,
                     const TpsipNuaEvent  *ev,
                     tagi_t             tags[],
                     gpointer           foo)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  const sdp_session_t *r_sdp = NULL;
  int offer_recv = 0;
  int answer_recv = 0;
  int ss_state = nua_callstate_init;
  gint status = ev->status;
  TpHandle peer;

  g_return_val_if_fail (priv->session != NULL, FALSE);

  tl_gets(tags,
          NUTAG_CALLSTATE_REF(ss_state),
          NUTAG_OFFER_RECV_REF(offer_recv),
          NUTAG_ANSWER_RECV_REF(answer_recv),
          SOATAG_REMOTE_SDP_REF(r_sdp),
          TAG_END());

  if (r_sdp)
    {
      g_return_val_if_fail (answer_recv || offer_recv, FALSE);
      if (!tpsip_media_session_set_remote_media (priv->session, r_sdp))
        {
          tpsip_media_channel_close (self);
          return TRUE;
        }
    }

  peer = tpsip_media_session_get_peer (priv->session);

  DEBUG("call with handle %p is %s", ev->nua_handle, nua_callstate_name (ss_state));

  switch ((enum nua_callstate)ss_state)
    {
    case nua_callstate_proceeding:
      if (status == 180)
        tpsip_media_channel_change_call_state (self, peer,
                TP_CHANNEL_CALL_STATE_RINGING, 0);
      else if (status == 182)
        tpsip_media_channel_change_call_state (self, peer,
                TP_CHANNEL_CALL_STATE_QUEUED, 0);
      break;

    case nua_callstate_completing:
      /* In auto-ack mode, we don't need to call nua_ack(), see NUTAG_AUTOACK() */
      break;

    case nua_callstate_ready:

      /* Clear Ringing and Queued call states when the call is established */
      tpsip_media_channel_change_call_state (self, peer, 0,
                TP_CHANNEL_CALL_STATE_RINGING |
                TP_CHANNEL_CALL_STATE_QUEUED);

      if (status < 300)
        tpsip_media_session_accept (priv->session);
      else if (status == 491)
        tpsip_media_session_resolve_glare (priv->session);
      else
        {
          /* Was something wrong with our re-INVITE? We can't cope anyway. */
          g_message ("can't handle non-fatal response %d %s", status, ev->text);
          tpsip_media_session_terminate (priv->session);
        }
      break;

    case nua_callstate_terminated:
      if (status >= 300)
        {
          tpsip_media_channel_peer_error (
                self, peer, status, ev->text);
        }
      tpsip_media_session_change_state (priv->session,
                                        TPSIP_MEDIA_SESSION_STATE_ENDED);
      break;

    default:
      break;
  }

  return TRUE;
}

static void priv_session_state_changed_cb (TpsipMediaSession *session,
					   guint old_state,
                                           guint state,
					   TpsipMediaChannel *channel)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  TpHandle self_handle;
  TpHandle peer;
  TpIntSet *set = NULL;

  DEBUG("enter");

  self_handle = mixin->self_handle;
  peer = tpsip_media_session_get_peer (session);

  switch (state)
    {
    case TPSIP_MEDIA_SESSION_STATE_INVITE_SENT:
      set = tp_intset_new ();

      g_assert (priv->initiator == self_handle);

      /* add the peer to remote pending */
      tp_intset_add (set, peer);
      tp_group_mixin_change_members ((GObject *)channel,
                                     "",
                                     NULL,    /* add */
                                     NULL,    /* remove */
                                     NULL,    /* local pending */
                                     set,     /* remote pending */
                                     self_handle,        /* actor */
                                     TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      /* update flags: allow removal and rescinding, no more adding */
      tp_group_mixin_change_flags ((GObject *)channel,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      break;

    case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
      set = tp_intset_new ();

      /* add ourself to local pending */
      tp_intset_add (set, self_handle);
      tp_group_mixin_change_members ((GObject *) channel, "",
                                     NULL,          /* add */
                                     NULL,          /* remove */
                                     set,           /* local pending */
                                     NULL,          /* remote pending */
                                     priv->initiator, /* actor */
                                     TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);

      /* No adding more members to the incoming call, removing is OK */
      tp_group_mixin_change_flags ((GObject *) channel,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      break;

    case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
      if (priv->initiator == self_handle)
        {
          if (!tp_handle_set_is_member (mixin->remote_pending, peer))
            break; /* no-op */

          set = tp_intset_new ();

          /* the peer has promoted itself to members */
          tp_intset_add (set, peer);
          tp_group_mixin_change_members ((GObject *)channel, "",
                                         set,     /* add */
                                         NULL,    /* remove */
                                         NULL,
                                         NULL,
                                         peer, 0);
        }
      else
        {
          if (!tp_handle_set_is_member (mixin->local_pending, self_handle))
            break; /* no-op */

          set = tp_intset_new ();

          /* promote ourselves to members */
          tp_intset_add (set, self_handle);
          tp_group_mixin_change_members ((GObject *)channel, "",
                                         set,     /* add */
                                         NULL,    /* remove */
                                         NULL,
                                         NULL,
                                         self_handle, 0);
        }

      /* update flags: allow removal, deny adding and rescinding */
      tp_group_mixin_change_flags ((GObject *)channel,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD
          | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);

      break;

    case TPSIP_MEDIA_SESSION_STATE_ENDED:
      set = tp_intset_new ();

      /* remove us and the peer from the member list */
      tp_intset_add (set, self_handle);
      tp_intset_add (set, peer);
      tp_group_mixin_change_members ((GObject *)channel, "",
                                     NULL,      /* add */
                                     set,       /* remove */
                                     NULL,
                                     NULL,
                                     0, 0);

      /* Close the channel; destroy the session first to avoid
       * the tpsip_media_session_terminate() path in this case */
      priv_destroy_session (channel);
      tpsip_media_channel_close (channel);
      break;
    }

  if (set != NULL)
    tp_intset_destroy (set);
}

static void
priv_connect_nua_handlers (TpsipMediaChannel *self, nua_handle_t *nh)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  tpsip_event_target_attach (nh, (GObject *) self);

  /* have the connection handle authentication, before all other
   * response callbacks */
  tpsip_connection_connect_auth_handler (priv->conn, TPSIP_EVENT_TARGET (self));

  g_signal_connect (self,
                    "nua-event::nua_i_invite",
                    G_CALLBACK (priv_nua_i_invite_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_bye",
                    G_CALLBACK (priv_nua_i_bye_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_cancel",
                    G_CALLBACK (priv_nua_i_cancel_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_state",
                    G_CALLBACK (priv_nua_i_state_cb),
                    NULL);

}

/**
 * priv_create_session:
 *
 * Creates a TpsipMediaSession object for given peer.
 **/
static void
priv_create_session (TpsipMediaChannel *channel,
                     nua_handle_t *nh,
                     TpHandle peer)
{
  TpsipMediaChannelPrivate *priv;
  TpsipMediaSession *session;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;
  gchar *object_path;
  gchar *local_ip_address = NULL;

  DEBUG("enter");

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  conn = (TpBaseConnection *)(priv->conn);
  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  g_assert (priv->session == NULL);

  /* Bind the channel object to the handle to handle NUA events */
  priv_connect_nua_handlers (channel, nh);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  DEBUG("allocating session, peer=%u", peer);

  /* The channel manages references to the peer handle for the session */
  tp_handle_ref (contact_repo, peer);

  g_object_get (priv->conn,
                "local-ip-address", &local_ip_address,
                NULL);

  session = g_object_new (TPSIP_TYPE_MEDIA_SESSION,
                          "media-channel", channel,
                          "object-path", object_path,
                          "nua-handle", nh,
                          "peer", peer,
                          "local-ip-address", local_ip_address,
                          NULL);

  g_free (local_ip_address);

  g_signal_connect_object (session,
                           "state-changed",
                           G_CALLBACK(priv_session_state_changed_cb),
                           channel,
                           0);

  priv->session = session;

  tp_svc_channel_interface_media_signalling_emit_new_session_handler (
      (TpSvcChannelInterfaceMediaSignalling *)channel, object_path, "rtp");

  g_free (object_path);

  DEBUG ("exit");
}

static void
priv_destroy_session(TpsipMediaChannel *channel)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  TpsipMediaSession *session;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;

  session = priv->session;
  if (session == NULL)
    return;

  DEBUG("enter");

  /* Release the peer handle */
  conn = (TpBaseConnection *)(priv->conn);
  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  tp_handle_unref (contact_repo, tpsip_media_session_get_peer (session));

  priv->session = NULL;
  g_object_unref (session);

  DEBUG("exit");
}

/*
 * Creates an outbound call session if a session does not exist
 */
static void
priv_outbound_call (TpsipMediaChannel *channel,
                    TpHandle peer)
{
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  nua_handle_t *nh;

  if (priv->session == NULL)
    {
      DEBUG("making outbound call - setting peer handle to %u", peer);

      nh = tpsip_conn_create_request_handle (priv->conn, peer);
      priv_create_session (channel, nh, peer);
      nua_handle_unref (nh);
    }
  else
    DEBUG("session already exists");

  g_assert (priv->session != NULL);
}

gboolean
_tpsip_media_channel_add_member (GObject *iface,
                                 TpHandle handle,
                                 const gchar *message,
                                 GError **error)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (iface);

  DEBUG("mixin->self_handle=%d, handle=%d", mixin->self_handle, handle);

  if (priv->initiator == mixin->self_handle)
    {
      TpIntSet *set;

      /* case a: an old-school outbound call
       * (we are the initiator, a new handle added with AddMembers) */

      priv_outbound_call (self, handle);

      /* Backwards compatible behavior:
       * add the peer to remote pending without waiting for the actual request
       * to be sent */
      set = tp_intset_new ();
      tp_intset_add (set, handle);
      tp_group_mixin_change_members (iface,
                                     "",
                                     NULL,    /* add */
                                     NULL,    /* remove */
                                     NULL,    /* local pending */
                                     set,     /* remote pending */
                                     mixin->self_handle,        /* actor */
                                     TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
      tp_intset_destroy (set);

      /* update flags: allow removal and rescinding, no more adding */
      tp_group_mixin_change_flags (iface,
          TP_CHANNEL_GROUP_FLAG_CAN_REMOVE | TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
          TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return TRUE;
    }
  if (priv->session &&
      handle == mixin->self_handle &&
      tp_handle_set_is_member (mixin->local_pending, handle))
    {
      /* case b: an incoming invite */
      DEBUG("accepting an incoming invite");
      g_return_val_if_fail (priv->session != NULL, FALSE);

      tpsip_media_session_accept (priv->session);

      return TRUE;
    }

  g_message ("unsupported member change requested for a media channel");

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      "handle %u cannot be added in the current state", handle);
  return FALSE;
}

static gint
tpsip_status_from_tp_reason (TpChannelGroupChangeReason reason)
{
  switch (reason)
    {
    case TP_CHANNEL_GROUP_CHANGE_REASON_NONE:
      return 603;       /* Decline */
    case TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER:
    case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
      return 480;       /* Temporarily Unavailable */
    case TP_CHANNEL_GROUP_CHANGE_REASON_BUSY:
      return 486;       /* Busy Here */
    case TP_CHANNEL_GROUP_CHANGE_REASON_PERMISSION_DENIED:
    case TP_CHANNEL_GROUP_CHANGE_REASON_BANNED:
      return 403;       /* Forbidden */
    case TP_CHANNEL_GROUP_CHANGE_REASON_INVALID_CONTACT:
      return 404;       /* Not Found */
    default:
      return 500;       /* Server Internal Error */
    }
}

static gboolean
tpsip_media_channel_remove_with_reason (GObject *obj,
                                        TpHandle handle,
                                        const gchar *message,
                                        guint reason,
                                        GError **error)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (obj);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  if (priv->initiator != mixin->self_handle &&
      handle != mixin->self_handle)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_PERMISSION_DENIED,
          "handle %u cannot be removed because you are not the initiator of the"
          " channel", handle);

      return FALSE;
    }

  if (priv->session == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "handle %u cannot be removed in the current state", handle);

      return FALSE;
    }

  if (handle == mixin->self_handle
      && tp_handle_set_is_member (mixin->local_pending, handle))
    {
      /* The user has rejected the call */
      gint status;

      status = tpsip_status_from_tp_reason (reason);

      /* XXX: raise NotAvailable if it's the wrong state? */
      tpsip_media_session_respond (priv->session, status, message);
    }
  else
    {
      /* Want to terminate the call in whatever other situation;
       * rescinding is handled by sending CANCEL */
      tpsip_media_session_terminate (priv->session);
    }

  return TRUE;
}

static void
tpsip_media_channel_get_call_states (TpSvcChannelInterfaceCallState *iface,
                                     DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  tp_svc_channel_interface_call_state_return_from_get_call_states (
        context,
        priv->call_states);
}

static void
tpsip_media_channel_get_hold_state (TpSvcChannelInterfaceHold *iface,
                                    DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpLocalHoldState hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
  TpLocalHoldStateReason hold_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;

  if (priv->session == NULL)
    {
      GError e = {TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "The media session is not available"};
      dbus_g_method_return_error (context, &e);
    }

  g_object_get (priv->session,
                "hold-state", &hold_state,
                "hold-state-reason", &hold_reason,
                NULL);

  tp_svc_channel_interface_hold_return_from_get_hold_state (context,
                                                            hold_state,
                                                            hold_reason);
}

static void
tpsip_media_channel_request_hold (TpSvcChannelInterfaceHold *iface,
                                  gboolean hold,
                                  DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session != NULL)
    {
      tpsip_media_session_request_hold (priv->session, hold);
    }
  else
    {
      GError e = {TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "The media session is not available"};
      dbus_g_method_return_error (context, &e);
    }

  tp_svc_channel_interface_hold_return_from_request_hold (context);
}

static void
tpsip_media_channel_start_tone (TpSvcChannelInterfaceDTMF *iface,
                              guint stream_id,
                              guchar event,
                              DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GError *error = NULL;

  DEBUG("enter");

  g_assert (TPSIP_IS_MEDIA_CHANNEL (self));

  if (event >= NUM_TP_DTMF_EVENTS)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "event %u is not a known DTMF event", event);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!tpsip_media_session_start_telephony_event (priv->session,
                                                  stream_id,
                                                  event,
                                                  &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_channel_interface_dtmf_return_from_start_tone (context);
}

static void
tpsip_media_channel_stop_tone (TpSvcChannelInterfaceDTMF *iface,
                             guint stream_id,
                             DBusGMethodInvocation *context)
{
  TpsipMediaChannel *self = TPSIP_MEDIA_CHANNEL (iface);
  TpsipMediaChannelPrivate *priv;
  GError *error = NULL;

  DEBUG("enter");

  g_assert (TPSIP_IS_MEDIA_CHANNEL (self));

  priv = TPSIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!tpsip_media_session_stop_telephony_event (priv->session,
                                                 stream_id,
                                                 &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  tp_svc_channel_interface_dtmf_return_from_stop_tone (context);
}

static void
event_target_init(gpointer g_iface, gpointer iface_data)
{
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

  tp_svc_channel_implement_close (
      klass, tpsip_media_channel_dbus_close);
#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
      klass, tpsip_media_channel_##x)
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

static void
streamed_media_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeStreamedMediaClass *klass = (TpSvcChannelTypeStreamedMediaClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_streamed_media_implement_##x (\
    klass, tpsip_media_channel_##x)
  IMPLEMENT(list_streams);
  IMPLEMENT(remove_streams);
  IMPLEMENT(request_stream_direction);
  IMPLEMENT(request_streams);
#undef IMPLEMENT
}

static void
media_signalling_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceMediaSignallingClass *klass = (TpSvcChannelInterfaceMediaSignallingClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_media_signalling_implement_##x (\
    klass, tpsip_media_channel_##x)
  IMPLEMENT(get_session_handlers);
#undef IMPLEMENT
}

static void
dtmf_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceDTMFClass *klass = (TpSvcChannelInterfaceDTMFClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_dtmf_implement_##x (\
    klass, tpsip_media_channel_##x)
  IMPLEMENT(start_tone);
  IMPLEMENT(stop_tone);
#undef IMPLEMENT
}

static void
call_state_iface_init (gpointer g_iface,
                       gpointer iface_data)
{
  TpSvcChannelInterfaceCallStateClass *klass = g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_call_state_implement_##x (\
    klass, tpsip_media_channel_##x)
  IMPLEMENT (get_call_states);
#undef IMPLEMENT
}

static void
hold_iface_init (gpointer g_iface,
                 gpointer iface_data)
{
  TpSvcChannelInterfaceHoldClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_hold_implement_##x (\
    klass, tpsip_media_channel_##x)
  IMPLEMENT (get_hold_state);
  IMPLEMENT (request_hold);
#undef IMPLEMENT
}
