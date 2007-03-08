/*
 * sip-text-channel.c - Source for SIPTextChannel
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
 * @author Martti Mela <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-im-channel).
 *   @author See gabble-im-channel.c
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

#include <assert.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sofia-sip/sip.h>
#include <sofia-sip/sip_header.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-channel.h>

#include "sip-connection.h"
#include "sip-connection-helpers.h"

#include "sip-text-channel.h"

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SIPTextChannel, sip_text_channel, G_TYPE_OBJECT,
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



#define TP_TYPE_PENDING_MESSAGE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))


#define enter g_debug("%s: enter", G_STRFUNC)


/* private structures */

typedef struct _SIPTextPendingMessage SIPTextPendingMessage;

struct _SIPTextPendingMessage
{
  guint id;
  nua_handle_t *nh;
  
  
  time_t timestamp;
  TpHandle sender;
  
  TpChannelTextMessageType type;
  
  gchar *text;
};

typedef struct _SIPTextChannelPrivate SIPTextChannelPrivate;

struct _SIPTextChannelPrivate
{
  SIPConnection *conn;
  gchar *object_path;
  TpHandle handle;

  guint recv_id;
  guint sent_id;
  GQueue  *pending_messages;
  GQueue  *messages_to_be_acknowledged;

  gboolean closed;

  gboolean dispose_has_run;
};


#define _sip_text_pending_new() \
	(g_slice_new(SIPTextPendingMessage))
#define _sip_text_pending_new0() \
	(g_slice_new0(SIPTextPendingMessage))

#define SIP_TEXT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_TEXT_CHANNEL, SIPTextChannelPrivate))

static void _sip_text_pending_free(SIPTextPendingMessage *msg)
{
        enter;

	if (msg->text)
	{
		g_free(msg->text);
	}
	msg->nh = NULL;
	
	g_slice_free(SIPTextPendingMessage, msg);
}


static void
sip_text_channel_init (SIPTextChannel *obj)
{
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (obj);

  enter;

  priv->pending_messages = g_queue_new ();
  priv->messages_to_be_acknowledged = g_queue_new ();

  /* XXX -- mela: priv->last_msg = _sip_text_pending_new0(); */

}

static
GObject *sip_text_channel_constructor(GType type,
				      guint n_props,
				      GObjectConstructParam *props)
{
  GObject *obj;
  SIPTextChannelPrivate *priv;
  DBusGConnection *bus;

  enter;

  obj =
    G_OBJECT_CLASS(sip_text_channel_parent_class)->constructor(type,
							       n_props,
							       props);
  priv = SIP_TEXT_CHANNEL_GET_PRIVATE(SIP_TEXT_CHANNEL(obj));

  bus = tp_get_bus();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);

  return obj;
}


static void sip_text_channel_get_property(GObject    *object,
					  guint       property_id,
					  GValue     *value,
					  GParamSpec *pspec);
static void sip_text_channel_set_property(GObject     *object,
					  guint        property_id,
					  const GValue *value,
					  GParamSpec   *pspec);
static void sip_text_channel_dispose(GObject *object);
static void sip_text_channel_finalize(GObject *object);

static void
sip_text_channel_class_init(SIPTextChannelClass *sip_text_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_text_channel_class);
  GParamSpec *param_spec;

  enter;

  g_type_class_add_private (sip_text_channel_class, sizeof (SIPTextChannelPrivate));

  object_class->get_property = sip_text_channel_get_property;
  object_class->set_property = sip_text_channel_set_property;

  object_class->constructor = sip_text_channel_constructor;

  object_class->dispose = sip_text_channel_dispose;
  object_class->finalize = sip_text_channel_finalize;

  param_spec = g_param_spec_object("connection", "SIPConnection object",
				   "SIP connection object that owns this "
				   "SIP media channel object.",
				   SIP_TYPE_CONNECTION,
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NICK |
				   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string("object-path", "D-Bus object path",
				   "The D-Bus object path used for this "
				   "object on the bus.",
				   NULL,
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME |
				   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string("channel-type", "Telepathy channel type",
				   "The D-Bus interface representing the "
				   "type of this channel.",
				   NULL,
				   G_PARAM_READABLE |
				   G_PARAM_STATIC_NAME |
				   G_PARAM_STATIC_BLURB);
  g_object_class_install_property(object_class, PROP_CHANNEL_TYPE, param_spec);

  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");
}

static
void sip_text_channel_get_property(GObject *object,
				   guint property_id,
				   GValue *value,
				   GParamSpec *pspec)
{
  SIPTextChannel *chan = SIP_TEXT_CHANNEL(object);
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE(chan);

  enter;


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
    g_debug ("%s: PROP_HANDLE: %d", G_STRFUNC, priv->handle); /* XXX -- mela */
    g_debug ("%s: priv->handle = %d", G_STRFUNC, priv->handle); /* XXX -- mela */
    g_value_set_uint(value, priv->handle);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    break;
  }
}

