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
#include <telepathy-glib/telepathy-glib.h>

#include "rakia/event-target.h"
#include "rakia/base-connection.h"

#include <sofia-sip/sip_protos.h>
#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG RAKIA_DEBUG_IM
#include "rakia/debug.h"

static gboolean
rakia_text_channel_nua_r_message_cb (RakiaTextChannel *self,
                                     const RakiaNuaEvent *ev,
                                     tagi_t            tags[],
                                     gpointer          foo);

static void destroyable_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (RakiaTextChannel, rakia_text_channel, TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (RAKIA_TYPE_EVENT_TARGET, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
      tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
      tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
      destroyable_iface_init));

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
  guint sent_id;
  GQueue  *sending_messages;

  gboolean closed;

  gboolean dispose_has_run;
};


#define _rakia_text_pending_new0() \
	(g_slice_new0(RakiaTextPendingMessage))

#define RAKIA_TEXT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), RAKIA_TYPE_TEXT_CHANNEL, RakiaTextChannelPrivate))

static void
rakia_text_pending_free (RakiaTextPendingMessage *msg)
{
  if (msg->nh)
    nua_handle_unref (msg->nh);

  g_free (msg->token);

  g_slice_free (RakiaTextPendingMessage, msg);
}

static void
rakia_text_channel_init (RakiaTextChannel *obj)
{
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE (obj);

  DEBUG("enter");

  priv->sending_messages = g_queue_new ();
}

static void rakia_text_channel_send_message (GObject *object,
    TpMessage *message,
    TpMessageSendingFlags flags);

static void
rakia_text_channel_constructed (GObject *obj)
{
  TpBaseConnection *base_conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (obj));
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

  g_assert (tp_base_channel_get_initiator (TP_BASE_CHANNEL (obj)) != 0);

  rakia_base_connection_add_auth_handler (RAKIA_BASE_CONNECTION (base_conn),
      RAKIA_EVENT_TARGET (obj));

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

  tp_base_channel_register (TP_BASE_CHANNEL (obj));
}


static void rakia_text_channel_dispose(GObject *object);
static void rakia_text_channel_finalize(GObject *object);

static void rakia_text_channel_close (TpBaseChannel *base);

static void
rakia_text_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      rakia_text_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "SupportedContentTypes",
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "MessageTypes",
      NULL);
}

static gchar *
rakia_text_channel_get_object_path_suffix (TpBaseChannel *chan)
{
  return g_strdup_printf ("TextChannel%u",
      tp_base_channel_get_target_handle (chan));
}

static GPtrArray *
rakia_text_channel_get_interfaces (TpBaseChannel *base)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_CHANNEL_CLASS (
      rakia_text_channel_parent_class)->get_interfaces (base);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_MESSAGES);
  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE);

  return interfaces;
}

static void
rakia_text_channel_class_init(RakiaTextChannelClass *klass)
{
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (klass);
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

  DEBUG("enter");

  g_type_class_add_private (klass, sizeof (RakiaTextChannelPrivate));

  object_class->constructed = rakia_text_channel_constructed;

  object_class->dispose = rakia_text_channel_dispose;
  object_class->finalize = rakia_text_channel_finalize;

  base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
  base_class->get_interfaces = rakia_text_channel_get_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_class->close = rakia_text_channel_close;
  base_class->fill_immutable_properties =
    rakia_text_channel_fill_immutable_properties;
  base_class->get_object_path_suffix =
    rakia_text_channel_get_object_path_suffix;

  klass->dbus_props_class.interfaces =
      prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (RakiaTextChannelClass, dbus_props_class));

  tp_message_mixin_init_dbus_properties (object_class);
}

static void
rakia_text_channel_dispose(GObject *object)
{
  RakiaTextChannel *self = RAKIA_TEXT_CHANNEL (object);
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_channel_emit_closed (self);
    }

  if (G_OBJECT_CLASS (rakia_text_channel_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_text_channel_parent_class)->dispose (object);
}

static void
zap_pending_messages (GQueue *pending_messages)
{
  g_queue_foreach (pending_messages,
      (GFunc) rakia_text_pending_free, NULL);
  g_queue_clear (pending_messages);
}

static void
rakia_text_channel_finalize(GObject *object)
{
  RakiaTextChannel *self = RAKIA_TEXT_CHANNEL (object);
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE (self);

  DEBUG ("%u pending outgoing message requests",
      g_queue_get_length (priv->sending_messages));
  zap_pending_messages (priv->sending_messages);
  g_queue_free (priv->sending_messages);

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

static void
rakia_text_channel_close (TpBaseChannel *base)
{
  RakiaTextChannel *self = RAKIA_TEXT_CHANNEL (base);
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE(self);

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
          tp_base_channel_destroyed (base);
        }
      else
        {
          DEBUG ("not really closing, there are pending messages left");

          tp_message_mixin_set_rescued ((GObject *) self);

          tp_base_channel_reopened (base,
              tp_base_channel_get_target_handle (base));
        }
    }
}

/**
 * rakia_text_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface Channel.Interface.Destroyable
 */
static void
rakia_text_channel_destroy (TpSvcChannelInterfaceDestroyable *iface,
                            DBusGMethodInvocation *context)
{
  tp_message_mixin_clear ((GObject *) iface);

  rakia_text_channel_close (TP_BASE_CHANNEL (iface));

  tp_svc_channel_interface_destroyable_return_from_destroy (context);
}

static void
rakia_text_channel_send_message (GObject *object,
    TpMessage *message,
    TpMessageSendingFlags flags)
{
  RakiaTextChannel *self = RAKIA_TEXT_CHANNEL(object);
  TpBaseConnection *conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (object));
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE (self);
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
    g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT, \
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

  msg_nh = rakia_base_connection_create_handle (RAKIA_BASE_CONNECTION (conn),
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (object)));
  if (msg_nh == NULL)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
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
  TpBaseConnection *conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (self));
  TpMessage *msg;

  msg = tp_cm_message_new (conn, 1);

  tp_cm_message_set_sender (msg, tp_base_channel_get_target_handle (
        TP_BASE_CHANNEL (self)));

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
  RakiaTextChannelPrivate *priv = RAKIA_TEXT_CHANNEL_GET_PRIVATE (self);
  RakiaTextPendingMessage *msg;
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

  rakia_text_pending_free (msg);

  return TRUE;
}

void rakia_text_channel_receive(RakiaTextChannel *chan,
                                const sip_t *sip,
                                TpHandle sender,
                                const char *text,
                                gsize len)
{
  TpBaseConnection *conn = tp_base_channel_get_connection (
      TP_BASE_CHANNEL (chan));
  TpMessage *msg;
  sip_call_id_t *hdr_call_id;
  sip_cseq_t *hdr_cseq;
  sip_date_t *hdr_date_sent;

  msg = tp_cm_message_new (conn, 2);

  DEBUG ("Received message from contact %u: %s", sender, text);

  /* Header */
  tp_cm_message_set_sender (msg, sender);
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
