/*
 * sip-media-stream.c - Source for RakiaMediaStream
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006-2010 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-stream).
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

#include "config.h"
#include "rakia/media-stream.h"

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>
#include <telepathy-glib/svc-media-interfaces.h>
#include <telepathy-glib/util.h>

#include "config.h"

#include <rakia/codec-param-formats.h>


#include "rakia/media-session.h"
#include "rakia/sip-session.h"

#include <sofia-sip/msg_parser.h>

#include "signals-marshal.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"


#define same_boolean(old, new) ((!(old)) == (!(new)))


#ifdef ENABLE_DEBUG

#define STREAM_DEBUG(stream, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "stream %u: " format, \
      (stream)->priv->id,##__VA_ARGS__)

#define STREAM_MESSAGE(stream, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "stream %u: " format, \
      (stream)->priv->id,##__VA_ARGS__)

#else

#define STREAM_DEBUG(stream, format, ...) G_STMT_START { } G_STMT_END
#define STREAM_MESSAGE(stream, format, ...) G_STMT_START { } G_STMT_END

#endif


static void stream_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(RakiaMediaStream,
    rakia_media_stream,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_STREAM_HANDLER,
      stream_handler_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
  )

/* signal enum */
enum
{
    SIG_READY,
    SIG_SUPPORTED_CODECS,
    SIG_STATE_CHANGED,
    SIG_DIRECTION_CHANGED,
    SIG_LOCAL_MEDIA_UPDATED,
    SIG_UNHOLD_FAILURE,

    SIG_LAST_SIGNAL
};

static guint signals[SIG_LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_SESSION = 1,
  PROP_DBUS_DAEMON,
  PROP_OBJECT_PATH,
  PROP_ID,
  PROP_MEDIA_TYPE,
  PROP_SIP_MEDIA,
  PROP_STATE,
  PROP_DIRECTION,
  PROP_PENDING_SEND_FLAGS,
  PROP_HOLD_STATE,
  PROP_CREATED_LOCALLY,
  PROP_NAT_TRAVERSAL,
  PROP_STUN_SERVERS,
  PROP_RELAY_INFO,
  LAST_PROPERTY
};

static GPtrArray *rakia_media_stream_relay_info_empty = NULL;

/* private structure */
struct _RakiaMediaStreamPrivate
{
  TpDBusDaemon *dbus_daemon;
  RakiaMediaSession *session;     /* see gobj. prop. 'media-session' */
  gchar *object_path;             /* see gobj. prop. 'object-path' */
  RakiaSipMedia *media;
  guint id;                       /* see gobj. prop. 'id' */
  guint media_type;               /* see gobj. prop. 'media-type' */
  guint state;                    /* see gobj. prop. 'state' */
  guint direction;                /* see gobj. prop. 'direction' */
  guint pending_send_flags;       /* see gobj. prop. 'pending-send-flags' */
  gboolean hold_state;            /* see gobj. prop. 'hold-state' */
  gboolean created_locally;       /* see gobj. prop. 'created-locally' */

  guint remote_candidate_counter;
  gchar *remote_candidate_id;

  gchar *native_candidate_id;

  gboolean ready_received;              /* our ready method has been called */
  gboolean playing;                     /* stream set to playing */
  gboolean sending;                     /* stream set to sending */
  gboolean pending_remote_receive;      /* TRUE if remote is to agree to receive media */
  gboolean native_cands_prepared;       /* all candidates discovered */
  gboolean native_codecs_prepared;      /* all codecs discovered */
  gboolean push_remote_cands_pending;   /* SetRemoteCandidates emission is pending */
  gboolean push_remote_codecs_pending;  /* SetRemoteCodecs emission is pending */
  gboolean codec_intersect_pending;     /* codec intersection is pending */
  gboolean requested_hold_state;        /* hold state last requested from the stream handler */
  gboolean dispose_has_run;
};

#define RAKIA_MEDIA_STREAM_GET_PRIVATE(stream) ((stream)->priv)

static void push_remote_codecs (RakiaMediaStream *stream);
static void push_remote_candidates (RakiaMediaStream *stream);
static void priv_update_sending (RakiaMediaStream *stream,
                                 TpMediaStreamDirection direction);
static void priv_emit_local_ready (RakiaMediaStream *stream);

/***********************************************************************
 * Set: Gobject interface
 ***********************************************************************/

static void
rakia_media_stream_init (RakiaMediaStream *self)
{
  RakiaMediaStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
      RAKIA_TYPE_MEDIA_STREAM, RakiaMediaStreamPrivate);

  self->priv = priv;
}

static void
rakia_media_stream_constructed (GObject *obj)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (
      RAKIA_MEDIA_STREAM (obj));
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_media_stream_parent_class);

  /* call base class method */
  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);

  /* XXX: overloading the remote pending send flag to check
   * if this is a locally offered stream. The code creating such streams
   * always sets the flag, because the remote end is supposed to decide
   * whether it wants to send.
   * This may look weird during a local hold. However, the pending flag
   * will be harmlessly cleared once the offer-answer is complete. */
  if ((priv->direction & TP_MEDIA_STREAM_DIRECTION_SEND) != 0
      && (priv->pending_send_flags & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0)
    {
      /* Block sending until the stream is remotely accepted */
      priv->pending_remote_receive = TRUE;
    }

  g_signal_connect_object (priv->media, "remote-candidates-updated",
      G_CALLBACK (push_remote_candidates), obj, G_CONNECT_SWAPPED);
  g_signal_connect_object (priv->media, "remote-codecs-updated",
      G_CALLBACK (push_remote_codecs), obj, G_CONNECT_SWAPPED);

  /* go for the bus */
  g_assert (TP_IS_DBUS_DAEMON (priv->dbus_daemon));
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path, obj);
}

