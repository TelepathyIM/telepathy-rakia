/*
 * sip-media-channel.c - Source for SIPMediaChannel
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2007 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-channel).
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib.h>
#include <stdlib.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/svc-channel.h>

#include "sip-media-channel.h"
#include "media-factory.h"
#include "sip-connection.h"
#include "sip-media-session.h"
#include "sip-media-stream.h"

#include "telepathy-helpers.h"
#include "sip-connection-helpers.h"

#define DEBUG_FLAG SIP_DEBUG_MEDIA
#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);
static void dtmf_iface_init (gpointer, gpointer);
static void priv_group_mixin_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SIPMediaChannel, sip_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      priv_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DTMF,
      dtmf_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_FACTORY,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
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

const TpPropertySignature media_channel_property_signatures[NUM_TP_PROPS] =
{
    { "nat-traversal",          G_TYPE_STRING },
    { "stun-server",            G_TYPE_STRING },
    { "stun-port",              G_TYPE_UINT },
};

/* private structure */
typedef struct _SIPMediaChannelPrivate SIPMediaChannelPrivate;

struct _SIPMediaChannelPrivate
{
  gboolean dispose_has_run;
  gboolean closed;
  SIPConnection *conn;
  SIPMediaFactory *factory;
  SIPMediaSession *session;
  gchar *object_path;
};

#define SIP_MEDIA_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_CHANNEL, SIPMediaChannelPrivate))

DEFINE_TP_STRUCT_TYPE(sip_session_handler_type,
                      DBUS_TYPE_G_OBJECT_PATH,
                      G_TYPE_STRING)


/***********************************************************************
 * Set: Gobject interface
 ***********************************************************************/

static void
sip_media_channel_init (SIPMediaChannel *obj)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (obj);

  DEBUG("enter");

  /* allocate any data required by the object here */

  /* initialise the properties mixin *before* GObject
   * sets the construct-time properties */
  tp_properties_mixin_init (G_OBJECT (obj),
      G_STRUCT_OFFSET (SIPMediaChannel, properties));

  /* to keep the compiler happy */
  priv = NULL;
}

static GObject *
sip_media_channel_constructor (GType type, guint n_props,
			       GObjectConstructParam *props)
{
  GObject *obj;
  SIPMediaChannelPrivate *priv;
  DBusGConnection *bus;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;

  DEBUG("enter");
  
  obj = G_OBJECT_CLASS (sip_media_channel_parent_class)->
           constructor (type, n_props, props);

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (SIP_MEDIA_CHANNEL (obj));
  conn = (TpBaseConnection *)(priv->conn);
  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  
  /* register object on the bus */
  bus = tp_get_bus ();

  DEBUG("registering object to dbus path=%s", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init (obj,
                       G_STRUCT_OFFSET (SIPMediaChannel, group),
                       contact_repo,
                       conn->self_handle);

  /* allow member adding */
  tp_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_CAN_ADD, 0);

  return obj;
}

static void sip_media_channel_dispose (GObject *object);
static void sip_media_channel_finalize (GObject *object);
static void sip_media_channel_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec);
static void sip_media_channel_set_property (GObject     *object,
					    guint        property_id,
					    const GValue *value,
					    GParamSpec   *pspec);

static void priv_create_session (SIPMediaChannel *channel,
                                 nua_handle_t *nh,
                                 TpHandle peer,
                                 gboolean remote_initiated);
static void priv_destroy_session(SIPMediaChannel *channel);
gboolean sip_media_channel_add_member (GObject *iface,
                                       TpHandle handle,
                                       const gchar *message,
                                       GError **error);
static gboolean sip_media_channel_remove_with_reason (
                                                 GObject *iface,
                                                 TpHandle handle,
                                                 const gchar *message,
                                                 guint reason,
                                                 GError **error);

