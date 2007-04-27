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

/* FIXME: take this out and depend on telepathy-glib >= 0.5.8 instead, after
 * it's released */
#define _TP_CM_UPDATED_FOR_0_5_7

#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/intset.h>
#include <telepathy-glib/svc-channel.h>

#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "sip-media-session.h"
#include "sip-media-stream.h"

#include "sip-media-channel.h"
#include "media-factory.h"
#include "signals-marshal.h"

#define DEBUG_FLAG SIP_DEBUG_MEDIA
#include "debug.h"

static void channel_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);
static void dtmf_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SIPMediaChannel, sip_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DTMF,
      dtmf_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_PROPERTIES_INTERFACE,
      tp_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

#define TP_SESSION_HANDLER_SET_TYPE (dbus_g_type_get_struct ("GValueArray", \
      DBUS_TYPE_G_OBJECT_PATH, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))

#define TP_CHANNEL_STREAM_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_INVALID))

/* signal enum */
enum
{
    SIG_NEW_MEDIA_SESSION_HANDLER = 1,
    SIG_PROPERTIES_CHANGED,
    SIG_PROPERTY_FLAGS_CHANGED,
    SIG_LAST
};

static guint signals[SIG_LAST] = {0};

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_FACTORY,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_CREATOR,
  PROP_NUA_OP,
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
  TpHandle creator;
  nua_handle_t *nua_op;
};

#define SIP_MEDIA_CHANNEL_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_CHANNEL, SIPMediaChannelPrivate))

static GPtrArray *priv_make_stream_list (SIPMediaChannel *self, GPtrArray *streams);

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

  g_debug("registering object to dbus path=%s.\n", priv->object_path);
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init (obj,
                       G_STRUCT_OFFSET (SIPMediaChannel, group),
                       contact_repo,
                       conn->self_handle);

  /* automatically add creator to channel, if defined */
  if (priv->creator) {
    TpIntSet *set = tp_intset_new ();
    tp_intset_add (set, priv->creator);
    tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL, 0, 0);
    tp_intset_destroy (set);
  }

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
                                 TpHandle peer,
                                 const gchar *sid);
static void priv_destroy_session(SIPMediaChannel *channel);
gboolean sip_media_channel_add_member (GObject *iface,
                                       TpHandle handle,
                                       const gchar *message,
                                       GError **error);
