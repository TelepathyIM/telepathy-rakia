/*
 * sip-text-channel.c - Source for RakiaTextChannel
 * Copyright (C) 2005-2008 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-im-channel).
 *   @author See gabble-im-channel.c
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

#include "rakia/text-channel.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-generic.h>

#include "rakia/event-target.h"
#include "rakia/base-connection.h"

#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG TPSIP_DEBUG_IM
#include "rakia/debug.h"

static gboolean
rakia_text_channel_nua_r_message_cb (RakiaTextChannel *self,
                                     const RakiaNuaEvent *ev,
                                     tagi_t            tags[],
                                     gpointer          foo);

static void channel_iface_init (gpointer, gpointer);
static void destroyable_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (RakiaTextChannel, rakia_text_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
      destroyable_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const char *rakia_text_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE,
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
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_REQUESTED,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  LAST_PROPERTY
};


/* private structures */

typedef struct _RakiaTextPendingMessage RakiaTextPendingMessage;

struct _RakiaTextPendingMessage
{
  nua_handle_t *nh;
  gchar *token;
  TpMessageSendingFlags flags;
};

typedef struct _RakiaTextChannelPrivate RakiaTextChannelPrivate;

struct _RakiaTextChannelPrivate
{
  RakiaBaseConnection *conn;
  gchar *object_path;
  TpHandle handle;
  TpHandle initiator;

  guint sent_id;
  GQueue  *sending_messages;

  gboolean closed;

  gboolean dispose_has_run;
};


#define _rakia_text_pending_new0() \
	(g_slice_new0(RakiaTextPendingMessage))

#define TPSIP_TEXT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_TEXT_CHANNEL, RakiaTextChannelPrivate))

static void rakia_text_pending_free (RakiaTextPendingMessage *msg,
                                     TpHandleRepoIface *contact_handles)
{
  if (msg->nh)
    nua_handle_unref (msg->nh);

  g_free (msg->token);

  g_slice_free (RakiaTextPendingMessage, msg);
}

static void
rakia_text_channel_init (RakiaTextChannel *obj)
{
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (obj);

  DEBUG("enter");

  priv->sending_messages = g_queue_new ();
}

static void rakia_text_channel_send_message (GObject *object,
    TpMessage *message,
    TpMessageSendingFlags flags);

static void
rakia_text_channel_constructed (GObject *obj)
{
  RakiaTextChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_handles;
  TpDBusDaemon *bus;
  TpChannelTextMessageType types[] = {
      TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
  };
  const gchar * supported_content_types[] = {
      "text/plain",
      NULL
  };
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_text_channel_parent_class);

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);

  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(TPSIP_TEXT_CHANNEL(obj));
  base_conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_handles, priv->handle);

  g_assert (priv->initiator != 0);
  tp_handle_ref (contact_handles, priv->initiator);

  rakia_base_connection_add_auth_handler (priv->conn, TPSIP_EVENT_TARGET (obj));

  g_signal_connect (obj,
                    "nua-event::nua_r_message",
                    G_CALLBACK (rakia_text_channel_nua_r_message_cb),
                    NULL);

  tp_message_mixin_init (obj, G_STRUCT_OFFSET (RakiaTextChannel, message_mixin),
      base_conn);

  tp_message_mixin_implement_sending (obj, rakia_text_channel_send_message,
      G_N_ELEMENTS (types), types, 0,
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_FAILURES |
      TP_DELIVERY_REPORTING_SUPPORT_FLAG_RECEIVE_SUCCESSES,
      supported_content_types);

  bus = tp_base_connection_get_dbus_daemon (base_conn);
  tp_dbus_daemon_register_object (bus, priv->object_path, obj);
}


static void rakia_text_channel_get_property(GObject    *object,
					  guint       property_id,
					  GValue     *value,
					  GParamSpec *pspec);
static void rakia_text_channel_set_property(GObject     *object,
					  guint        property_id,
					  const GValue *value,
					  GParamSpec   *pspec);