static void
rakia_media_stream_get_property (GObject    *object,
			         guint       property_id,
			         GValue     *value,
			         GParamSpec *pspec)
{
  RakiaMediaStream *stream = RAKIA_MEDIA_STREAM (object);
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id)
    {
    case PROP_DBUS_DAEMON:
      g_value_set_object (value, priv->dbus_daemon);
      break;
    case PROP_MEDIA_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_ID:
      g_value_set_uint (value, priv->id);
      break;
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, priv->media_type);
      break;
    case PROP_SIP_MEDIA:
      g_value_set_object (value, priv->media);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_DIRECTION:
      g_value_set_uint (value, priv->direction);
      break;
    case PROP_PENDING_SEND_FLAGS:
      g_value_set_uint (value, priv->pending_send_flags);
      break;
    case PROP_HOLD_STATE:
      g_value_set_boolean (value, priv->hold_state);
      break;
    case PROP_CREATED_LOCALLY:
      g_value_set_boolean (value, priv->created_locally);
      break;
    case PROP_NAT_TRAVERSAL:
      g_value_set_static_string (value, "none");
      break;
    case PROP_STUN_SERVERS:
      g_return_if_fail (priv->session != NULL);
      g_object_get_property (G_OBJECT (priv->session), "stun-servers", value);
      break;
    case PROP_RELAY_INFO:
      g_value_set_static_boxed (value, rakia_media_stream_relay_info_empty);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