static
void
sip_text_channel_set_property(GObject *object,
			      guint property_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
  SIPTextChannel *chan = SIP_TEXT_CHANNEL (object);
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (chan);

  enter;

  switch (property_id) {
  case PROP_CONNECTION:
    g_debug ("%s: PROP_CONNECTION", G_STRFUNC);
    priv->conn = g_value_get_object (value);
    break;
    
  case PROP_OBJECT_PATH:
    g_debug ("%s: PROP_OBJECT_PATH", G_STRFUNC);
    g_free (priv->object_path);
    priv->object_path = g_value_dup_string (value);
    break;

  case PROP_HANDLE_TYPE:
    /* this property is writable in the interface, but not actually
     * meaningfully changable on this channel, so we do nothing */
    break;

  case PROP_HANDLE:
    priv->handle = g_value_get_uint(value);
    g_debug ("%s: priv->handle = %d", G_STRFUNC, priv->handle);
    g_debug ("%s: PROP_HANDLE: %d", G_STRFUNC, priv->handle);
    break;
    
  default:
    g_debug ("%s: default", G_STRFUNC);
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

void
sip_text_channel_dispose(GObject *object)
{
  SIPTextChannel *self = SIP_TEXT_CHANNEL (object);
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (self);

  enter;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (sip_text_channel_parent_class)->dispose)
    G_OBJECT_CLASS (sip_text_channel_parent_class)->dispose (object);
}

void
sip_text_channel_finalize(GObject *object)
{
  SIPTextChannel *self = SIP_TEXT_CHANNEL (object);
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (self);

  enter;

  if (!priv)
    return;

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (sip_text_channel_parent_class)->finalize (object);
}



static gint sip_pending_message_compare(gconstpointer msg, gconstpointer id)
{
	SIPTextPendingMessage *message = (SIPTextPendingMessage *)(msg);

	enter;

	return (message->id != GPOINTER_TO_INT(id));
}


static gint sip_acknowledged_messages_compare(gconstpointer msg,
					      gconstpointer id)
{
  SIPTextPendingMessage *message = (SIPTextPendingMessage *)msg;
  nua_handle_t *nh = (nua_handle_t *) id;
  enter;

  return (message->nh != nh);
}

/**
 * sip_text_channel_acknowledge_pending_messages
 *
 * Implements DBus method AcknowledgePendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
sip_text_channel_acknowledge_pending_messages(TpSvcChannelTypeText *iface,
					      const GArray *ids,
					      DBusGMethodInvocation *context)
{
  SIPTextChannel *obj = SIP_TEXT_CHANNEL (iface);
  SIPTextChannelPrivate *priv;
  TpBaseConnection *conn;
  GList **nodes;
  SIPTextPendingMessage *msg;
  guint i;

  enter;
  
  priv = SIP_TEXT_CHANNEL_GET_PRIVATE(obj);
  conn = (TpBaseConnection *)(priv->conn);

  nodes = g_new(GList *, ids->len);

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index(ids, guint, i);

      nodes[i] = g_queue_find_custom (priv->pending_messages,
                                      GINT_TO_POINTER (id),
                                      sip_pending_message_compare);

      if (nodes[i] == NULL)
        {
          GError *error = NULL;

          g_debug ("invalid message id %u", id);

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
      msg = (SIPTextPendingMessage *) nodes[i]->data;

      g_debug ("acknowleding message id %u", msg->id);

      g_queue_remove (priv->pending_messages, msg);

      tp_handle_unref (conn->handles[TP_HANDLE_TYPE_CONTACT],
          msg->sender);
      _sip_text_pending_free (msg);
    }

  g_free(nodes);

  tp_svc_channel_type_text_return_from_acknowledge_pending_messages (context);
}


/**
 * sip_text_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_text_channel_close (TpSvcChannel *iface, DBusGMethodInvocation *context)
{
  enter;

  tp_svc_channel_return_from_close (context);
}


/**
 * sip_text_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_text_channel_get_channel_type (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  enter;

  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_TEXT);
}


/**
 * sip_text_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_text_channel_get_handle (TpSvcChannel *iface,
                             DBusGMethodInvocation *context)
{
  SIPTextChannel *obj = SIP_TEXT_CHANNEL (iface);

  SIPTextChannelPrivate *priv;

  enter;

  g_assert(obj != NULL);
  g_assert(SIP_IS_TEXT_CHANNEL(obj));

  priv = SIP_TEXT_CHANNEL_GET_PRIVATE(obj);

  g_debug ("%s: priv->handle = %d", G_STRFUNC, priv->handle); /* XXX -- mela */
  tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
      priv->handle);
}