static gboolean priv_media_channel_remove_member (GObject *iface,
                                                  TpHandle handle,
                                                  const gchar *message,
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
                             priv_media_channel_remove_member);

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

  param_spec = g_param_spec_uint ("creator", "Channel creator",
                                  "The TpHandle representing the contact "
                                  "who created the channel.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_pointer("nua-handle", "Sofia-SIP NUA operator handle",
                                    "Handle associated with this media channel.",
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NUA_OP, param_spec);

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

  signals[SIG_NEW_MEDIA_SESSION_HANDLER] =
    g_signal_new ("new-media-session-handler",
                  G_OBJECT_CLASS_TYPE (sip_media_channel_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tpsip_marshal_VOID__INT_STRING_STRING,
                  G_TYPE_NONE, 3, G_TYPE_UINT, DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING);
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
    case PROP_CREATOR:
      g_value_set_uint (value, priv->creator);
      break;
    case PROP_NUA_OP:
      g_value_set_pointer (value, priv->nua_op);
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
      priv->conn = g_value_get_object (value);
      break;
    case PROP_FACTORY:
      priv->factory = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_uint (value);
      break;
    case PROP_NUA_OP:
      {
        nua_handle_t *new_nua_op = g_value_get_pointer (value);

        g_debug ("%s: channel %p: assigning NUA handle %p", G_STRFUNC, object,
            new_nua_op);

        /* you can only set the NUA handle once - migrating a media channel
         * between two NUA handles makes no sense */
        g_return_if_fail (priv->nua_op == NULL);
        /* migrating a NUA handle between two active media channels
         * makes no sense either */
        if (new_nua_op)
          {
            nua_hmagic_t *nua_op_chan = nua_handle_magic (new_nua_op);

            g_return_if_fail (nua_op_chan == NULL || nua_op_chan == chan);
          }

        priv->nua_op = new_nua_op;
        /* tell the NUA that we're handling this call */
        nua_handle_bind (priv->nua_op, chan);
      }
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

  /* closing the channel should have discarded the NUA handle */
  g_assert (priv->nua_op == NULL);

  if (G_OBJECT_CLASS (sip_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_channel_parent_class)->dispose (object);

  DEBUG ("exit");
}

void
sip_media_channel_finalize (GObject *object)
{
  /* free any data held directly by the object here */
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

  if (priv->nua_op)
    {
      g_assert (nua_handle_magic (priv->nua_op) == obj);
      nua_handle_bind (priv->nua_op, NULL);
      priv->nua_op = NULL;
    }

  tp_svc_channel_emit_closed ((TpSvcChannel *)obj);

  return;
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

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (self));

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (priv->session) {
    GValue handler = { 0, };
    TpHandle member;
    gchar *path;

    g_value_init (&handler, TP_SESSION_HANDLER_SET_TYPE);
    g_value_set_static_boxed (&handler,
        dbus_g_type_specialized_construct (TP_SESSION_HANDLER_SET_TYPE));

    g_object_get (priv->session,
		  "peer", &member,
		  "object-path", &path,
		  NULL);

    dbus_g_type_struct_set (&handler,
			    0, path,
			    1, "rtp",
			    G_MAXUINT);

    g_free (path);
    
    ret = g_ptr_array_sized_new (1);
    g_ptr_array_add (ret, g_value_get_boxed (&handler));
  }
  else {
    ret = g_ptr_array_sized_new (0);
  }

  tp_svc_channel_interface_media_signalling_return_from_get_session_handlers (
      context, ret);
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
  const GType stream_type = TP_CHANNEL_STREAM_TYPE;
  GPtrArray *streams = NULL;
  GPtrArray *ret;
  int i;

  g_assert (SIP_IS_MEDIA_CHANNEL (self));
  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);

  if (sip_media_session_list_streams (priv->session, &streams)) {
    ret = priv_make_stream_list (self, streams);
    g_ptr_array_free (streams, TRUE);
  }
  else {
    ret = g_ptr_array_new ();
  }

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);

  for (i = 0; i < ret->len; i++)
    g_boxed_free (stream_type, g_ptr_array_index (ret, i));
  g_ptr_array_free (ret, TRUE);
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
  GPtrArray *ret;
  SIPMediaChannelPrivate *priv;
  TpHandleRepoIface *contact_repo;

  GPtrArray *streams;

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

  if (!sip_media_session_request_streams (priv->session, types, &streams, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = priv_make_stream_list (self, streams);

  g_assert(types->len == (ret)->len);

  g_ptr_array_free (streams, TRUE);

  tp_svc_channel_type_streamed_media_return_from_request_streams (context, ret);
  g_ptr_array_free (ret, TRUE);
  DEBUG ("exit");
}

static GPtrArray *priv_make_stream_list (SIPMediaChannel *self, GPtrArray *streams)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  const GType stream_type = TP_CHANNEL_STREAM_TYPE;
  GPtrArray *ret;
  guint i;

  DEBUG("enter");

  ret = g_ptr_array_sized_new (streams->len);

  for (i = 0; i < streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index (streams, i);
    GValue entry = { 0, };
    guint id;
    TpHandle peer;
    TpMediaStreamType type = TP_MEDIA_STREAM_TYPE_AUDIO;
    TpMediaStreamState connection_state = TP_MEDIA_STREAM_STATE_CONNECTED;
    /* CombinedStreamDirection combined_direction; */

    /* note: removed streams are kept in the ptr-array as NULL
     *       items (one cannot remove m-lines in SDP negotiation)
     */

    if (stream == NULL)
      continue;

    g_object_get (stream,
		  "id", &id,
		  "media-type", &type,
		  /* XXX: add to sip-stream -> "connection-state", &connection_state, */
		  /* "combined-direction", &combined_direction,*/
		  NULL);

    if (id != i)
      g_warning("%s: strange stream id %d, should be %d", G_STRFUNC, id, i);

    g_assert (priv->session);
    peer = sip_media_session_get_peer (priv->session);

    g_value_init (&entry, stream_type);
    g_value_take_boxed (&entry,
			dbus_g_type_specialized_construct (stream_type));

    dbus_g_type_struct_set (&entry,
			    0, id,
			    1, peer,
			    2, type,
			    3, connection_state,
			    4, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
			    5, 0,   /* no pending send */
			    G_MAXUINT);

    g_ptr_array_add (ret, g_value_get_boxed (&entry));
  }

  DEBUG ("exit");

  return ret;
}