rakia_media_stream_set_property (GObject      *object,
			         guint         property_id,
			         const GValue *value,
			         GParamSpec   *pspec)
{
  RakiaMediaStream *stream = RAKIA_MEDIA_STREAM (object);
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id)
    {
    case PROP_DBUS_DAEMON:
      g_assert (priv->dbus_daemon == NULL);       /* construct-only */
      priv->dbus_daemon = g_value_dup_object (value);
      break;
    case PROP_MEDIA_SESSION:
      priv->session = g_value_get_object (value);
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_uint (value);
      break;
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
      break;
    case PROP_SIP_MEDIA:
      priv->media = g_value_dup_object (value);
      break;
    case PROP_STATE:
      priv->state = g_value_get_uint (value);
      break;
    case PROP_DIRECTION:
      priv->direction = g_value_get_uint (value);
      break;
    case PROP_PENDING_SEND_FLAGS:
      priv->pending_send_flags = g_value_get_uint (value);
      break;
    case PROP_HOLD_STATE:
      priv->hold_state = g_value_get_boolean (value);
      break;
    case PROP_CREATED_LOCALLY:
      priv->created_locally = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void rakia_media_stream_dispose (GObject *object);
static void rakia_media_stream_finalize (GObject *object);

static void
rakia_media_stream_class_init (RakiaMediaStreamClass *klass)
{
  static TpDBusPropertiesMixinPropImpl stream_handler_props[] = {
      { "CreatedLocally", "created-locally", NULL },
      { "NATTraversal", "nat-traversal", NULL },
      { "STUNServers", "stun-servers", NULL },
      { "RelayInfo", "relay-info", NULL },
      { NULL }
  };

  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_MEDIA_STREAM_HANDLER,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_handler_props,
      },
      { NULL }
  };

  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GType stream_type = G_OBJECT_CLASS_TYPE (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (RakiaMediaStreamPrivate));

  object_class->constructed = rakia_media_stream_constructed;

  object_class->get_property = rakia_media_stream_get_property;
  object_class->set_property = rakia_media_stream_set_property;

  object_class->dispose = rakia_media_stream_dispose;
  object_class->finalize = rakia_media_stream_finalize;

  param_spec = g_param_spec_object ("dbus-daemon", "TpDBusDaemon",
      "Connection to D-Bus.", TP_TYPE_DBUS_DAEMON,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, param_spec);

  param_spec = g_param_spec_object ("media-session", "RakiaMediaSession object",
      "SIP media session object that owns this media stream object.",
      RAKIA_TYPE_MEDIA_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_SESSION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_uint ("id", "Stream ID",
      "A stream number for the stream used in the D-Bus API.",
      0, G_MAXUINT,
      0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Stream media type",
      "A constant indicating which media type the stream carries.",
      TP_MEDIA_STREAM_TYPE_AUDIO, TP_MEDIA_STREAM_TYPE_VIDEO,
      TP_MEDIA_STREAM_TYPE_AUDIO,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);


  param_spec = g_param_spec_object ("sip-media", "RakiaSipMedia object",
      "SIP media session object that owns this media stream object.",
      RAKIA_TYPE_MEDIA_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_SESSION, param_spec);


  param_spec = g_param_spec_uint ("state", "Connection state",
      "Connection state of the media stream",
      TP_MEDIA_STREAM_STATE_DISCONNECTED, TP_MEDIA_STREAM_STATE_CONNECTED,
      TP_MEDIA_STREAM_STATE_DISCONNECTED,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  /* We don't change the following two as individual properties
   * after construction, use rakia_media_stream_set_direction() */

  param_spec = g_param_spec_uint ("direction", "Stream direction",
      "A value indicating the current direction of the stream",
      TP_MEDIA_STREAM_DIRECTION_NONE, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
      TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DIRECTION, param_spec);

  param_spec = g_param_spec_uint ("pending-send-flags", "Pending send flags",
      "Flags indicating the current pending send state of the stream",
      0,
      TP_MEDIA_STREAM_PENDING_LOCAL_SEND | TP_MEDIA_STREAM_PENDING_REMOTE_SEND,
      0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_PENDING_SEND_FLAGS,
                                   param_spec);

  param_spec = g_param_spec_boolean ("hold-state", "Hold state",
      "Hold state of the media stream as reported by the stream engine",
      FALSE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
                                   PROP_HOLD_STATE,
                                   param_spec);

  param_spec = g_param_spec_boolean ("created-locally", "Created locally?",
      "True if this stream was created by the local user", FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATED_LOCALLY,
      param_spec);

  param_spec = g_param_spec_string ("nat-traversal", "NAT traversal",
      "NAT traversal mechanism for this stream", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL,
      param_spec);

  param_spec = g_param_spec_boxed ("stun-servers", "STUN servers",
      "Array of IP address-port pairs for available STUN servers",
      TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS, param_spec);

  param_spec = g_param_spec_boxed ("relay-info", "Relay info",
      "Array of mappings containing relay server information",
      TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RELAY_INFO, param_spec);

  rakia_media_stream_relay_info_empty = g_ptr_array_new ();

  /* signals not exported by DBus interface */
  signals[SIG_READY] =
    g_signal_new ("ready",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[SIG_SUPPORTED_CODECS] =
    g_signal_new ("supported-codecs",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIG_STATE_CHANGED] =
    g_signal_new ("state-changed",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIG_DIRECTION_CHANGED] =
    g_signal_new ("direction-changed",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _rakia_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[SIG_LOCAL_MEDIA_UPDATED] =
    g_signal_new ("local-media-updated",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[SIG_UNHOLD_FAILURE] =
    g_signal_new ("unhold-failure",
                  stream_type,
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  klass->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (RakiaMediaStreamClass, dbus_props_class));
}

void
rakia_media_stream_dispose (GObject *object)
{
  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (object);
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->media);
  tp_clear_object (&priv->dbus_daemon);

  if (G_OBJECT_CLASS (rakia_media_stream_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_media_stream_parent_class)->dispose (object);

  DEBUG ("exit");
}

void
rakia_media_stream_finalize (GObject *object)
{
  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (object);
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->object_path);


  g_free (priv->native_candidate_id);
  g_free (priv->remote_candidate_id);

  G_OBJECT_CLASS (rakia_media_stream_parent_class)->finalize (object);

  DEBUG ("exit");
}

/***********************************************************************
 * Set: Media.StreamHandler interface implementation (same for 0.12/0.13???)
 ***********************************************************************/

/**
 * rakia_media_stream_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_error (TpSvcMediaStreamHandler *iface,
                          guint errno,
                          const gchar *message,
                          DBusGMethodInvocation *context)
{
  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (iface);

  STREAM_DEBUG (self, "StreamHandler.Error called: %u %s", errno, message);

  rakia_media_stream_close (self);

  tp_svc_media_stream_handler_return_from_error (context);
}

/**
 * rakia_media_stream_native_candidates_prepared
 *
 * Implements DBus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_native_candidates_prepared (TpSvcMediaStreamHandler *iface,
                                               DBusGMethodInvocation *context)
{
  /* purpose: "Informs the connection manager that all possible native candisates
   *          have been discovered for the moment." 
   */

  RakiaMediaStream *obj = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv = obj->priv;

  STREAM_DEBUG(obj, "Media.StreamHandler.NativeCandidatesPrepared called");

  priv->native_cands_prepared = TRUE;

  rakia_sip_media_local_candidates_prepared (priv->media);

  if (priv->native_codecs_prepared)
    priv_emit_local_ready (obj);

  tp_svc_media_stream_handler_return_from_native_candidates_prepared (context);
}


/**
 * rakia_media_stream_new_active_candidate_pair
 *
 * Implements DBus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_new_active_candidate_pair (TpSvcMediaStreamHandler *iface,
                                              const gchar *native_candidate_id,
                                              const gchar *remote_candidate_id,
                                              DBusGMethodInvocation *context)
{
  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv = self->priv;

  STREAM_DEBUG (self, "stream engine reported new active candidate pair %s-%s",
        native_candidate_id, remote_candidate_id);

  if (priv->remote_candidate_id == NULL
      || strcmp (priv->remote_candidate_id, remote_candidate_id))
    {
      GError *err;
      err = g_error_new (TP_ERRORS,
                         TP_ERROR_INVALID_ARGUMENT,
                         "Remote candidate ID does not match the locally "
                                "stored data");
      dbus_g_method_return_error (context, err);
      g_error_free (err);
      return;
    }

  tp_svc_media_stream_handler_return_from_new_active_candidate_pair (context);
}


/**
 * rakia_media_stream_new_native_candidate
 *
 * Implements DBus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_new_native_candidate (TpSvcMediaStreamHandler *iface,
                                         const gchar *candidate_id,
                                         const GPtrArray *transports,
                                         DBusGMethodInvocation *context)
{
  RakiaMediaStream *obj = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv;
  GValue transport = { 0, };
  guint i;

  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (obj);

  g_return_if_fail (transports->len >= 1);


  /* Rate the preferability of the address */
  g_value_init (&transport, TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT);

  for (i = 0; i < transports->len; i++)
    {
      RakiaSipCandidate *sipcandidate;
      guint tr_component;
      guint tr_proto = G_MAXUINT;
      gchar *tr_ip;
      guint tr_port;
      gdouble tr_preference;

      g_value_set_static_boxed (&transport,
          g_ptr_array_index (transports, i));

      /* Find the RTP component */
      dbus_g_type_struct_get (&transport,
          0, &tr_component,
          1, &tr_ip,
          2, &tr_port,
          3, &tr_proto,
          6, &tr_preference,
          G_MAXUINT);

      if (tr_proto != TP_MEDIA_STREAM_BASE_PROTO_UDP)
        continue;

      sipcandidate = rakia_sip_candidate_new (tr_component,
          tr_ip, tr_port, candidate_id, (guint) tr_preference * 65536);

      g_free (tr_ip);

      rakia_sip_media_take_local_candidate (priv->media, sipcandidate);
    }


  STREAM_DEBUG(obj, "put native candidate '%s' into cache", candidate_id);

  tp_svc_media_stream_handler_return_from_new_native_candidate (context);
}

static void
priv_set_local_codecs (RakiaMediaStream *self,
                       const GPtrArray *codecs)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);
  GPtrArray *sipcodecs = g_ptr_array_new_with_free_func (
      (GDestroyNotify)rakia_sip_codec_free);
  GValue codec = { 0, };
  gchar *co_name = NULL;
  guint co_id;
  guint co_type;
  guint co_clockrate;
  guint co_channels;
  GHashTable *co_params = NULL;
  guint i;
  STREAM_DEBUG(self, "putting list of %d locally supported codecs into cache",
      codecs->len);

  g_value_init (&codec, TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC);

  for (i = 0; i < codecs->len; i++)
    {
      RakiaSipCodec *sipcodec;
      GHashTableIter iter;
      gpointer key, value;

      g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));

      dbus_g_type_struct_get (&codec,
          0, &co_id,
          1, &co_name,
          2, &co_type,
          3, &co_clockrate,
          4, &co_channels,
          5, &co_params,
          G_MAXUINT);

      sipcodec = rakia_sip_codec_new (co_id, co_name, co_clockrate,
          co_channels);

      g_hash_table_iter_init (&iter, co_params);
      while (g_hash_table_iter_next (&iter, &key, &value))
        {
          rakia_sip_codec_add_param (sipcodec, key, value);
        }

      g_ptr_array_add (sipcodecs, sipcodec);

      g_free (co_name);
      co_name = NULL;
      g_hash_table_unref (co_params);
      co_params = NULL;
    }

  rakia_sip_media_take_local_codecs (priv->media, sipcodecs);

  priv->native_codecs_prepared = TRUE;
  if (priv->native_cands_prepared)
    priv_emit_local_ready (self);
}