/**
 * sip_text_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_text_channel_get_interfaces(TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  const char *interfaces[] = { NULL };

  enter;
  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}


/**
 * sip_text_channel_get_message_types
 *
 * Implements DBus method GetMessageTypes
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
sip_text_channel_get_message_types(TpSvcChannelTypeText *iface,
                                   DBusGMethodInvocation *context)
{
  GArray *ret = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
  guint normal[1] = { TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL };

  enter;
  g_array_append_val (ret, normal);
  tp_svc_channel_type_text_return_from_get_message_types (context, ret);
  g_array_free (ret, TRUE);
}


/**
 * sip_text_channel_list_pending_messages
 *
 * Implements DBus method ListPendingMessages
 * on interface org.freedesktop.Telepathy.Channel.Type.Text
 */
static void
sip_text_channel_list_pending_messages(TpSvcChannelTypeText *iface,
                                       gboolean clear,
                                       DBusGMethodInvocation *context)
{
  SIPTextChannel *self = SIP_TEXT_CHANNEL(iface);
  SIPTextChannelPrivate *priv;
  guint count;
  GPtrArray *messages;
  GList *cur;

  priv = SIP_TEXT_CHANNEL_GET_PRIVATE (self);

  count = g_queue_get_length (priv->pending_messages);
  messages = g_ptr_array_sized_new (count);

  for (cur = (clear ? g_queue_pop_head_link(priv->pending_messages)
                    : g_queue_peek_head_link(priv->pending_messages));
       cur != NULL;
       cur = (clear ? g_queue_pop_head_link(priv->pending_messages)
                    : cur->next))
    {
      SIPTextPendingMessage *msg = (SIPTextPendingMessage *) cur->data;
      GValue val = { 0, };

      g_value_init (&val, TP_TYPE_PENDING_MESSAGE_STRUCT);
      g_value_take_boxed (&val,
          dbus_g_type_specialized_construct (TP_TYPE_PENDING_MESSAGE_STRUCT));
      dbus_g_type_struct_set (&val,
			      0, msg->id,
			      1, msg->timestamp,
			      2, msg->sender,
			      3, msg->type,
			      4, 0 /* msg->flags */,
			      5, msg->text,
			      G_MAXUINT);

      g_ptr_array_add (messages, g_value_get_boxed (&val));
    }

  tp_svc_channel_type_text_return_from_list_pending_messages (context,
      messages);
}


/**
 * sip_text_channel_send
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
sip_text_channel_send(TpSvcChannelTypeText *iface,
                      guint type,
                      const gchar *text,
                      DBusGMethodInvocation *context)
{
  SIPTextChannel *self = SIP_TEXT_CHANNEL(iface);
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (self);
  SIPConnection *sip_conn = SIP_CONNECTION (priv->conn);
  TpBaseConnection *conn = (TpBaseConnection *)(priv->conn);
  SIPTextPendingMessage *msg = NULL;
  gchar *object_path;
  nua_t *sofia_nua = sip_conn_sofia_nua (sip_conn);
  su_home_t *sofia_home = sip_conn_sofia_home (sip_conn);
  nua_handle_t *msg_nh = NULL;
  const char *recipient;

  enter;

  g_assert(sofia_nua);
  g_assert(sofia_home);

  object_path = g_strdup_printf ("%s/TextChannel%u", priv->object_path, priv->handle);

  g_debug ("%s: priv->handle = %d", G_STRFUNC, priv->handle);
  recipient = tp_handle_inspect(
      conn->handles[TP_HANDLE_TYPE_CONTACT],
      priv->handle);
  
  if ((recipient == NULL) || (strlen(recipient) == 0)) {
      GError invalid =  { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "invalid recipient" };

      g_debug("%s: invalid recipient %d", G_STRFUNC, priv->handle);
      dbus_g_method_return_error (context, &invalid);
      return;
  }
  
  /* XXX: do we have any need to track the handles at client side? */

  msg_nh = sip_conn_create_request_handle (sofia_nua, sofia_home,
					   recipient, priv->handle);
  nua_message(msg_nh,
	      SIPTAG_CONTENT_TYPE_STR("text/plain"),
	      SIPTAG_PAYLOAD_STR(text),
	      TAG_END());

  msg = _sip_text_pending_new();
  msg->nh = msg_nh;
  msg->text = g_strdup(text);
  msg->type = type;
  msg->timestamp = time(NULL);

  g_queue_push_tail(priv->messages_to_be_acknowledged, msg);

  tp_svc_channel_type_text_return_from_send (context);
}