static void rakia_text_channel_dispose(GObject *object);
static void rakia_text_channel_finalize(GObject *object);

static void
rakia_text_channel_class_init(RakiaTextChannelClass *klass)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "InitiatorHandle", "initiator-handle", NULL },
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

  g_type_class_add_private (klass, sizeof (RakiaTextChannelPrivate));

  object_class->get_property = rakia_text_channel_get_property;
  object_class->set_property = rakia_text_channel_set_property;

  object_class->constructed = rakia_text_channel_constructed;

  object_class->dispose = rakia_text_channel_dispose;
  object_class->finalize = rakia_text_channel_finalize;

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

  param_spec = g_param_spec_object("connection", "RakiaConnection object",
      "SIP connection object that owns this SIP media channel object.",
      TPSIP_TYPE_BASE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Addition Channel.Interface.* interfaces", G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_string ("target-id", "Peer's SIP URI",
      "The URI string obtained by inspecting the peer handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
      "The contact who initiated the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
      param_spec);

  param_spec = g_param_spec_string ("initiator-id", "Initiator's URI",
      "The string obtained by inspecting the initiator-handle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIATOR_ID,
      param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  klass->dbus_props_class.interfaces =
      prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (RakiaTextChannelClass, dbus_props_class));

  tp_message_mixin_init_dbus_properties (object_class);
}

static void
rakia_text_channel_get_property(GObject *object,
				guint property_id,
				GValue *value,
				GParamSpec *pspec)
{
  RakiaTextChannel *chan = TPSIP_TEXT_CHANNEL(object);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(chan);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  switch (property_id)
    {
    case PROP_CONNECTION:
      g_value_set_object(value, priv->conn);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string(value, priv->object_path);
      break;

    case PROP_CHANNEL_TYPE:
      g_value_set_string(value, TP_IFACE_CHANNEL_TYPE_TEXT);
      break;

    case PROP_HANDLE_TYPE:
      g_value_set_uint(value, TP_HANDLE_TYPE_CONTACT);
      break;

    case PROP_HANDLE:
      g_value_set_uint(value, priv->handle);
      break;

    case PROP_TARGET_ID:
      {
        TpHandleRepoIface *repo = tp_base_connection_get_handles (
            base_conn, TP_HANDLE_TYPE_CONTACT);

        g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
      }
      break;

    case PROP_INITIATOR_HANDLE:
      g_value_set_uint (value, priv->initiator);
      break;

    case PROP_INITIATOR_ID:
      {
        TpHandleRepoIface *repo = tp_base_connection_get_handles (
            base_conn, TP_HANDLE_TYPE_CONTACT);

        g_assert (priv->initiator != 0);
        g_value_set_string (value,
            tp_handle_inspect (repo, priv->initiator));
      }
      break;

    case PROP_REQUESTED:
      g_value_set_boolean (value, (priv->initiator == base_conn->self_handle));
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
              TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
              TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
              TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
              TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
              NULL));
      break;

    case PROP_INTERFACES:
      g_value_set_static_boxed (value, rakia_text_channel_interfaces);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
