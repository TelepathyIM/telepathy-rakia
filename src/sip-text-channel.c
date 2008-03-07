/*
 * sip-text-channel.c - Source for TpsipTextChannel
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sofia-sip/sip.h>
#include <sofia-sip/sip_header.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-channel.h>

#include "sip-text-channel.h"

#include "sip-connection.h"
#include "sip-connection-helpers.h"

#define DEBUG_FLAG TPSIP_DEBUG_IM
#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (TpsipTextChannel, tpsip_text_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  /* PROP_CREATOR, */
  LAST_PROPERTY
};


/* private structures */

typedef struct _TpsipTextPendingMessage TpsipTextPendingMessage;

struct _TpsipTextPendingMessage
{
  guint id;
  nua_handle_t *nh;
  
  
  time_t timestamp;
  TpHandle sender;
  
  TpChannelTextMessageType type;
  
  gchar *text;
};

typedef struct _TpsipTextChannelPrivate TpsipTextChannelPrivate;

struct _TpsipTextChannelPrivate
{
  TpsipConnection *conn;
  gchar *object_path;
  TpHandle handle;

  guint recv_id;
  guint sent_id;
  GQueue  *pending_messages;
  GQueue  *messages_to_be_acknowledged;

  gboolean closed;

  gboolean dispose_has_run;
};


#define _tpsip_text_pending_new() \
	(g_slice_new(TpsipTextPendingMessage))
#define _tpsip_text_pending_new0() \
	(g_slice_new0(TpsipTextPendingMessage))

#define TPSIP_TEXT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_TEXT_CHANNEL, TpsipTextChannelPrivate))

static void _tpsip_text_pending_free(TpsipTextPendingMessage *msg)
{
  if (msg->text)
    g_free (msg->text);

  nua_handle_unref (msg->nh);

  g_slice_free (TpsipTextPendingMessage, msg);
}

static void _tpsip_text_pending_free_walk(gpointer data, gpointer user_data)
{
  _tpsip_text_pending_free ((TpsipTextPendingMessage *) data);
}

static void
tpsip_text_channel_init (TpsipTextChannel *obj)
{
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (obj);

  DEBUG("enter");

  priv->pending_messages = g_queue_new ();
  priv->messages_to_be_acknowledged = g_queue_new ();
}

static
GObject *tpsip_text_channel_constructor(GType type,
				      guint n_props,
				      GObjectConstructParam *props)
{
  GObject *obj;
  TpsipTextChannelPrivate *priv;
  DBusGConnection *bus;

  DEBUG("enter");

  obj =
    G_OBJECT_CLASS(tpsip_text_channel_parent_class)->constructor(type,
							       n_props,
							       props);
  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(TPSIP_TEXT_CHANNEL(obj));

  bus = tp_get_bus();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  return obj;
}


static void tpsip_text_channel_get_property(GObject    *object,
					  guint       property_id,
					  GValue     *value,
					  GParamSpec *pspec);
static void tpsip_text_channel_set_property(GObject     *object,
					  guint        property_id,
					  const GValue *value,
					  GParamSpec   *pspec);
static void tpsip_text_channel_dispose(GObject *object);
static void tpsip_text_channel_finalize(GObject *object);

static void
tpsip_text_channel_class_init(TpsipTextChannelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  DEBUG("enter");

  g_type_class_add_private (klass, sizeof (TpsipTextChannelPrivate));

  object_class->get_property = tpsip_text_channel_get_property;
  object_class->set_property = tpsip_text_channel_set_property;

  object_class->constructor = tpsip_text_channel_constructor;

  object_class->dispose = tpsip_text_channel_dispose;
  object_class->finalize = tpsip_text_channel_finalize;

  param_spec = g_param_spec_object("connection", "TpsipConnection object",
				   "SIP connection object that owns this "
				   "SIP media channel object.",
				   TPSIP_TYPE_CONNECTION,
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NICK |
				   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);

  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
}