static void
sip_media_channel_class_init (SIPMediaChannelClass *sip_media_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_media_channel_class);
  GParamSpec *param_spec;

  DEBUG("enter");

  g_type_class_add_private (sip_media_channel_class, sizeof (SIPMediaChannelPrivate));

  object_class->constructor = sip_media_channel_constructor;

  object_class->get_property = sip_media_channel_get_property;
  object_class->set_property = sip_media_channel_set_property;

  object_class->dispose = sip_media_channel_dispose;
  object_class->finalize = sip_media_channel_finalize;

  tp_group_mixin_class_init (object_class,
                             G_STRUCT_OFFSET (SIPMediaChannelClass, group_class),
                             sip_media_channel_add_member,
                             NULL);
  tp_group_mixin_class_set_remove_with_reason_func(object_class,
                             sip_media_channel_remove_with_reason);

  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  tp_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (SIPMediaChannelClass, properties_class),
      media_channel_property_signatures, NUM_TP_PROPS, NULL);

  param_spec = g_param_spec_object ("connection", "SIPConnection object",
                                    "SIP connection object that owns this "
                                    "SIP media channel object.",
                                    SIP_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_object ("factory", "SIPMediaFactory object",
                                    "Channel factory object that owns this "
                                    "SIP media channel object.",
                                    SIP_TYPE_MEDIA_FACTORY,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FACTORY, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("channel-type", "Telepathy channel type",
                                    "The D-Bus interface representing the "
                                    "type of this channel.",
                                    NULL,
                                    G_PARAM_READABLE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CHANNEL_TYPE, param_spec);

  param_spec = g_param_spec_string ("nat-traversal", "NAT traversal mechanism",
                                    "A string representing the type of NAT "
                                    "traversal that should be performed for "
                                    "streams on this channel.",
                                    "none",
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL, param_spec);

  param_spec = g_param_spec_string ("stun-server",
                                    "STUN server",
                                    "IP or address of STUN server.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port",
                                  "STUN port",
                                  "UDP port of STUN server.",
                                  0, G_MAXUINT16, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);
}

static void
sip_media_channel_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SIPMediaChannel *chan = SIP_MEDIA_CHANNEL (object);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_FACTORY:
      g_value_set_object (value, priv->factory);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_HANDLE:
      g_value_set_uint (value, 0);
      break;
    case PROP_HANDLE_TYPE:
      g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    default:
      /* the NAT_TRAVERSAL property lives in the mixin */
      {
        const gchar *param_name = g_param_spec_get_name (pspec);
        guint tp_property_id;

        if (tp_properties_mixin_has_property (object, param_name,
              &tp_property_id))
          {
            GValue *tp_property_value =
              chan->properties.properties[tp_property_id].value;

            if (tp_property_value)
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
sip_media_channel_set_property (GObject     *object,
				guint        property_id,
				const GValue *value,
				GParamSpec   *pspec)
{
  SIPMediaChannel *chan = SIP_MEDIA_CHANNEL (object);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (chan);

  switch (property_id) {
    case PROP_HANDLE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_HANDLE_TYPE:
      /* this property is writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = SIP_CONNECTION (g_value_dup_object (value));
      break;
    case PROP_FACTORY:
      priv->factory = SIP_MEDIA_FACTORY (g_value_dup_object (value));
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    default:
      /* the NAT_TRAVERSAL property lives in the mixin */
      {
        const gchar *param_name = g_param_spec_get_name (pspec);
        guint tp_property_id;

        if (tp_properties_mixin_has_property (object, param_name,
              &tp_property_id))
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

void
sip_media_channel_dispose (GObject *object)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (object);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  DEBUG("enter");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  priv_destroy_session(self);

  if (!priv->closed)
    sip_media_channel_close (self);

  if (priv->factory)
    g_object_unref (priv->factory);

  if (priv->conn)
    g_object_unref (priv->conn);

  if (G_OBJECT_CLASS (sip_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_channel_parent_class)->dispose (object);

  DEBUG ("exit");
}

void
sip_media_channel_finalize (GObject *object)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (object);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  g_free (priv->object_path);

  tp_group_mixin_finalize (object);

  tp_properties_mixin_finalize (object);

  G_OBJECT_CLASS (sip_media_channel_parent_class)->finalize (object);

  DEBUG ("exit");
}

/***********************************************************************
 * Set: Channel interface implementation (same for 0.12/0.13)
 ***********************************************************************/

/**
 * sip_media_channel_close_async
 *
 * Implements DBus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_media_channel_close_async (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);

  sip_media_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

void
sip_media_channel_close (SIPMediaChannel *obj)
{
  SIPMediaChannelPrivate *priv;

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (obj));
  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (obj);

  if (priv->closed)
    return;

  priv->closed = TRUE;

  if (priv->session) {
    sip_media_session_terminate (priv->session);
    g_assert (priv->session == NULL);
  }

  tp_svc_channel_emit_closed ((TpSvcChannel *)obj);

  return;
}

void
sip_media_channel_terminated (SIPMediaChannel *self)
{
  SIPMediaChannelPrivate *priv;

  DEBUG("enter");

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session)
    {
      g_object_set (priv->session,
                    "state", SIP_MEDIA_SESSION_STATE_ENDED,
                    NULL);
    }
}

/**
 * sip_media_channel_get_channel_type
 *
 * Implements DBus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_media_channel_get_channel_type (TpSvcChannel *obj,
                                    DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
}


/**
 * sip_media_channel_get_handle
 *
 * Implements DBus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_media_channel_get_handle (TpSvcChannel *iface,
                              DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_handle (context, 0, 0);
}

/**
 * sip_media_channel_get_interfaces
 *
 * Implements DBus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
sip_media_channel_get_interfaces (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
  const gchar *interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
    TP_IFACE_CHANNEL_INTERFACE_DTMF,
    TP_IFACE_PROPERTIES_INTERFACE,
    NULL
  };

  tp_svc_channel_return_from_get_interfaces (context, interfaces);
}

/***********************************************************************
 * Set: Channel.Interface.MediaSignalling Telepathy-0.13 interface 
 ***********************************************************************/

/**
 * sip_media_channel_get_session_handlers
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
sip_media_channel_get_session_handlers (TpSvcChannelInterfaceMediaSignalling *iface,
                                        DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  SIPMediaChannelPrivate *priv;
  GPtrArray *ret;
  GValue handler = { 0 };

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (self));

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  ret = g_ptr_array_new ();

  if (priv->session)
    {
      GType handler_type;
      gchar *path;

      g_object_get (priv->session,
                    "object-path", &path,
                    NULL);

      handler_type = sip_session_handler_type ();

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
 * sip_media_channel_list_streams
 *
 * Implements D-Bus method ListStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
sip_media_channel_list_streams (TpSvcChannelTypeStreamedMedia *iface,
                                DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  SIPMediaChannelPrivate *priv;
  GPtrArray *ret = NULL;

  g_assert (SIP_IS_MEDIA_CHANNEL (self));
  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!sip_media_session_list_streams (priv->session, &ret))
    ret = g_ptr_array_new ();

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);

  g_boxed_free (SIP_TP_STREAM_LIST_TYPE, ret);
}

/**
 * sip_media_channel_remove_streams
 *
 * Implements D-Bus method RemoveStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
sip_media_channel_remove_streams (TpSvcChannelTypeStreamedMedia *self,
                                  const GArray *streams,
                                  DBusGMethodInvocation *context)
{
  /* FIXME: stub */
  tp_svc_channel_type_streamed_media_return_from_remove_streams (context);
}

/**
 * sip_media_channel_request_stream_direction
 *
 * Implements D-Bus method RequestStreamDirection
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
sip_media_channel_request_stream_direction (TpSvcChannelTypeStreamedMedia *iface,
                                            guint stream_id,
                                            guint stream_direction,
                                            DBusGMethodInvocation *context)
{
  /* XXX: requires performing a re-INVITE either disabling a
   * a media, or putting it to hold */
  GError error = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
		       "RequestStreamDirection not implemented" };
  g_debug ("%s: not implemented", G_STRFUNC);
  dbus_g_method_return_error (context, &error);
}


/**
 * sip_media_channel_request_streams
 *
 * Implements D-Bus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
sip_media_channel_request_streams (TpSvcChannelTypeStreamedMedia *iface,
                                   guint contact_handle,
                                   const GArray *types,
                                   DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  GError *error = NULL;
  GPtrArray *ret = NULL;
  SIPMediaChannelPrivate *priv;
  TpHandleRepoIface *contact_repo;

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (self));

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (contact_repo, contact_handle, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  if (!tp_handle_set_is_member (self->group.members, contact_handle) &&
      !tp_handle_set_is_member (self->group.remote_pending, contact_handle))
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT, "given handle "
			    "%u is not a member of the channel", contact_handle);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  /* if the person is a channel member, we should have a session */
  g_assert (priv->session != NULL);

  if (!sip_media_session_request_streams (priv->session, types, &ret, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  g_assert (types->len == ret->len);

  tp_svc_channel_type_streamed_media_return_from_request_streams (context, ret);

  g_boxed_free (SIP_TP_STREAM_LIST_TYPE, ret);

  DEBUG ("exit");
}

/***********************************************************************
 * Set: sip-media-channel API towards sip-connection
 ***********************************************************************/

/**
 * Handle an incoming INVITE, normally called just after the channel
 * has been created.
 */
void
sip_media_channel_receive_invite (SIPMediaChannel *self, 
                                  nua_handle_t *nh,
                                  TpHandle handle)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  GObject *obj = G_OBJECT (self);
  TpHandleRepoIface *contact_repo;
  TpIntSet *member_set, *pending_set;
 
  contact_repo = tp_base_connection_get_handles (
        (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);

  if (priv->session == NULL)
    {
      priv_create_session (self, nh, handle, TRUE);
    
      /* note: start the local stream-engine; once the local 
       *       candidate are ready, reply with nua_respond() 
       *
       *       with the tp-0.13 API, the streams need to be created
       *       based on remote SDP (see sip_media_session_set_remote_info()) */
    }
  else
    g_warning ("session already exists");

  /* XXX: should be attached more data than just the handle? 
   * - yes, we need to be able to access all the <op,handle> pairs */

  DEBUG("adding handle %d (%s)", 
        handle,
        tp_handle_inspect (contact_repo, handle));

  /* add the peer to channel members and self_handle to local pending */

  member_set = tp_intset_new ();
  tp_intset_add (member_set, handle);

  pending_set = tp_intset_new ();
  tp_intset_add (pending_set, mixin->self_handle);

  tp_group_mixin_change_members (obj, "INVITE received",
                                 member_set,    /* add */
                                 NULL,          /* remove */
                                 pending_set,   /* local pending */
                                 NULL,          /* remote pending */
                                 0, 0);         /* irrelevant */

  tp_intset_destroy (member_set);
  tp_intset_destroy (pending_set);
}

/**
 * Handle an incoming re-INVITE request.
 */
void
sip_media_channel_receive_reinvite (SIPMediaChannel *self)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  g_return_if_fail (priv->session != NULL);

  sip_media_session_receive_reinvite (priv->session);
}

gboolean
sip_media_channel_set_remote_info (SIPMediaChannel *chan,
                                   const sdp_session_t* r_sdp)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (chan);
  gboolean res = FALSE;

  DEBUG("enter");

  if (priv->session) {
    res = sip_media_session_set_remote_info (priv->session, r_sdp);
  }

  DEBUG ("exit");

  return res;
}

void
sip_media_channel_peer_error (SIPMediaChannel *self,
                              guint status,
                              const char* message)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpIntSet *set;
  TpHandle peer;
  guint reason = TP_CHANNEL_GROUP_CHANGE_REASON_ERROR;
 
  DEBUG("peer responded with error %u %s", status, message);

  g_return_if_fail (priv->session != NULL);

  switch (status)
    {
    case 404:
    case 410:
    case 604:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_INVALID_CONTACT;
      break;
    case 486:
    case 600:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_BUSY;
      break;
    case 408:
    case 480:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER;
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

  peer = sip_media_session_get_peer (priv->session);

  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_group_mixin_change_members ((GObject *)self, message,
      NULL, set, NULL, NULL, peer, reason);
  tp_intset_destroy (set);
}

void
sip_media_channel_peer_cancel (SIPMediaChannel *self)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  TpIntSet *set;
  TpHandle peer;

  g_return_if_fail (priv->session != NULL);

  peer = sip_media_session_get_peer (priv->session);

  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_intset_add (set, mixin->self_handle);
  tp_group_mixin_change_members ((GObject *) self, "Cancelled",
      NULL, set, NULL, NULL, peer, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (set);
}

/***********************************************************************
 * Set: Helper functions follow (not based on generated templates)
 ***********************************************************************/

static void priv_session_state_changed_cb (SIPMediaSession *session,
					   GParamSpec *arg1,
					   SIPMediaChannel *channel)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (channel);
  SIPMediaSessionState state;
  TpHandle peer;
  TpIntSet *set;

  DEBUG("enter");

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  set = tp_intset_new ();

  if (state == SIP_MEDIA_SESSION_STATE_ACTIVE) {
    /* add the peer to the member list */
    tp_intset_add (set, peer);

    tp_group_mixin_change_members ((GObject *)channel,
        "", set, NULL, NULL, NULL, 0, 0);

    /* update flags accordingly -- allow removal, deny adding and rescinding */
    tp_group_mixin_change_flags ((GObject *)channel,
				     TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
				     TP_CHANNEL_GROUP_FLAG_CAN_ADD |
				     TP_CHANNEL_GROUP_FLAG_CAN_RESCIND);
  }
  else if (state == SIP_MEDIA_SESSION_STATE_ENDED) {
    /* remove us and the peer from the member list */
    tp_intset_add (set, mixin->self_handle);
    tp_intset_add (set, peer);
    tp_group_mixin_change_members ((GObject *)channel,
        "", NULL, set, NULL, NULL, 0, 0);

#if 0
    /* update flags accordingly -- allow adding, deny removal */
    tp_group_mixin_change_flags ((GObject *)channel,
        TP_CHANNEL_GROUP_FLAG_CAN_ADD, TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);
#endif

    priv_destroy_session (channel);
    sip_media_channel_close (channel);
  }

  tp_intset_destroy (set);

  DEBUG ("exit");
}