static void
rakia_media_stream_codecs_updated (TpSvcMediaStreamHandler *iface,
                                   const GPtrArray *codecs,
                                   DBusGMethodInvocation *context)
{
  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);

  if (!priv->native_codecs_prepared)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "CodecsUpdated may not be called before codecs have been provided "
          "with SetLocalCodecs or Ready" };

      STREAM_DEBUG (self,
          "CodecsUpdated called before SetLocalCodecs or Ready");

      dbus_g_method_return_error (context, &e);
    }
  else
    {
      STREAM_DEBUG (self, "putting list of %d locally supported "
          "codecs from CodecsUpdated into cache", codecs->len);
      priv_set_local_codecs (self, codecs);

      if (priv->native_cands_prepared)
        g_signal_emit (self, signals[SIG_LOCAL_MEDIA_UPDATED], 0);

      tp_svc_media_stream_handler_return_from_codecs_updated (context);
    }
}

/**
 * rakia_media_stream_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_ready (TpSvcMediaStreamHandler *iface,
                          const GPtrArray *codecs,
                          DBusGMethodInvocation *context)
{
  /* purpose: "Inform the connection manager that a client is ready to handle
   *          this StreamHandler. Also provide it with info about all supported
   *          codecs."
   *
   * - note, with SIP we don't send the invite just yet (we need
   *   candidates first
   */

  RakiaMediaStream *obj = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv = obj->priv;

  STREAM_DEBUG (obj, "Media.StreamHandler.Ready called");

  if (priv->ready_received)
    {
      STREAM_MESSAGE (obj, "Ready called more than once");
      tp_svc_media_stream_handler_return_from_ready (context);
      return;
    }

  priv->ready_received = TRUE;

  if (codecs->len != 0)
    priv_set_local_codecs (obj, codecs);

  /* Push the initial sending/playing state */
  tp_svc_media_stream_handler_emit_set_stream_playing (
        iface, priv->playing);
  tp_svc_media_stream_handler_emit_set_stream_sending (
        iface, priv->sending);

  if (priv->push_remote_cands_pending)
    {
      priv->push_remote_cands_pending = FALSE;
      push_remote_candidates (obj);
    }
  if (priv->push_remote_codecs_pending)
    {
      priv->push_remote_codecs_pending = FALSE;
      push_remote_codecs (obj);
    }


  rakia_media_stream_set_playing (obj, TRUE);

  tp_svc_media_stream_handler_return_from_ready (context);
}

