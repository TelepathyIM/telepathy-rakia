/*
 * sip-text-channel.c - Source for TpsipTextChannel
 * Copyright (C) 2005-2008 Collabora Ltd.
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

#include "sip-text-channel.h"

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

#include <tpsip/event-target.h>

#include "sip-connection.h"
#include "sip-connection-helpers.h"

#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG TPSIP_DEBUG_IM
#include "debug.h"

static gboolean
tpsip_text_channel_nua_r_message_cb (TpsipTextChannel *self,
                                     const TpsipNuaEvent *ev,
                                     tagi_t            tags[],
                                     gpointer          foo);

static void channel_iface_init (gpointer, gpointer);
static void text_iface_init (gpointer, gpointer);
static void destroyable_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (TpsipTextChannel, tpsip_text_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT, text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
      destroyable_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const char *tpsip_text_channel_interfaces[] = {
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

typedef struct _TpsipTextPendingMessage TpsipTextPendingMessage;

struct _TpsipTextPendingMessage
{
  guint id;
  time_t timestamp;
  TpHandle sender;
  TpChannelTextMessageType type;
  guint flags;
  gchar *text;

  nua_handle_t *nh;
};

typedef struct _TpsipTextChannelPrivate TpsipTextChannelPrivate;

struct _TpsipTextChannelPrivate
{
  TpsipConnection *conn;
  gchar *object_path;
  TpHandle handle;
  TpHandle initiator;

  guint recv_id;
  guint sent_id;
  GQueue  *pending_messages;
  GQueue  *sending_messages;

  gboolean closed;

  gboolean dispose_has_run;
};


#define _tpsip_text_pending_new0() \
	(g_slice_new0(TpsipTextPendingMessage))

#define TPSIP_TEXT_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_TEXT_CHANNEL, TpsipTextChannelPrivate))

static void tpsip_text_pending_free (TpsipTextPendingMessage *msg,
                                     TpHandleRepoIface *contact_handles)
{
  if (msg->sender)
    tp_handle_unref (contact_handles, msg->sender);

  g_free (msg->text);

  if (msg->nh)
    nua_handle_unref (msg->nh);

  g_slice_free (TpsipTextPendingMessage, msg);
}

static void
tpsip_text_channel_init (TpsipTextChannel *obj)
{
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (obj);

  DEBUG("enter");

  priv->pending_messages = g_queue_new ();
  priv->sending_messages = g_queue_new ();
}

static void
tpsip_text_channel_constructed (GObject *obj)
{
  TpsipTextChannelPrivate *priv;
  TpBaseConnection *base_conn;
  TpHandleRepoIface *contact_handles;
  DBusGConnection *bus;
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (tpsip_text_channel_parent_class);

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);

  priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(TPSIP_TEXT_CHANNEL(obj));
  base_conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_handles, priv->handle);

  g_assert (priv->initiator != 0);
  tp_handle_ref (contact_handles, priv->initiator);

  tpsip_connection_connect_auth_handler (priv->conn, TPSIP_EVENT_TARGET (obj));

  g_signal_connect (obj,
                    "nua-event::nua_r_message",
                    G_CALLBACK (tpsip_text_channel_nua_r_message_cb),
                    NULL);

  bus = tp_get_bus();
  dbus_g_connection_register_g_object(bus, priv->object_path, obj);
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

  g_type_class_add_private (klass, sizeof (TpsipTextChannelPrivate));

  object_class->get_property = tpsip_text_channel_get_property;
  object_class->set_property = tpsip_text_channel_set_property;

  object_class->constructed = tpsip_text_channel_constructed;

  object_class->dispose = tpsip_text_channel_dispose;
  object_class->finalize = tpsip_text_channel_finalize;

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

  param_spec = g_param_spec_object("connection", "TpsipConnection object",
      "SIP connection object that owns this SIP media channel object.",
      TPSIP_TYPE_CONNECTION,
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
      G_STRUCT_OFFSET (TpsipTextChannelClass, dbus_props_class));
}

static void
tpsip_text_channel_get_property(GObject *object,
				guint property_id,
				GValue *value,
				GParamSpec *pspec)
{
  TpsipTextChannel *chan = TPSIP_TEXT_CHANNEL(object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(chan);
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
              NULL));
      break;

    case PROP_INTERFACES:
      g_value_set_static_boxed (value, tpsip_text_channel_interfaces);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
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
tpsip_text_channel_dispose(GObject *object)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
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

  if (G_OBJECT_CLASS (tpsip_text_channel_parent_class)->dispose)
    G_OBJECT_CLASS (tpsip_text_channel_parent_class)->dispose (object);
}

static void
zap_pending_messages (GQueue *pending_messages,
                      TpHandleRepoIface *contact_handles)
{
  g_queue_foreach (pending_messages,
      (GFunc) tpsip_text_pending_free, contact_handles);
  g_queue_clear (pending_messages);
}

static void
tpsip_text_channel_finalize(GObject *object)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (object);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* XXX: could have responded to the requests with e.g. 480,
   * but generating network traffic upon abnormal channel termination
   * does not sound like a good idea */
  DEBUG ("%u pending incoming messages",
      g_queue_get_length (priv->pending_messages));
  zap_pending_messages (priv->pending_messages, contact_handles);
  g_queue_free (priv->pending_messages);

  DEBUG ("%u pending outgoing message requests",
      g_queue_get_length (priv->sending_messages));
  zap_pending_messages (priv->sending_messages, contact_handles);
  g_queue_free (priv->sending_messages);

  g_free (priv->object_path);

  G_OBJECT_CLASS (tpsip_text_channel_parent_class)->finalize (object);
}