static void priv_session_stream_added_cb (SIPMediaSession *session,
					  SIPMediaStream  *stream,
					  SIPMediaChannel *chan)
{
  guint id, handle, type;

  DEBUG("enter");

  /* emit StreamAdded */
  handle = sip_media_session_get_peer (session);
  g_object_get (stream, "id", &id, "media-type", &type, NULL);

  tp_svc_channel_type_streamed_media_emit_stream_added (
        (TpSvcChannelTypeStreamedMedia *)chan, id, handle, type);
}

/**
 * priv_create_session:
 *
 * Creates a SIPMediaSession object for given peer.
 **/
static void
priv_create_session (SIPMediaChannel *channel,
                     nua_handle_t *nh,
                     TpHandle peer,
                     gboolean remote_initiated)
{
  SIPMediaChannelPrivate *priv;
  SIPMediaSession *session;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;
  gchar *object_path;
  const gchar *sid = NULL;

  DEBUG("enter");

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  g_assert (priv->session == NULL);
  conn = (TpBaseConnection *)(priv->conn);
  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  /* allocate a hash-entry for the new media session */
  sid = sip_media_factory_session_id_allocate (priv->factory);

  DEBUG("allocating session, peer=%u", peer);

  /* The channel manages references to the peer handle for the session */
  tp_handle_ref (contact_repo, peer);

  session = g_object_new (SIP_TYPE_MEDIA_SESSION,
                          "media-channel", channel,
                          "object-path", object_path,
                          "nua-handle", nh,
                          "session-id", sid,
                          "peer", peer,
                          NULL);

  g_signal_connect (session, "notify::state",
                    (GCallback) priv_session_state_changed_cb, channel);
  g_signal_connect (session, "stream-added",
		    (GCallback) priv_session_stream_added_cb, channel);

  priv->session = session;

  /* keep a list of media session ids */
  sip_media_factory_session_id_register (priv->factory, sid, channel);

  if (remote_initiated)
    sip_media_session_receive_invite (session);

  tp_svc_channel_interface_media_signalling_emit_new_session_handler (
      (TpSvcChannelInterfaceMediaSignalling *)channel, object_path, "rtp");

  g_free (object_path);

  DEBUG ("exit");
}