/***********************************************************************
 * Set: sip-media-channel API towards sip-connection
 ***********************************************************************/

/**
 * Invite the given handle to this channel
 */
void sip_media_channel_respond_to_invite (SIPMediaChannel *self, 
					  TpHandle handle,
					  const char *subject,
					  const char *remoteurl)
{
  SIPMediaChannelPrivate *priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (self);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)(priv->conn), TP_HANDLE_TYPE_CONTACT);
  TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
  GObject *obj = G_OBJECT (self);
  TpIntSet *set;
 
  DEBUG("enter");

  priv->creator = handle;

  g_message ("%s: adding handle %d (%s)", 
	     G_STRFUNC,
             handle,
	     tp_handle_inspect (contact_repo, handle));

  set = tp_intset_new ();
  tp_intset_add (set, handle);
  tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL, 0, 0);
  tp_intset_destroy (set);

  if (priv->session == NULL) {
    priv_create_session(self, handle, remoteurl);
    
    /* note: start the local stream-engine; once the local 
     *       candidate are ready, reply with nua_respond() 
     *
     *       with the tp-0.13 API, the streams need to be created
     *       based on remote SDP (see sip_media_session_set_remote_info()) */
  }

  /* XXX: should be attached more data than just the handle? 
   * - yes, we need to be able to access all the <op,handle> pairs */

  /* add self_handle to local pending */
  set = tp_intset_new ();
  tp_intset_add (set, mixin->self_handle);
  tp_group_mixin_change_members (obj, "", NULL, NULL, set, NULL, 0, 0);
  tp_intset_destroy (set);
}

gboolean
sip_media_channel_set_remote_info (SIPMediaChannel *chan, const char* r_sdp)
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