rakia_text_channel_set_property(GObject *object,
			        guint property_id,
			        const GValue *value,
			        GParamSpec *pspec)
{
  RakiaTextChannel *chan = TPSIP_TEXT_CHANNEL (object);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (chan);

  switch (property_id)
    {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;

    case PROP_OBJECT_PATH:
      g_assert (priv->object_path == NULL);
      priv->object_path = g_value_dup_string (value);
      break;

    case PROP_CHANNEL_TYPE:
    case PROP_HANDLE_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;

    case PROP_HANDLE:
      /* we don't ref it here because we don't necessarily have access to the
       * contact repo yet - instead we ref it in the constructed */
      priv->handle = g_value_get_uint(value);
      break;

    case PROP_INITIATOR_HANDLE:
      /* similarly we can't ref this yet */
      priv->initiator = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
rakia_text_channel_dispose(GObject *object)
{
  RakiaTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_channel_emit_closed (self);
    }

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_handle_unref (contact_handles, priv->handle);

  if (priv->initiator != 0)
    tp_handle_unref (contact_handles, priv->initiator);

  if (G_OBJECT_CLASS (rakia_text_channel_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_text_channel_parent_class)->dispose (object);
}

static void
zap_pending_messages (GQueue *pending_messages,
                      TpHandleRepoIface *contact_handles)
{
  g_queue_foreach (pending_messages,
      (GFunc) rakia_text_pending_free, contact_handles);
  g_queue_clear (pending_messages);
}

static void
rakia_text_channel_finalize(GObject *object)
{
  RakiaTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_CONTACT);

  DEBUG ("%u pending outgoing message requests",
      g_queue_get_length (priv->sending_messages));
  zap_pending_messages (priv->sending_messages, contact_handles);
  g_queue_free (priv->sending_messages);

  g_free (priv->object_path);

  tp_message_mixin_finalize (object);

  G_OBJECT_CLASS (rakia_text_channel_parent_class)->finalize (object);
}

static gint rakia_acknowledged_messages_compare(gconstpointer msg,
					      gconstpointer id)
{
  RakiaTextPendingMessage *message = (RakiaTextPendingMessage *)msg;
  nua_handle_t *nh = (nua_handle_t *) id;
  return (message->nh != nh);
}

/**
 * rakia_text_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
rakia_text_channel_close (TpSvcChannel *iface,
                          DBusGMethodInvocation *context)
{
  RakiaTextChannel *self = TPSIP_TEXT_CHANNEL (iface);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(self);

  if (priv->closed)
    {
      DEBUG ("already closed, doing nothing");
    }
  else
    {
      if (!tp_message_mixin_has_pending_messages ((GObject *) self, NULL))
        {
          DEBUG ("actually closing, no pending messages");
          priv->closed = TRUE;
        }
      else
        {
          DEBUG ("not really closing, there are pending messages left");

          tp_message_mixin_set_rescued ((GObject *) self);

          if (priv->initiator != priv->handle)
            {
              TpHandleRepoIface *contact_repo = tp_base_connection_get_handles
                  ((TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

              g_assert (priv->initiator != 0);
              g_assert (priv->handle != 0);

              tp_handle_unref (contact_repo, priv->initiator);
              priv->initiator = priv->handle;
              tp_handle_ref (contact_repo, priv->initiator);
            }
        }
      tp_svc_channel_emit_closed (self);
    }
  tp_svc_channel_return_from_close (context);
}

/**
 * rakia_text_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface org.freedesktop.Telepathy.Channel.Interface.Destroyable
 */
static void
rakia_text_channel_destroy (TpSvcChannelInterfaceDestroyable *iface,
                            DBusGMethodInvocation *context)
{
  RakiaTextChannel *self = TPSIP_TEXT_CHANNEL (iface);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  tp_message_mixin_clear ((GObject *) iface);

  /* Close() and Destroy() have the same signature, so we can safely
   * chain to the other function now */
  rakia_text_channel_close ((TpSvcChannel *) self, context);
}

/**
 * rakia_text_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
rakia_text_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  DEBUG("enter");

  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * rakia_text_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
rakia_text_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  RakiaTextChannel *obj = TPSIP_TEXT_CHANNEL (iface);
  RakiaTextChannelPrivate *priv;

  DEBUG("enter");

  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(obj);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * rakia_text_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
rakia_text_channel_get_interfaces(TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  DEBUG("enter");
  tp_svc_channel_return_from_get_interfaces (context,
      rakia_text_channel_interfaces);
}

static void
rakia_text_channel_send_message (GObject *object,
    TpMessage *message,
    TpMessageSendingFlags flags)
{
  RakiaTextChannel *self = TPSIP_TEXT_CHANNEL(object);
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  RakiaTextPendingMessage *msg = NULL;
  nua_handle_t *msg_nh = NULL;
  GError *error = NULL;
  const GHashTable *part;
  guint n_parts;
  const gchar *content_type;
  const gchar *text;

  DEBUG("enter");

#define INVALID_ARGUMENT(msg, ...) \
  G_STMT_START { \
    DEBUG (msg , ## __VA_ARGS__); \
    g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, \
        msg , ## __VA_ARGS__); \
    goto fail; \
  } G_STMT_END

  part = tp_message_peek (message, 0);

  if (tp_asv_lookup (part, "message-type") != NULL)
    {
      if (tp_asv_get_uint32 (part, "message-type", NULL) !=
          TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL)
        INVALID_ARGUMENT ("invalid message type");
    }

  n_parts = tp_message_count_parts (message);

  if (n_parts != 2)
    INVALID_ARGUMENT ("message must contain exactly 1 part, not %u",
        (n_parts - 1));

  part = tp_message_peek (message, 1);
  content_type = tp_asv_get_string (part, "content-type");
  text = tp_asv_get_string (part, "content");

  if (content_type == NULL || tp_strdiff (content_type, "text/plain"))
    INVALID_ARGUMENT ("message must be text/plain");

  if (text == NULL)
    INVALID_ARGUMENT ("content must be a UTF-8 string");

  /* Okay, it's valid. Let's send it. */

  msg_nh = rakia_base_connection_create_handle (priv->conn, priv->handle);
  if (msg_nh == NULL)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Request creation failed");
      goto fail;
    }

  rakia_event_target_attach (msg_nh, (GObject *) self);

  nua_message(msg_nh,
	      SIPTAG_CONTENT_TYPE_STR("text/plain"),
	      SIPTAG_PAYLOAD_STR(text),
	      TAG_END());

  msg = _rakia_text_pending_new0 ();
  msg->nh = msg_nh;
  msg->token = g_strdup_printf ("%u", priv->sent_id++);
  msg->flags = flags;

  tp_message_mixin_sent (object, message, flags, msg->token, NULL);
  g_queue_push_tail (priv->sending_messages, msg);

  DEBUG ("message queued for delivery");
  return;