static void
priv_destroy_session(SIPMediaChannel *channel)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  SIPMediaSession *session;
  TpBaseConnection *conn;
  TpHandleRepoIface *contact_repo;
  gchar *sid = NULL;

  DEBUG("enter");

  session = priv->session;
  if (session == NULL)
    return;

  g_object_get (session, "session-id", &sid, NULL);
  sip_media_factory_session_id_unregister(priv->factory, sid);
  g_free (sid);

  /* Release the peer handle */
  conn = (TpBaseConnection *)(priv->conn);
  contact_repo = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  tp_handle_unref (contact_repo, sip_media_session_get_peer (session));

  priv->session = NULL;
  g_object_unref (session);
}


/* Check that self_handle is not already in the members. If it is,
 * we're trying to call ourselves. */
static void
priv_add_members (TpSvcChannelInterfaceGroup *obj,
                    const GArray *contacts,
                    const gchar *message,
                    DBusGMethodInvocation *context)
{
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  guint i;
  TpHandle handle;
  GError *error = NULL;

  for (i = 0; i < contacts->len; i++)
    {
      handle = g_array_index (contacts, TpHandle, i);

      if (handle == mixin->self_handle &&
          tp_handle_set_is_member (mixin->members, handle))
        {
          DEBUG ("attempted to add self_handle into the mixin again");
          g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_HANDLE,
            "you cannot call yourself");
        }
    }

  if (error == NULL)
      tp_group_mixin_add_members ((GObject *) obj, contacts, message, &error);

  if (error == NULL)
    {
      tp_svc_channel_interface_group_return_from_add_members (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

gboolean
sip_media_channel_add_member (GObject *iface,
                              TpHandle handle,
                              const gchar *message,
                              GError **error)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (iface);

  DEBUG("mixin->self_handle=%d, handle=%d", mixin->self_handle, handle);

  /* case a: outgoing call (we are the initiator, a new handle added) */
  if (mixin->self_handle != handle)
    {
      TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
      TpIntSet *lset, *rset;
      nua_handle_t *nh;

      DEBUG("making outbound call - setting peer handle to %u", handle);

      nh = sip_conn_create_request_handle (priv->conn, handle);
      priv_create_session (self, nh, handle, FALSE);
      nua_handle_unref (nh);

      /* make remote pending */
      rset = tp_intset_new ();
      lset = tp_intset_new ();
      tp_intset_add (lset, mixin->self_handle);
      tp_intset_add (rset, handle);
      tp_group_mixin_change_members (iface, "Sending INVITE",
                                     lset,      /* add */
                                     NULL,      /* remove */
                                     NULL,      /* local pending */
                                     rset,      /* remote pending */
                                     0, 0);
      tp_intset_destroy (lset);
      tp_intset_destroy (rset);

      /* and update flags accordingly */
      tp_group_mixin_change_flags (iface,
        TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
        TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
        0);

      return TRUE;
    }
  /* case b: an incoming invite */
  else if (priv->session &&
	   tp_handle_set_is_member (mixin->local_pending, handle))
    {
      TpIntSet *set;

      DEBUG("accepting an incoming invite");

      g_assert (handle == mixin->self_handle);

      set = tp_intset_new ();
      tp_intset_add (set, handle);
      tp_group_mixin_change_members (iface, "Incoming call accepted",
                                     set,       /* add */
                                     NULL,      /* remove */
                                     NULL,      /* local pending */
                                     NULL,      /* remote pending */
                                     0, 0);
      tp_intset_destroy (set);

      sip_media_session_accept (priv->session);

      return TRUE;
    }

  g_assert_not_reached();

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
               "Can't map this member change to protocol behavior");
  return FALSE;
}