void sip_text_channel_message_emit(nua_handle_t *nh, SIPTextChannel *obj, int status)
{
  SIPTextChannelPrivate *priv = SIP_TEXT_CHANNEL_GET_PRIVATE (obj);
  SIPTextPendingMessage *msg;
  TpChannelTextSendError send_error;
  GList *node;

  enter;

  node = g_queue_find_custom(priv->messages_to_be_acknowledged, nh,
			     sip_acknowledged_messages_compare);

  /* Shouldn't happen... */
  if (!node) {
    g_debug("%s: message not found", G_STRFUNC);
    return;
  }
  
  msg = (SIPTextPendingMessage *)node->data;

  if (!msg) {
    g_critical ("%s: message broken. Corrupted memory?", G_STRFUNC);
    return;
  }

  if (status >= 200 && status < 300) {
    tp_svc_channel_type_text_emit_sent ((TpSvcChannelTypeText *)obj,
        msg->timestamp, msg->type, msg->text);
    return;
  }
  else if (status >= 400 && status < 500) {
    send_error = TP_CHANNEL_TEXT_SEND_ERROR_INVALID_CONTACT;
  }
  else {
    send_error = TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN;
  }

  tp_svc_channel_type_text_emit_send_error ((TpSvcChannelTypeText *)obj,
      send_error, msg->timestamp, msg->type, msg->text);  

  g_queue_remove(priv->messages_to_be_acknowledged, msg);
  _sip_text_pending_free(msg);
}


/**
 * sip_text_channel_receive

      <arg type="u" name="id" />
      <arg type="u" name="timestamp" />
      <arg type="u" name="sender" />
      <arg type="u" name="type" />
      <arg type="u" name="flags" />
      <arg type="s" name="text" />

 *
 */
void sip_text_channel_receive(SIPTextChannel *chan,
			      TpHandle sender,
			      const char *display_name,
			      const char *url,
			      const char *subject,
			      const char *message)
{
  SIPTextPendingMessage *msg;
  SIPTextChannelPrivate *priv;
  TpBaseConnection *conn;
  GObject *obj;

  enter;

  g_assert(chan != NULL);
  g_assert(SIP_IS_TEXT_CHANNEL(chan));
  priv = SIP_TEXT_CHANNEL_GET_PRIVATE (chan);
  conn = (TpBaseConnection *)(priv->conn);
  obj = &chan->parent;

  msg = _sip_text_pending_new();

  msg->id = priv->recv_id++;
  msg->timestamp = time(NULL);
  msg->sender = sender;
  msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  msg->text = g_strdup(message);

  g_queue_push_tail(priv->pending_messages, msg);

  tp_handle_ref (conn->handles[TP_HANDLE_TYPE_CONTACT], msg->sender);

  g_debug ("%s: message = %s", G_STRFUNC, message);

  tp_svc_channel_type_text_emit_received ((TpSvcChannelTypeText *)chan,
      msg->id, msg->timestamp, msg->sender, msg->type,
      0 /* flags */, msg->text);
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, sip_text_channel_##x##suffix)
  IMPLEMENT(close,);
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
    sip_text_channel_##x)
  IMPLEMENT(acknowledge_pending_messages);
  IMPLEMENT(get_message_types);
  IMPLEMENT(list_pending_messages);
  IMPLEMENT(send);
#undef IMPLEMENT
}