fail:
  g_assert (error != NULL);
  tp_message_mixin_sent (object, message, 0, NULL, error);
  g_error_free (error);
}

static gchar *
text_send_error_to_dbus_error (TpChannelTextSendError error)
{
  switch (error)
    {
      case TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE:
        return TP_ERROR_STR_OFFLINE;

      case TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT:
        return TP_ERROR_STR_INVALID_HANDLE;

      case TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED:
        return TP_ERROR_STR_PERMISSION_DENIED;

      case TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG:
        return TP_ERROR_STR_INVALID_ARGUMENT;

      case TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED:
        return TP_ERROR_STR_NOT_IMPLEMENTED;

      case TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN:
      default:
        return TP_ERROR_STR_INVALID_ARGUMENT;
    }

  return NULL;
}

static void
delivery_report (RakiaTextChannel *self,
    const gchar *token,
    TpDeliveryStatus status,
    TpChannelTextSendError send_error)
{
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpBaseConnection *base_conn;
  TpMessage *msg;

  base_conn = (TpBaseConnection *) priv->conn;

  msg = tp_message_new (base_conn, 1, 1);

  tp_message_set_handle (msg, 0, "message-sender", TP_HANDLE_TYPE_CONTACT,
      priv->handle);

  tp_message_set_uint32 (msg, 0, "message-type",
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);

  tp_message_set_string (msg, 0, "delivery-token", token);
  tp_message_set_uint32 (msg, 0, "delivery-status", status);

  if (status == TP_DELIVERY_STATUS_TEMPORARILY_FAILED ||
      status == TP_DELIVERY_STATUS_PERMANENTLY_FAILED)
    {
      if (send_error != TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN)
        tp_message_set_uint32 (msg, 0, "delivery-error", send_error);

      tp_message_set_string (msg, 0, "delivery-dbus-error",
          text_send_error_to_dbus_error (send_error));
    }

  tp_message_mixin_take_received((GObject *) self, msg);
}