static void
rakia_media_stream_set_local_codecs (TpSvcMediaStreamHandler *iface,
                                     const GPtrArray *codecs,
                                     DBusGMethodInvocation *context)
{
  priv_set_local_codecs (RAKIA_MEDIA_STREAM (iface), codecs);
  tp_svc_media_stream_handler_return_from_set_local_codecs (context);
}

/**
 * rakia_media_stream_stream_state
 *
 * Implements DBus method StreamState
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_stream_state (TpSvcMediaStreamHandler *iface,
                               guint state,
                               DBusGMethodInvocation *context)
{
  /* purpose: "Informs the connection manager of the stream's current state
   *           State is as specified in *ChannelTypeStreamedMedia::GetStreams."
   *
   * - set the stream state for session
   */

  RakiaMediaStream *obj = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv;
  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (obj);

  if (priv->state != state)
    {
      STREAM_DEBUG (obj, "stream state change %u -> %u", priv->state, state);
      priv->state = state;
      g_signal_emit (obj, signals[SIG_STATE_CHANGED], 0, state);
    }

  tp_svc_media_stream_handler_return_from_stream_state (context);
}

/**
 * rakia_media_stream_supported_codecs
 *
 * Implements DBus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
rakia_media_stream_supported_codecs (TpSvcMediaStreamHandler *iface,
                                     const GPtrArray *codecs,
                                     DBusGMethodInvocation *context)
{
  /* purpose: "Inform the connection manager of the supported codecs for this session.
   *          This is called after the connection manager has emitted SetRemoteCodecs
   *          to notify what codecs are supported by the peer, and will thus be an
   *          intersection of all locally supported codecs (passed to Ready)
   *          and those supported by the peer."
   *
   * - emit SupportedCodecs
   */ 

  RakiaMediaStream *self = RAKIA_MEDIA_STREAM (iface);
  RakiaMediaStreamPrivate *priv;
  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);

  STREAM_DEBUG (self,
      "got codec intersection containing %u codecs from stream-engine",
      codecs->len);

  /* Save the local codecs, but avoid triggering a new
   * session update at this point. If the stream engine have changed any codec
   * parameters, it is supposed to follow up with CodecsUpdated. */
  priv_set_local_codecs (self, codecs);

  if (priv->codec_intersect_pending)
    {
      if (priv->push_remote_codecs_pending)
        {
          /* The remote codec list has been updated since the intersection
           * has started, plunge into a new intersection immediately */
          priv->push_remote_codecs_pending = FALSE;
          push_remote_codecs (self);
        }
      else
        {
          priv->codec_intersect_pending = FALSE;
          g_signal_emit (self, signals[SIG_SUPPORTED_CODECS], 0, codecs->len);
        }
    }
  else
    WARNING("SupportedCodecs called when no intersection is ongoing");

  tp_svc_media_stream_handler_return_from_supported_codecs (context);
}

static void
rakia_media_stream_hold_state (TpSvcMediaStreamHandler *self,
                               gboolean held,
                               DBusGMethodInvocation *context)
{
  g_object_set (self, "hold-state", held, NULL);
  tp_svc_media_stream_handler_return_from_hold_state (context);
}

static void
rakia_media_stream_unhold_failure (TpSvcMediaStreamHandler *self,
                                   DBusGMethodInvocation *context)
{
  /* Not doing anything to hold_state or requested_hold_state,
   * because the session is going to put all streams on hold after getting
   * the signal below */

  g_signal_emit (self, signals[SIG_UNHOLD_FAILURE], 0);
  tp_svc_media_stream_handler_return_from_unhold_failure (context);
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

guint
rakia_media_stream_get_id (RakiaMediaStream *self)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->id;
}

guint
rakia_media_stream_get_media_type (RakiaMediaStream *self)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->media_type;
}

void
rakia_media_stream_close (RakiaMediaStream *self)
{
  tp_svc_media_stream_handler_emit_close (self);
}

/*
 * Returns stream direction as requested by the latest local or remote
 * direction change.
 */