void sip_media_channel_stream_state (SIPMediaChannel *chan,
                                     guint id,
                                     guint state)
{
  tp_svc_channel_type_streamed_media_emit_stream_state_changed(
      (TpSvcChannelTypeStreamedMedia *)chan, id, state);
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
 
  DEBUG("enter");

  g_message ("%s: peer responded with error %u %s",
	     G_STRFUNC,
	     status,
	     message);

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
    /*
    case 401:
    case 403:
    case 407:
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_?;
      break;
    */
    }

  peer = sip_media_session_get_peer (priv->session);

  set = tp_intset_new ();
  tp_intset_add (set, peer);
  tp_group_mixin_change_members ((GObject *)self, message,
      NULL, set, NULL, NULL, 0, reason);
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
  JingleSessionState state;
  TpHandle peer;
  TpIntSet *set;

  DEBUG("enter");

  g_object_get (session,
                "state", &state,
                "peer", &peer,
                NULL);

  set = tp_intset_new ();

  if (state == JS_STATE_ACTIVE) {
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
  else if (state == JS_STATE_ENDED) {
    /* remove us and the peer from the member list */
    tp_intset_add (set, mixin->self_handle);
    tp_intset_add (set, peer);
    tp_group_mixin_change_members ((GObject *)channel,
        "", NULL, set, NULL, NULL, 0, 0);

    /* update flags accordingly -- allow adding, deny removal */
    tp_group_mixin_change_flags ((GObject *)channel,
        TP_CHANNEL_GROUP_FLAG_CAN_ADD, TP_CHANNEL_GROUP_FLAG_CAN_REMOVE);

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
 * create_session
 *
 * Creates a SIPMediaSession object for given peer.
 *
 * If "remoteurl" is set to NULL, a unique session identifier is 
 * generated and the "initiator" property of the newly created
 * SIPMediaSession is set to our own handle.
 */
static void
priv_create_session (SIPMediaChannel *channel,
                     TpHandle peer,
                     const gchar *remote_url)
{
  SIPMediaChannelPrivate *priv;
  TpBaseConnection *conn;
  SIPMediaSession *session;
  gchar *object_path;
  const gchar *sid = NULL;
  TpHandle initiator;

  DEBUG("enter");

  g_assert (SIP_IS_MEDIA_CHANNEL (channel));

  priv = SIP_MEDIA_CHANNEL_GET_PRIVATE (channel);
  conn = (TpBaseConnection *)(priv->conn);
  g_assert (priv->session == NULL);

  object_path = g_strdup_printf ("%s/MediaSession%u", priv->object_path, peer);

  if (remote_url == NULL) {
    initiator = conn->self_handle;
    /* allocate a hash-entry for the new jingle session */
    sid = sip_media_factory_session_id_allocate (priv->factory);
  }
  else {
    initiator = peer;
    sid = remote_url;
  }

  g_debug("%s: allocating session, initiator=%u, peer=%u.", G_STRFUNC, initiator, peer);

  session = g_object_new (SIP_TYPE_MEDIA_SESSION,
                          "media-channel", channel,
                          "object-path", object_path,
                          "session-id", sid,
                          "initiator", initiator,
                          "peer", peer,
                          NULL);

  g_signal_connect (session, "notify::state",
                    (GCallback) priv_session_state_changed_cb, channel);
  g_signal_connect (session, "stream-added",
		    (GCallback) priv_session_stream_added_cb, channel);

  priv->session = session;

  /* keep a list of media session ids */
  sip_media_factory_session_id_register (priv->factory, sid, channel);

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
  gchar *sid = NULL;

  DEBUG("enter");

  session = priv->session;
  if (session == NULL)
    return;

  g_object_get (session, "session-id", &sid, NULL);
  sip_media_factory_session_id_unregister(priv->factory, sid);
  g_free (sid);

  priv->session = NULL;
  g_object_unref (session);
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

  DEBUG("enter");

  g_debug ("mixin->self_handle=%d, priv->creator=%d, handle=%d", 
	   mixin->self_handle, priv->creator, handle);
  
  /* case a: outgoing call (we are the initiator, a new handle added) */
  if (mixin->self_handle == priv->creator &&
      mixin->self_handle != handle) {

    TpGroupMixin *mixin = TP_GROUP_MIXIN (self);
    TpIntSet *lset, *rset;

    g_debug("making outbound call - setting peer handle to %u.\n", handle);

    priv_create_session(self, handle, NULL);

    /* make remote pending */
    rset = tp_intset_new ();
    lset = tp_intset_new ();
    tp_intset_add (lset, mixin->self_handle);
    tp_intset_add (rset, handle);
    tp_group_mixin_change_members (iface, "", lset, NULL, NULL, rset, 0, 0);
    tp_intset_destroy (lset);
    tp_intset_destroy (rset);

    /* and update flags accordingly */
    tp_group_mixin_change_flags (iface,
        TP_CHANNEL_GROUP_FLAG_CAN_ADD | TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
        TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
        0);
  }
  /* case b: an incoming invite */
  else {
    if (priv->session &&
	handle == mixin->self_handle &&
	tp_handle_set_is_member (mixin->local_pending, handle)) {

      TpIntSet *set;

      g_debug ("%s - accepting an incoming invite", G_STRFUNC);

      set = tp_intset_new ();
      tp_intset_add (set, mixin->self_handle);
      tp_group_mixin_change_members (iface, "", set, NULL, NULL, NULL, 0, 0);
      tp_intset_destroy (set);

      sip_media_session_accept (priv->session, TRUE);

    }
  }
  return TRUE;
}

static gboolean
priv_media_channel_remove_member (GObject *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  GError **error)
{
  /* XXX: no implemented */
  g_assert_not_reached ();
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