static gint tpsip_pending_message_compare(gconstpointer msg, gconstpointer id)
{
  TpsipTextPendingMessage *message = (TpsipTextPendingMessage *)(msg);
  return (message->id != GPOINTER_TO_UINT(id));
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
  TpsipTextChannel *chan = TPSIP_TEXT_CHANNEL (iface);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(chan);
  TpHandleRepoIface *contact_repo;
  GList **nodes;
  TpsipTextPendingMessage *msg;
  guint i;

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  nodes = g_new(GList *, ids->len);

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index(ids, guint, i);

      nodes[i] = g_queue_find_custom (priv->pending_messages,
                                      GUINT_TO_POINTER (id),
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

      tpsip_text_pending_free (msg, contact_repo);
    }

  g_free(nodes);

  tp_svc_channel_type_text_return_from_acknowledge_pending_messages (context);
}


/**
 * tpsip_text_channel_close
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
tpsip_text_channel_close (TpSvcChannel *iface,
                          DBusGMethodInvocation *context)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (iface);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE(self);

  if (priv->closed)
    {
      DEBUG ("already closed, doing nothing");
    }
  else
    {
      if (g_queue_is_empty (priv->pending_messages))
        {
          DEBUG ("actually closing, no pending messages");
          priv->closed = TRUE;
        }
      else
        {
          GList *cur;

          DEBUG ("not really closing, there are pending messages left");

          for (cur = g_queue_peek_head_link (priv->pending_messages);
               cur != NULL;
               cur = cur->next)
            {
              TpsipTextPendingMessage *msg = cur->data;
              msg->flags |= TP_CHANNEL_TEXT_MESSAGE_FLAG_RESCUED;
            }

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
 * tpsip_text_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface org.freedesktop.Telepathy.Channel.Interface.Destroyable
 */