static void
tpsip_text_channel_get_property(GObject *object,
				   guint property_id,
				   GValue *value,
				   GParamSpec *pspec)
{
  TpsipTextChannel *chan = TPSIP_TEXT_CHANNEL(object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(chan);

  switch (property_id) {
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

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static void
tpsip_text_channel_set_property(GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
  TpsipTextChannel *chan = TPSIP_TEXT_CHANNEL (object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
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
    priv->handle = g_value_get_uint(value);
    break;
    
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
tpsip_text_channel_dispose(GObject *object)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    tpsip_text_channel_close (self);

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (tpsip_text_channel_parent_class)->dispose)
    G_OBJECT_CLASS (tpsip_text_channel_parent_class)->dispose (object);
}

static void
tpsip_text_channel_finalize(GObject *object)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);

  DEBUG("enter");

  if (!g_queue_is_empty (priv->pending_messages))
    {
      g_warning ("zapping %u pending incoming messages",
                 g_queue_get_length (priv->pending_messages));
      g_queue_foreach (priv->pending_messages,
                       _tpsip_text_pending_free_walk, NULL);
    }
  g_queue_free (priv->pending_messages);

  if (!g_queue_is_empty (priv->messages_to_be_acknowledged))
    {
      g_message ("zapping %u pending outgoing message requests",
                 g_queue_get_length (priv->messages_to_be_acknowledged));
      g_queue_foreach (priv->messages_to_be_acknowledged,
                       _tpsip_text_pending_free_walk, NULL);
    }
  g_queue_free (priv->messages_to_be_acknowledged);

  g_free (priv->object_path);

  G_OBJECT_CLASS (tpsip_text_channel_parent_class)->finalize (object);
}

static gint tpsip_pending_message_compare(gconstpointer msg, gconstpointer id)
{
  TpsipTextPendingMessage *message = (TpsipTextPendingMessage *)(msg);
  return (message->id != GPOINTER_TO_INT(id));
}

static gint tpsip_acknowledged_messages_compare(gconstpointer msg,
					      gconstpointer id)
{
  TpsipTextPendingMessage *message = (TpsipTextPendingMessage *)msg;
  nua_handle_t *nh = (nua_handle_t *) id;
  return (message->nh != nh);
}

/**
 * tpsip_text_channel_acknowledge_pending_messages
 *
 * Implements DBus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
tpsip_text_channel_acknowledge_pending_messages(TpSvcChannelTypeText *iface,
					      const GArray *ids,
					      DBusGMethodInvocation *context)
{
  TpsipTextChannel *obj = TPSIP_TEXT_CHANNEL (iface);
  TpsipTextChannelPrivate *priv;
  TpHandleRepoIface *contact_repo;
  GList **nodes;
  TpsipTextPendingMessage *msg;
  guint i;

  DEBUG("enter");
  
  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(obj);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  nodes = g_new(GList *, ids->len);

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index(ids, guint, i);

      nodes[i] = g_queue_find_custom (priv->pending_messages,
                                      GINT_TO_POINTER (id),
                                      tpsip_pending_message_compare);

      if (nodes[i] == NULL)
        {
          GError *error = NULL;

          DEBUG ("invalid message id %u", id);

          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "invalid message id %u", id);

          g_free(nodes);
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }

  for (i = 0; i < ids->len; i++)
    {
      msg = (TpsipTextPendingMessage *) nodes[i]->data;

      DEBUG("acknowledging message id %u", msg->id);

      g_queue_remove (priv->pending_messages, msg);

      tp_handle_unref (contact_repo, msg->sender);
      _tpsip_text_pending_free (msg);
    }

  g_free(nodes);

  tp_svc_channel_type_text_return_from_acknowledge_pending_messages (context);
}


/**
 * tpsip_text_channel_close_async
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_text_channel_close_async (TpSvcChannel *iface,
                              DBusGMethodInvocation *context)
{
  tpsip_text_channel_close (TPSIP_TEXT_CHANNEL(iface));
  tp_svc_channel_return_from_close (context);
}

void
tpsip_text_channel_close (TpsipTextChannel *self)
{
  TpsipTextChannelPrivate *priv;

  DEBUG("enter");

  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(self);
  if (!priv->closed)
    {
      priv->closed = TRUE;
      tp_svc_channel_emit_closed (self);
    }
}

/**
 * tpsip_text_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_text_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  DEBUG("enter");

  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * tpsip_text_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_text_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  TpsipTextChannel *obj = TPSIP_TEXT_CHANNEL (iface);
  TpsipTextChannelPrivate *priv;

  DEBUG("enter");

  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(obj);

  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * tpsip_text_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_text_channel_get_interfaces(TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  DEBUG("enter");
  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * tpsip_text_channel_get_message_types
 *
 * Implements DBus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
tpsip_text_channel_get_message_types(TpSvcChannelTypeText *iface,
                                   DBusGMethodInvocation *context)
{
  GArray *ret = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
  guint normal[1] = { TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL };

  DEBUG("enter");
  g_array_append_val (ret, normal);
  tp_svc_channel_type_text_return_from_get_message_types (context, ret);
  g_array_free (ret, TRUE);
}

static void
tpsip_pending_message_list_add (GPtrArray *list, TpsipTextPendingMessage *msg)
{
  GValue val = { 0 };
  GType message_type;

  message_type = TP_STRUCT_TYPE_PENDING_TEXT_MESSAGE;
  g_value_init (&val, message_type);
  g_value_take_boxed (&val,
                      dbus_g_type_specialized_construct (message_type));
  dbus_g_type_struct_set (&val,
                          0, msg->id,
                          1, msg->timestamp,
                          2, msg->sender,
                          3, msg->type,
                          4, 0 /* msg->flags */,
                          5, msg->text,
                          G_MAXUINT);

   g_ptr_array_add (list, g_value_get_boxed (&val));
}