static TpMediaStreamDirection
priv_get_requested_direction (RakiaMediaStreamPrivate *priv)
{
  TpMediaStreamDirection direction;

  direction = priv->direction;
  if ((priv->pending_send_flags & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
    direction |= TP_MEDIA_STREAM_DIRECTION_SEND;
  return direction;
}

/**
 * Converts a sofia-sip media type enum to Telepathy media type.
 * See <sofia-sip/sdp.h> and <telepathy-constants.h>.
 *
 * @return G_MAXUINT if the media type cannot be mapped
 */
guint
rakia_tp_media_type (sdp_media_e sip_mtype)
{
  switch (sip_mtype)
    {
      case sdp_media_audio: return TP_MEDIA_STREAM_TYPE_AUDIO;
      case sdp_media_video: return TP_MEDIA_STREAM_TYPE_VIDEO; 
      default: return G_MAXUINT;
    }
}


/**
 * Sets the media state to playing or non-playing. When not playing,
 * received RTP packets may not be played locally.
 */
void rakia_media_stream_set_playing (RakiaMediaStream *stream, gboolean playing)
{
  RakiaMediaStreamPrivate *priv;
  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  if (same_boolean (priv->playing, playing))
    return;

  STREAM_DEBUG (stream, "set playing to %s", playing? "TRUE" : "FALSE");

  priv->playing = playing;

  if (priv->ready_received)
    tp_svc_media_stream_handler_emit_set_stream_playing (
        (TpSvcMediaStreamHandler *)stream, playing);
}

/**
 * Sets the media state to sending or non-sending. When not sending,
 * captured media are not sent over the network.
 */
void
rakia_media_stream_set_sending (RakiaMediaStream *stream, gboolean sending)
{
  RakiaMediaStreamPrivate *priv;
  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  if (same_boolean(priv->sending, sending))
    return;

  STREAM_DEBUG (stream, "set sending to %s", sending? "TRUE" : "FALSE");

  priv->sending = sending;

  if (priv->ready_received)
    tp_svc_media_stream_handler_emit_set_stream_sending (
        (TpSvcMediaStreamHandler *)stream, sending);
}

static void
priv_update_sending (RakiaMediaStream *stream,
                     TpMediaStreamDirection direction)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);
  gboolean sending = TRUE;

  /* XXX: the pending send flag check is probably an overkill
   * considering that effective sending direction and pending send should be
   * mutually exclusive */
  if ((direction & TP_MEDIA_STREAM_DIRECTION_SEND) == 0
      || priv->pending_remote_receive
      || (priv->pending_send_flags & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0
      || !rakia_media_session_is_accepted (priv->session))
    {
      sending = FALSE;
    }

  rakia_media_stream_set_sending (stream, sending);
}

void
rakia_media_stream_set_direction (RakiaMediaStream *stream,
                                  TpMediaStreamDirection direction,
                                  guint pending_send_mask)
{
  RakiaMediaStreamPrivate *priv;
  guint pending_send_flags;
  TpMediaStreamDirection old_sdp_direction;

  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);
  pending_send_flags = priv->pending_send_flags & pending_send_mask;

  if ((direction & TP_MEDIA_STREAM_DIRECTION_SEND) == 0)
    {
      /* We won't be sending, clear the pending local send flag */
      pending_send_flags &= ~TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
    }
  else if ((direction & TP_MEDIA_STREAM_DIRECTION_SEND & ~priv->direction) != 0)
    {
      /* We are requested to start sending, but... */
      if ((pending_send_mask
            & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
        {
          /* ... but we need to confirm this with the client.
           * Clear the sending bit and set the pending send flag. */
          direction &= ~(guint)TP_MEDIA_STREAM_DIRECTION_SEND;
          pending_send_flags |= TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
        }
      if ((pending_send_mask
              & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0
          && (priv->pending_send_flags
              & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) == 0)
        {
          g_assert ((priv_get_requested_direction (priv) & TP_MEDIA_STREAM_DIRECTION_SEND) == 0);

          /* ... but the caller wants to agree with the remote
           * end first. Block the stream handler from sending for now. */
          priv->pending_remote_receive = TRUE;
        }
    }

  if ((direction & TP_MEDIA_STREAM_DIRECTION_RECEIVE) == 0)
    {
      /* We are not going to receive, clear the pending remote send flag */
      pending_send_flags &= ~TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
    }
  else if ((direction & TP_MEDIA_STREAM_DIRECTION_RECEIVE & ~priv->direction) != 0
           && (pending_send_mask
               & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0)
    {
      /* We're requested to start receiving, but the remote end did not
       * confirm if it will send. Set the pending send flag. */
      pending_send_flags |= TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
    }

  if (priv->direction == direction
      && priv->pending_send_flags == pending_send_flags)
    return;

  old_sdp_direction = priv_get_requested_direction (priv);

  priv->direction = direction;
  priv->pending_send_flags = pending_send_flags;

  STREAM_DEBUG (stream, "set direction %u, pending send flags %u", priv->direction, priv->pending_send_flags);

  g_signal_emit (stream, signals[SIG_DIRECTION_CHANGED], 0,
                 priv->direction, priv->pending_send_flags);

  priv_update_sending (stream, priv->direction);

  if (priv->native_cands_prepared
      && priv->native_codecs_prepared
      && priv_get_requested_direction (priv)
         != old_sdp_direction)
    g_signal_emit (stream, signals[SIG_LOCAL_MEDIA_UPDATED], 0);
}

/*
 * Clears the pending send flag(s) present in @pending_send_mask.
 * If #TP_MEDIA_STREAM_PENDING_LOCAL_SEND is thus cleared,
 * enable the sending bit in the stream direction.
 * If @pending_send_mask has #TP_MEDIA_STREAM_PENDING_REMOTE_SEND flag set,
 * also start sending if agreed by the stream direction.
 */
void
rakia_media_stream_apply_pending_direction (RakiaMediaStream *stream,
                                            guint pending_send_mask)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);
  guint flags;


  /* Don't apply pending send for new streams that haven't been negotiated */
  //if (priv->remote_media == NULL)
  //  return;

  /* Remember the flags that got changes and then clear the set */
  flags = (priv->pending_send_flags & pending_send_mask);
  priv->pending_send_flags &= ~pending_send_mask;

  if (flags != 0)
    {
      if ((flags & TP_MEDIA_STREAM_PENDING_LOCAL_SEND) != 0)
        priv->direction |= TP_MEDIA_STREAM_DIRECTION_SEND;

      STREAM_DEBUG (stream, "set direction %u, pending send flags %u", priv->direction, priv->pending_send_flags);

      g_signal_emit (stream, signals[SIG_DIRECTION_CHANGED], 0,
                     priv->direction, priv->pending_send_flags);
    }

  if ((pending_send_mask & TP_MEDIA_STREAM_PENDING_REMOTE_SEND) != 0)
    {
      priv->pending_remote_receive = FALSE;
      STREAM_DEBUG (stream, "remote end ready to receive");
    }

  /* Always check to enable sending because the session could become accepted */
  priv_update_sending (stream, priv->direction);
}

TpMediaStreamDirection
rakia_media_stream_get_requested_direction (RakiaMediaStream *self)
{
  return priv_get_requested_direction (RAKIA_MEDIA_STREAM_GET_PRIVATE (self));
}

/**
 * Returns true if the stream has a valid SDP description and
 * connection has been established with the stream engine.
 */
gboolean rakia_media_stream_is_local_ready (RakiaMediaStream *self)
{
  RakiaMediaStreamPrivate *priv = self->priv;
  return (priv->ready_received && priv->native_cands_prepared
          && priv->native_codecs_prepared);
}

gboolean
rakia_media_stream_is_codec_intersect_pending (RakiaMediaStream *self)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->codec_intersect_pending;
}

void
rakia_media_stream_start_telephony_event (RakiaMediaStream *self, guchar event)
{
  tp_svc_media_stream_handler_emit_start_telephony_event (
        (TpSvcMediaStreamHandler *)self, event);
}

void
rakia_media_stream_stop_telephony_event  (RakiaMediaStream *self)
{
  tp_svc_media_stream_handler_emit_stop_telephony_event (
        (TpSvcMediaStreamHandler *)self);
}

gboolean
rakia_media_stream_request_hold_state (RakiaMediaStream *self, gboolean hold)
{
  RakiaMediaStreamPrivate *priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (self);

  if ((!priv->requested_hold_state) != (!hold))
    {
      priv->requested_hold_state = hold;
      tp_svc_media_stream_handler_emit_set_stream_held (self, hold);
      return TRUE;
    }
  return FALSE;
}

static void
priv_emit_local_ready (RakiaMediaStream *self)
{
  /* Trigger any session updates that are due in the current session state */
  g_signal_emit (self, signals[SIG_LOCAL_MEDIA_UPDATED], 0);
  g_signal_emit (self, signals[SIG_READY], 0);
}

/**
 * Notify StreamEngine of remote codecs.
 *
 * @pre Ready signal must be receiveid (priv->ready_received)
 */
static void push_remote_codecs (RakiaMediaStream *stream)
{
  RakiaMediaStreamPrivate *priv;
  GPtrArray *codecs;
  GType codecs_type;
  GType codec_type;
  GPtrArray *sipcodecs;
  guint i, j;

  DEBUG ("enter");

  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  sipcodecs = rakia_sip_media_get_remote_codecs (priv->media);
  if (sipcodecs == NULL)
    {
      STREAM_DEBUG (stream, "remote media description is not received yet");
      return;
    }

  if (!priv->ready_received)
    {
      STREAM_DEBUG (stream, "the stream engine is not ready, SetRemoteCodecs is pending");
      priv->push_remote_codecs_pending = TRUE;
      return;
    }

  codec_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC;
  codecs_type = TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CODEC_LIST;

  codecs = dbus_g_type_specialized_construct (codecs_type);

  for (i = 0; i < sipcodecs->len; i++)
    {
      GValue codec = { 0, };
      GHashTable *opt_params;
      RakiaSipCodec *sipcodec = g_ptr_array_index (sipcodecs, i);

      g_value_init (&codec, codec_type);
      g_value_take_boxed (&codec,
                          dbus_g_type_specialized_construct (codec_type));

      opt_params = g_hash_table_new (g_str_hash, g_str_equal);
      if (sipcodec->params)
        {
          for (j = 0; j < sipcodec->params->len; j++)
            {
              RakiaSipCodecParam *param =
                  g_ptr_array_index (sipcodec->params, j);

              g_hash_table_insert (opt_params, param->name, param->value);
            }
        }

      dbus_g_type_struct_set (&codec,
                              /* payload type: */
                              0, sipcodec->id,
                              /* encoding name: */
                              1, sipcodec->encoding_name,
                              /* media type */
                              2, (guint)priv->media_type,
                              /* clock-rate */
                              3, sipcodec->clock_rate,
                              /* number of supported channels: */
                              4, sipcodec->channels,
                              /* optional params: */
                              5, opt_params,
                              G_MAXUINT);

      g_hash_table_unref (opt_params);

      g_ptr_array_add (codecs, g_value_get_boxed (&codec));
    }


  STREAM_DEBUG(stream, "emitting %d remote codecs to the handler",
      codecs->len);

  tp_svc_media_stream_handler_emit_set_remote_codecs (
        (TpSvcMediaStreamHandler *)stream, codecs);

  g_boxed_free (codecs_type, codecs);
}

static void push_remote_candidates (RakiaMediaStream *stream)
{
  RakiaMediaStreamPrivate *priv;
  GValue candidate = { 0 };
  GValue transport = { 0 };
  GValue transport_rtcp = { 0 };
  GPtrArray *candidates;
  GPtrArray *transports;
  GType candidate_type;
  GType candidates_type;
  GType transport_type;
  GType transports_type;
  gchar *candidate_id;
  GPtrArray *remote_candidates;
  RakiaSipCandidate *rtp_cand = NULL;
  RakiaSipCandidate *rtcp_cand = NULL;
  guint i;

  DEBUG("enter");

  priv = RAKIA_MEDIA_STREAM_GET_PRIVATE (stream);

  remote_candidates = rakia_sip_media_get_remote_candidates (priv->media);

  if (remote_candidates == NULL)
    {
      STREAM_DEBUG (stream, "remote media description is not received yet");
      return;
    }

  if (!priv->ready_received)
    {
      STREAM_DEBUG (stream, "the stream engine is not ready, SetRemoteCandidateList is pending");
      priv->push_remote_cands_pending = TRUE;
      return;
    }

  for (i = 0; i < remote_candidates->len; i++)
    {
      RakiaSipCandidate *tmpc = g_ptr_array_index (remote_candidates, i);

      if (tmpc->component == 1)
        {
          rtp_cand = tmpc;
        }
      else if (tmpc->component == 2)
        {
          rtcp_cand = tmpc;
        }
    }

  if (rtp_cand == NULL)
    {
      STREAM_DEBUG (stream, "no rtp candidate");
      priv->push_remote_cands_pending = TRUE;
      return;
    }

  transports_type = TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT_LIST;
  transports = dbus_g_type_specialized_construct (transports_type);

  transport_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT;
  g_value_init (&transport, transport_type);
  g_value_take_boxed (&transport,
                      dbus_g_type_specialized_construct (transport_type));
  dbus_g_type_struct_set (&transport,
                          0, 1,         /* component number */
                          1, rtp_cand->ip,
                          2, rtp_cand->port,
                          3, TP_MEDIA_STREAM_BASE_PROTO_UDP,
                          4, "RTP",
                          5, "AVP",
                          /* 6, 0.0f, */
                          7, TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
                          /* 8, "", */
                          /* 9, "", */
                          G_MAXUINT);

  STREAM_DEBUG (stream, "remote RTP address=<%s>, port=<%u>", rtp_cand->ip,
      rtp_cand->port);
  g_ptr_array_add (transports, g_value_get_boxed (&transport));

  if (rtcp_cand)
    {
      g_value_init (&transport_rtcp, transport_type);
      g_value_take_boxed (&transport_rtcp,
          dbus_g_type_specialized_construct (transport_type));
      dbus_g_type_struct_set (&transport_rtcp,
          0, 2,         /* component number */
          1, rtcp_cand->ip,
          2, rtcp_cand->port,
          3, TP_MEDIA_STREAM_BASE_PROTO_UDP,
          4, "RTP",
          5, "AVP",
          /* 6, 0.0f, */
          7, TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
          /* 8, "", */
          /* 9, "", */
          G_MAXUINT);

      STREAM_DEBUG (stream, "remote RTCP address=<%s>, port=<%u>",
          rtcp_cand->ip, rtcp_cand->port);
      g_ptr_array_add (transports, g_value_get_boxed (&transport_rtcp));
    }

  g_free (priv->remote_candidate_id);
  candidate_id = g_strdup_printf ("L%u", ++priv->remote_candidate_counter);
  priv->remote_candidate_id = candidate_id;

  candidate_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE;
  g_value_init (&candidate, candidate_type);
  g_value_take_boxed (&candidate,
                      dbus_g_type_specialized_construct (candidate_type));
  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  candidates_type = TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE_LIST;
  candidates = dbus_g_type_specialized_construct (candidates_type);
  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  STREAM_DEBUG (stream, "emitting SetRemoteCandidateList with %s", candidate_id);

  tp_svc_media_stream_handler_emit_set_remote_candidate_list (
          (TpSvcMediaStreamHandler *)stream, candidates);

  g_boxed_free (candidates_type, candidates);
  g_boxed_free (transports_type, transports);
}


static void
stream_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaStreamHandlerClass *klass = (TpSvcMediaStreamHandlerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_media_stream_handler_implement_##x (\
    klass, (tp_svc_media_stream_handler_##x##_impl) rakia_media_stream_##x)
  IMPLEMENT(error);
  IMPLEMENT(native_candidates_prepared);
  IMPLEMENT(new_active_candidate_pair);
  IMPLEMENT(new_native_candidate);
  IMPLEMENT(ready);
  IMPLEMENT(set_local_codecs);
  IMPLEMENT(codecs_updated);
  IMPLEMENT(stream_state);
  IMPLEMENT(supported_codecs);
  IMPLEMENT(hold_state);
  IMPLEMENT(unhold_failure);
#undef IMPLEMENT
}

RakiaSipMedia *
rakia_media_stream_get_media (RakiaMediaStream *stream)
{
  RakiaMediaStreamPrivate *priv = stream->priv;

  return priv->media;
}