static void
tpsip_text_channel_destroy (TpSvcChannelInterfaceDestroyable *iface,
                            DBusGMethodInvocation *context)
{
  TpsipTextChannel *self = TPSIP_TEXT_CHANNEL (iface);
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_handles;

  contact_handles = tp_base_connection_get_handles (
      (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

  /* Make sure there are no pending messages for Close to get excited about */
  zap_pending_messages (priv->pending_messages, contact_handles);

  /* Close() and Destroy() have the same signature, so we can safely
   * chain to the other function now */
  tpsip_text_channel_close ((TpSvcChannel *) self, context);
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
  DEBUG("enter");
  tp_svc_channel_return_from_get_interfaces (context,
      tpsip_text_channel_interfaces);
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
                          4, msg->flags,
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
      TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
          (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);
      while ((cur = g_queue_pop_head_link (priv->pending_messages)) != NULL)
        {
          TpsipTextPendingMessage * msg = (TpsipTextPendingMessage *) cur->data;
          tpsip_pending_message_list_add (messages, msg);
          tpsip_text_pending_free (msg, contact_repo);
        }
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
          "Invalid recipient" };

      WARNING ("invalid recipient handle %d", priv->handle);
      dbus_g_method_return_error (context, &invalid);
      return;
    }

  msg_nh = tpsip_conn_create_request_handle (priv->conn, priv->handle);
  if (msg_nh == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "Request creation failed" };
      dbus_g_method_return_error (context, &e);
      return;
    }

  tpsip_event_target_attach (msg_nh, (GObject *) self);

  nua_message(msg_nh,
	      SIPTAG_CONTENT_TYPE_STR("text/plain"),
	      SIPTAG_PAYLOAD_STR(text),
	      TAG_END());

  msg = _tpsip_text_pending_new0 ();
  msg->nh = msg_nh;
  msg->text = g_strdup(text);
  msg->type = type;
  msg->timestamp = time(NULL);

  g_queue_push_tail (priv->sending_messages, msg);

  DEBUG("message queued for delivery with timestamp %u", (guint)msg->timestamp);

  tp_svc_channel_type_text_return_from_send (context);
}

static gboolean
tpsip_text_channel_nua_r_message_cb (TpsipTextChannel *self,
                                     const TpsipNuaEvent *ev,
                                     tagi_t            tags[],
                                     gpointer          foo)
{
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (self);
  TpsipTextPendingMessage *msg;
  TpHandleRepoIface *contact_repo;
  TpChannelTextSendError send_error;
  GList *node;

  /* ignore provisional responses */
  if (ev->status < 200)
    return TRUE;

  node = g_queue_find_custom (priv->sending_messages,
                              ev->nua_handle,
			      tpsip_acknowledged_messages_compare);

  /* Shouldn't happen... */
  if (node == NULL)
    {
      WARNING ("message pending sent acknowledgement not found");
      return FALSE;
    }

  msg = (TpsipTextPendingMessage *)node->data;

  g_assert (msg != NULL);

  if (ev->status >= 200 && ev->status < 300)
    {
      DEBUG("message with timestamp %u delivered", (guint)msg->timestamp);
      tp_svc_channel_type_text_emit_sent (self,
          msg->timestamp, msg->type, msg->text);
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

    tp_svc_channel_type_text_emit_send_error (self,
	send_error, msg->timestamp, msg->type, msg->text);
  }

  g_queue_remove(priv->sending_messages, msg);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  tpsip_text_pending_free(msg, contact_repo);

  return TRUE;
}

void tpsip_text_channel_receive(TpsipTextChannel *chan,
                                nua_t *nua,
                                TpHandle sender,
                                const char *text,
                                gsize len)
{
  TpsipTextChannelPrivate *priv = TPSIP_TEXT_CHANNEL_GET_PRIVATE (chan);
  TpsipTextPendingMessage *msg;
  TpHandleRepoIface *contact_repo;

  msg = _tpsip_text_pending_new0 ();

  msg->id = priv->recv_id++;
  msg->timestamp = time(NULL);
  msg->sender = sender;
  msg->type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  msg->text = g_strndup (text, len);

  g_queue_push_tail(priv->pending_messages, msg);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  tp_handle_ref (contact_repo, sender);

  DEBUG("received message id %u, now %u pending",
        msg->id, g_queue_get_length (priv->pending_messages));

  tp_svc_channel_type_text_emit_received ((TpSvcChannelTypeText *)chan,
      msg->id, msg->timestamp, msg->sender, msg->type,
      0 /* flags */, msg->text);
}

static void
destroyable_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
  TpSvcChannelInterfaceDestroyableClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
    klass, tpsip_text_channel_##x)
  IMPLEMENT(destroy);
#undef IMPLEMENT
}

static void
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
      klass, tpsip_text_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
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