/**
 * tpsip_text_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
tpsip_text_channel_list_pending_messages(TpSvcChannelTypeText *iface,
                                       gboolean clear,
                                       DBusGMethodInvocation *context)
{
  TpsipTextChannel *self = (TpsipTextChannel*) iface;
  TpsipTextChannelPrivate *priv;
  GPtrArray *messages;
  GList *cur;
  guint count;

  g_assert (TPSIP_IS_TEXT_CHANNEL(self));
  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);

  count = g_queue_get_length (priv->pending_messages);
  messages = g_ptr_array_sized_new (count);

  if (clear)
    {
      while ((cur = g_queue_pop_head_link (priv->pending_messages)) != NULL)
        tpsip_pending_message_list_add (messages,
                                        (TpsipTextPendingMessage *) cur->data);
    }
  else
    {
      for (cur = g_queue_peek_head_link(priv->pending_messages);
           cur != NULL;
           cur = cur->next)
        tpsip_pending_message_list_add (messages,
                                        (TpsipTextPendingMessage *) cur->data);
    }

  tp_svc_channel_type_text_return_from_list_pending_messages (context,
      messages);

  g_boxed_free (TP_ARRAY_TYPE_PENDING_TEXT_MESSAGE_LIST, messages);
}


/**
 * tpsip_text_channel_send
 *
 * Implements DBus method Send
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 *
 * @error: Used to return a pointer to a GError detailing any error
 *         that occured, DBus will throw the error only if this
 *         function returns false.
 *
 * Returns: TRUE if successful, FALSE if an error was thrown.
 */
static void
tpsip_text_channel_send(TpSvcChannelTypeText *iface,
                      guint type,
                      const gchar *text,
                      DBusGMethodInvocation *context)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL(iface);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpsipTextPendingMessage *msg = NULL;
  nua_handle_t *msg_nh = NULL;

  DEBUG("enter");

  if (priv->handle == 0)
    {
      GError invalid =  { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "invalid recipient" };

      g_warning ("invalid recipient handle %d", priv->handle);
      dbus_g_method_return_error (context, &invalid);
      return;
    }

  /* XXX: would it be helpful to bind the channel, or the
   * TpsipTextPendingMessage, or something, to the NH? */

  msg_nh = tpsip_conn_create_request_handle (priv->conn, priv->handle);
  g_assert (msg_nh != NULL);

  nua_message(msg_nh,
	      SIPTAG_CONTENT_TYPE_STR("text/plain"),
	      SIPTAG_PAYLOAD_STR(text),
	      TAG_END());

  msg = _tpsip_text_pending_new();
  msg->nh = msg_nh;
  msg->text = g_strdup(text);
  msg->type = type;
  msg->timestamp = time(NULL);

  g_queue_push_tail(priv->messages_to_be_acknowledged, msg);

  tp_svc_channel_type_text_return_from_send (context);
}

void
tpsip_text_channel_emit_message_status(TpsipTextChannel *obj,
                                     nua_handle_t *nh,
                                     int status)
{
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (obj);
  TpsipTextPendingMessage *msg;
  TpChannelTextSendError send_error;
  GList *node;

  DEBUG("enter");

  node = g_queue_find_custom(priv->messages_to_be_acknowledged, nh,
			     tpsip_acknowledged_messages_compare);

  /* Shouldn't happen... */
  if (!node) {
    g_warning ("message not found");
    return;
  }
  
  msg = (TpsipTextPendingMessage *)node->data;

  g_return_if_fail (msg != NULL);

  if (status >= 200 && status < 300)
    {
      tp_svc_channel_type_text_emit_sent ((TpSvcChannelTypeText *)obj,
          msg->timestamp, msg->type, msg->text);
    }
  else
    {
      switch (status)
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

    DEBUG("emitting send error %d %s", (int)send_error, msg->text);

    tp_svc_channel_type_text_emit_send_error ((TpSvcChannelTypeText *)obj,
	send_error, msg->timestamp, msg->type, msg->text);  
  }

  g_queue_remove(priv->messages_to_be_acknowledged, msg);
  _tpsip_text_pending_free(msg);
}

void tpsip_text_channel_receive(TpsipTextChannel *chan,
			      TpHandle sender,
			      const char *message)
{
  TpsipTextPendingMessage *msg;
  TpsipTextChannelPrivate *priv;
  TpHandleRepoIface *contact_repo;

  DEBUG("enter");

  g_assert(chan != NULL);
  g_assert(TPSIP_IS_TEXT_CHANNEL(chan));
  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (chan);
  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  msg = _tpsip_text_pending_new();

  msg->id = priv->recv_id++;
  msg->timestamp = time(NULL);
  msg->sender = sender;
  msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  msg->text = g_strdup(message);

  g_queue_push_tail(priv->pending_messages, msg);

  tp_handle_ref (contact_repo, msg->sender);

  DEBUG("received message: %s", message);

  tp_svc_channel_type_text_emit_received ((TpSvcChannelTypeText *)chan,
      msg->id, msg->timestamp, msg->sender, msg->type,
      0 /* flags */, msg->text);
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, tpsip_text_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
text_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeTextClass *klass = (TpSvcChannelTypeTextClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_text_implement_##x (klass,\
    tpsip_text_channel_##x)
  IMPLEMENT(acknowledge_pending_messages);
  IMPLEMENT(get_message_types);
  IMPLEMENT(list_pending_messages);
  IMPLEMENT(send);
#undef IMPLEMENT
}