static gint
sip_status_from_tp_reason (TpChannelGroupChangeReason reason)
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
sip_media_channel_remove_with_reason (GObject *obj,
                                      TpHandle handle,
                                      const gchar *message,
                                      guint reason,
                                      GError **error)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (obj);
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  /* We handle only one case: removal of the self handle from local pending
   * due to the user rejecting the call */
  if (priv->session &&
      handle == mixin->self_handle &&
      tp_handle_set_is_member (mixin->local_pending, handle))
    {
      TpIntSet *set;
      gint status;

      status = sip_status_from_tp_reason (reason);

      /* XXX: raise NotAvailable if it's the wrong state? */
      sip_media_session_reject (priv->session, status, message);

      set = tp_intset_new ();
      tp_intset_add (set, handle);
      tp_group_mixin_change_members (obj,
                                     message,
                                     NULL, /* add */ 
                                     set,  /* remove */
                                     NULL, /* add local pending */
                                     NULL, /* add remote pending */ 
                                     0,    /* actor */
                                     reason);
      tp_intset_destroy (set);

      /* no more adding to this channel */
      tp_group_mixin_change_flags (obj,
                                   0,
                                   TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return TRUE;
    }

  g_assert_not_reached();

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
               "Can't map this member change to protocol behavior");
  return FALSE;
}