static gboolean
rakia_text_channel_nua_r_message_cb (RakiaTextChannel *self,
                                     const RakiaNuaEvent *ev,
                                     tagi_t            tags[],
                                     gpointer          foo)
{
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  RakiaTextPendingMessage *msg;
  TpHandleRepoIface *contact_repo;
  TpChannelTextSendError send_error;
  GList *node;

  /* ignore provisional responses */
  if (ev->status < 200)
    return TRUE;

  node = g_queue_find_custom (priv->sending_messages,
                              ev->nua_handle,
			      rakia_acknowledged_messages_compare);

  /* Shouldn't happen... */
  if (node == NULL)
    {
      WARNING ("message pending sent acknowledgement not found");
      return FALSE;
    }

  msg = (RakiaTextPendingMessage *)node->data;

  g_assert (msg != NULL);

  /* FIXME: generate a delivery report */
  if (ev->status >= 200 && ev->status < 300)
    {
      DEBUG ("message delivered");

      if (msg->flags & TP_MESSAGE_SENDING_FLAG_REPORT_DELIVERY)
        {
          DEBUG ("Sending delivery report");
          delivery_report (self, msg->token, TP_DELIVERY_STATUS_DELIVERED, 0);
        }
    }
  else
    {
      switch (ev->status)
        {
        case 401:
        case 403:
        case 407:
        case 603:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_PERMISSION_DENIED;
          break;
        case 604:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;
          break;
        case 405:
        case 406:
        case 415:
        case 416:
        case 488:
        case 501:
        case 505:
        case 606:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_NOT_IMPLEMENTED;
          break;
        case 410:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT /* TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE? */;
          break;
        case 404:
        case 480:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_OFFLINE;
          break;
        case 413:
        case 513:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_TOO_LONG;
          break;
        default:
          send_error = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
      }

      delivery_report (self, msg->token, TP_DELIVERY_STATUS_PERMANENTLY_FAILED,
          send_error);
  }

  g_queue_remove(priv->sending_messages, msg);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  rakia_text_pending_free(msg, contact_repo);

  return TRUE;
}

void rakia_text_channel_receive(RakiaTextChannel *chan,
                                const sip_t *sip,
                                TpHandle sender,
                                const char *text,
                                gsize len)
{
  RakiaTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (chan);
  TpMessage *msg;
  TpBaseConnection *base_conn;
  sip_call_id_t *hdr_call_id;
  sip_cseq_t *hdr_cseq;
  sip_date_t *hdr_date_sent;

  base_conn = (TpBaseConnection *) priv->conn;
  msg = tp_message_new (base_conn, 2, 2);

  DEBUG ("Received message from contact %u: %s", sender, text);

  /* Header */
  tp_message_set_handle (msg, 0, "message-sender", TP_HANDLE_TYPE_CONTACT,
      sender);
  tp_message_set_int64 (msg, 0, "message-received", time (NULL));

  hdr_date_sent = sip_date (sip);
  if (hdr_date_sent != NULL)
    {
      tp_message_set_int64 (msg, 0, "message-sent",
          hdr_date_sent->d_time - SU_TIME_EPOCH);
    }

  /* Create a message token out of globally unique SIP header values.
   * As MESSAGE requests can be sent within a dialog, we have to append
   * the Call-ID value with the sequence number in CSeq. */
  hdr_call_id = sip_call_id (sip);
  hdr_cseq = sip_cseq (sip);
  if (hdr_call_id != NULL && hdr_cseq != NULL)
    {
      tp_message_set_string_printf (msg, 0, "message-token", "%s;cseq=%u",
          hdr_call_id->i_id, (guint) hdr_cseq->cs_seq);
    }

  /* Body */
  tp_message_set_string (msg, 1, "content-type", "text/plain");
  tp_message_set_string (msg, 1, "content", text);

  tp_message_mixin_take_received (G_OBJECT (chan), msg);
}

static void
destroyable_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
  TpSvcChannelInterfaceDestroyableClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
    klass, rakia_text_channel_##x)
  IMPLEMENT(destroy);
#undef IMPLEMENT
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
      klass, rakia_text_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}