static void
sip_media_channel_start_tone (TpSvcChannelInterfaceDTMF *iface,
                              guint stream_id,
                              guchar event,
                              DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  SIPMediaChannelPrivate *priv;
  GError *error = NULL;

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (self));

  if (event >= NUM_TP_DTMF_EVENTS)
    {
      g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "event %u is not a known DTMF event", event);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!sip_media_session_start_telephony_event (priv->session,
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
sip_media_channel_stop_tone (TpSvcChannelInterfaceDTMF *iface,
                             guint stream_id,
                             DBusGMethodInvocation *context)
{
  SIPMediaChannel *self = SIP_MEDIA_CHANNEL (iface);
  SIPMediaChannelPrivate *priv;
  GError *error = NULL;

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (self));

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (!sip_media_session_stop_telephony_event (priv->session,
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
channel_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, sip_media_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
streamed_media_iface_init(gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeStreamedMediaClass *klass = (TpSvcChannelTypeStreamedMediaClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_streamed_media_implement_##x (\
    klass, sip_media_channel_##x)
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
    klass, sip_media_channel_##x)
  IMPLEMENT(get_session_handlers);
#undef IMPLEMENT
}

static void
dtmf_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceDTMFClass *klass = (TpSvcChannelInterfaceDTMFClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_dtmf_implement_##x (\
    klass, sip_media_channel_##x)
  IMPLEMENT(start_tone);
  IMPLEMENT(stop_tone);
#undef IMPLEMENT
}

static void
priv_group_mixin_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceGroupClass *klass =
      (TpSvcChannelInterfaceGroupClass *)g_iface;

  tp_group_mixin_iface_init (g_iface, iface_data);

  tp_svc_channel_interface_group_implement_add_members (klass,
      priv_add_members);
}

