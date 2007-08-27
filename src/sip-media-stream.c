/*
 * sip-media-stream.c - Source for SIPMediaStream
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006,2007 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-stream).
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

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "config.h"

#include "sip-media-stream.h"
#include "sip-media-session.h"

#include "signals-marshal.h"
#include "telepathy-helpers.h"

#define DEBUG_FLAG SIP_DEBUG_MEDIA
#include "debug.h"

static void stream_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(SIPMediaStream,
    sip_media_stream,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_STREAM_HANDLER,
      stream_handler_iface_init)
    )

/* signal enum */
enum
{
    SIG_READY,
    SIG_SUPPORTED_CODECS,
    SIG_STATE_CHANGED,
    SIG_DIRECTION_CHANGED,

    SIG_LAST_SIGNAL
};

static guint signals[SIG_LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_MEDIA_SESSION = 1,
  PROP_OBJECT_PATH,
  PROP_ID,
  PROP_MEDIA_TYPE,
  PROP_STATE,
  PROP_DIRECTION,
  PROP_PENDING_SEND_FLAGS,
  LAST_PROPERTY
};

/* private structure */
typedef struct _SIPMediaStreamPrivate SIPMediaStreamPrivate;

struct _SIPMediaStreamPrivate
{
  SIPMediaSession *session;       /** see gobj. prop. 'media-session' */
  gchar *object_path;             /** see gobj. prop. 'object-path' */
  guint id;                       /** see gobj. prop. 'id' */
  guint media_type;               /** see gobj. prop. 'media-type' */
  guint state;                    /** see gobj. prop. 'state' */
  guint direction;                /** see gobj. prop. 'direction' */
  guint pending_send_flags;       /** see gobj. prop. 'pending-send-flags' */

  gchar *stream_sdp;              /** SDP description of the stream */


  GValue native_codecs;           /** intersected codec list */
  GValue native_candidates;

  const sdp_media_t *remote_media; /** pointer to the SDP media structure
                                    *  owned by the session object */

  guint remote_candidate_counter;
  gchar *remote_candidate_id;

  gchar *native_candidate_id;

  gboolean ready_received               : 1; /** our ready method has been called */
  gboolean playing                      : 1; /** stream set to playing */
  gboolean sending                      : 1; /** stream set to sending */
  gboolean native_cands_prepared        : 1; /** all candidates discovered */
  gboolean native_codecs_prepared       : 1; /** all codecs discovered */
  gboolean push_remote_requested        : 1; /** remote info signals are pending */
  gboolean codec_intersect_pending      : 1; /** codec intersection is pending */
  gboolean dispose_has_run              : 1;
};

#define SIP_MEDIA_STREAM_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_STREAM, SIPMediaStreamPrivate))

static void push_remote_codecs (SIPMediaStream *stream);
static void push_remote_candidates (SIPMediaStream *stream);
static void push_active_candidate_pair (SIPMediaStream *stream);
static void priv_update_sending (SIPMediaStream *stream,
                                 TpMediaStreamDirection direction,
                                 guint pending_send_flags);
static void priv_update_local_sdp(SIPMediaStream *stream);
static void priv_generate_sdp (SIPMediaStream *stream);

#if 0
#ifdef ENABLE_DEBUG
static const char *debug_tp_protocols[] = {
  "TP_MEDIA_STREAM_PROTO_UDP (0)",
  "TP_MEDIA_STREAM_PROTO_TCP (1)"
};

static const char *debug_tp_transports[] = {
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL (0)",
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED (1)",
  "TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY (2)"
};
#endif /* ENABLE_DEBUG */
#endif /* 0 */

/***********************************************************************
 * Set: DBus type utilities
 ***********************************************************************/

DEFINE_TP_STRUCT_TYPE(sip_tp_transport_struct_type,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_STRING,
                      G_TYPE_DOUBLE,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_STRING)

DEFINE_TP_LIST_TYPE(sip_tp_transport_list_type,
                    sip_tp_transport_struct_type())

DEFINE_TP_STRUCT_TYPE(sip_tp_candidate_struct_type,
                      G_TYPE_STRING,
                      sip_tp_transport_list_type ())

DEFINE_TP_LIST_TYPE(sip_tp_candidate_list_type,
                    sip_tp_candidate_struct_type ())

DEFINE_TP_STRUCT_TYPE(sip_tp_codec_struct_type,
                      G_TYPE_UINT,
                      G_TYPE_STRING,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      DBUS_TYPE_G_STRING_STRING_HASHTABLE)

DEFINE_TP_LIST_TYPE(sip_tp_codec_list_type,
                    sip_tp_codec_struct_type ())

/***********************************************************************
 * Set: Gobject interface
 ***********************************************************************/

static void
sip_media_stream_init (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  priv->playing = FALSE;
  priv->sending = FALSE;

  g_value_init (&priv->native_codecs, sip_tp_codec_list_type ());
  g_value_take_boxed (&priv->native_codecs,
      dbus_g_type_specialized_construct (sip_tp_codec_list_type ()));

  g_value_init (&priv->native_candidates, sip_tp_candidate_list_type ());
  g_value_take_boxed (&priv->native_candidates,
      dbus_g_type_specialized_construct (sip_tp_candidate_list_type ()));

  priv->native_cands_prepared = FALSE;
  priv->native_codecs_prepared = FALSE;

  priv->push_remote_requested = FALSE;
}

static GObject *
sip_media_stream_constructor (GType type, guint n_props,
			      GObjectConstructParam *props)
{
  GObject *obj;
  SIPMediaStreamPrivate *priv;
  DBusGConnection *bus;

  /* call base class constructor */
  obj = G_OBJECT_CLASS (sip_media_stream_parent_class)->
           constructor (type, n_props, props);
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (SIP_MEDIA_STREAM (obj));

  /* go for the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
sip_media_stream_get_property (GObject    *object,
			       guint       property_id,
			       GValue     *value,
			       GParamSpec *pspec)
{
  SIPMediaStream *stream = SIP_MEDIA_STREAM (object);
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id)
    {
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
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    case PROP_DIRECTION:
      g_value_set_uint (value, priv->direction);
      break;
    case PROP_PENDING_SEND_FLAGS:
      g_value_set_uint (value, priv->pending_send_flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
sip_media_stream_set_property (GObject      *object,
			       guint         property_id,
			       const GValue *value,
			       GParamSpec   *pspec)
{
  SIPMediaStream *stream = SIP_MEDIA_STREAM (object);
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  switch (property_id)
    {
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
    case PROP_STATE:
      priv->state = g_value_get_uint (value);
      break;
    case PROP_DIRECTION:
      priv->direction = g_value_get_uint (value);
      break;
    case PROP_PENDING_SEND_FLAGS:
      priv->pending_send_flags = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void sip_media_stream_dispose (GObject *object);
static void sip_media_stream_finalize (GObject *object);

static void
sip_media_stream_class_init (SIPMediaStreamClass *sip_media_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_media_stream_class);
  GParamSpec *param_spec;

  g_type_class_add_private (sip_media_stream_class, sizeof (SIPMediaStreamPrivate));

  object_class->constructor = sip_media_stream_constructor;

  object_class->get_property = sip_media_stream_get_property;
  object_class->set_property = sip_media_stream_set_property;

  object_class->dispose = sip_media_stream_dispose;
  object_class->finalize = sip_media_stream_finalize;

  param_spec = g_param_spec_object ("media-session", "GabbleMediaSession object",
                                    "SIP media session object that owns this "
                                    "media stream object.",
                                    SIP_TYPE_MEDIA_SESSION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_SESSION, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_uint ("id", "Stream ID",
                                  "A stream number for the stream used in the "
                                  "D-Bus API.",
                                  0, G_MAXUINT, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Stream media type",
                                  "A constant indicating which media type the "
                                  "stream carries.",
                                  TP_MEDIA_STREAM_TYPE_AUDIO,
                                  TP_MEDIA_STREAM_TYPE_VIDEO,
                                  TP_MEDIA_STREAM_TYPE_AUDIO,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  /* We don't change the following two as individual properties
   * after construction, use sip_media_stream_set_direction() */

  param_spec = g_param_spec_uint ("state", "Connection state",
                                  "Connection state of the media stream",
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  TP_MEDIA_STREAM_STATE_CONNECTED,
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_STATE,
                                   param_spec);

  param_spec = g_param_spec_uint ("direction", "Stream direction",
                                  "A value indicating the current "
                                        "direction of the stream",
                                  TP_MEDIA_STREAM_DIRECTION_NONE,
                                  TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
                                  TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DIRECTION, param_spec);

  param_spec = g_param_spec_uint ("pending-send-flags", "Pending send flags",
                                  "Flags indicating the current "
                                        "pending send state of the stream",
                                  0,
                                  TP_MEDIA_STREAM_PENDING_LOCAL_SEND
                                        | TP_MEDIA_STREAM_PENDING_REMOTE_SEND,
                                  0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class,
                                   PROP_PENDING_SEND_FLAGS,
                                   param_spec);

  /* signals not exported by DBus interface */
  signals[SIG_READY] =
    g_signal_new ("ready",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  signals[SIG_SUPPORTED_CODECS] =
    g_signal_new ("supported-codecs",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIG_STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);

  signals[SIG_DIRECTION_CHANGED] =
    g_signal_new ("direction-changed",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tpsip_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

void
sip_media_stream_dispose (GObject *object)
{
  SIPMediaStream *self = SIP_MEDIA_STREAM (object);
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (sip_media_stream_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_stream_parent_class)->dispose (object);

  DEBUG ("exit");
}

void
sip_media_stream_finalize (GObject *object)
{
  SIPMediaStream *self = SIP_MEDIA_STREAM (object);
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  /* free any data held directly by the object here */
  g_free (priv->object_path);
  g_free (priv->stream_sdp);

  g_value_unset (&priv->native_codecs);
  g_value_unset (&priv->native_candidates);

  g_free (priv->native_candidate_id);
  g_free (priv->remote_candidate_id);

  G_OBJECT_CLASS (sip_media_stream_parent_class)->finalize (object);

  DEBUG ("exit");
}

/***********************************************************************
 * Set: Media.StreamHandler interface implementation (same for 0.12/0.13???)
 ***********************************************************************/

/**
 * sip_media_stream_codec_choice
 *
 * Implements DBus method CodecChoice
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_codec_choice (TpSvcMediaStreamHandler *iface,
                               guint codec_id,
                               DBusGMethodInvocation *context)
{
  /* Inform the connection manager of the current codec choice. 
   * -> note: not implemented by tp-gabble either (2006/May) */

  g_debug ("%s: not implemented (ignoring)", G_STRFUNC);

  tp_svc_media_stream_handler_return_from_codec_choice (context);
}

/**
 * sip_media_stream_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_error (TpSvcMediaStreamHandler *iface,
                        guint errno,
                        const gchar *message,
                        DBusGMethodInvocation *context)
{
  /* Note: Inform the connection manager that an error occured in this stream. */

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  SESSION_DEBUG(priv->session, "Media.StreamHandler::Error called -- terminating session");

  sip_media_session_terminate (priv->session);

  tp_svc_media_stream_handler_return_from_error (context);
}


/**
 * sip_media_stream_native_candidates_prepared
 *
 * Implements DBus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_native_candidates_prepared (TpSvcMediaStreamHandler *iface,
                                             DBusGMethodInvocation *context)
{
  /* purpose: "Informs the connection manager that all possible native candisates
   *          have been discovered for the moment." 
   *
   * note: only emitted by the stream-engine when built without
   *       libjingle (tested with s-e 0.3.11, 2006/Dec)
   */

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;

  g_assert (SIP_IS_MEDIA_STREAM (obj));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);
  
  priv->native_cands_prepared = TRUE;

  push_active_candidate_pair (obj);

  if (priv->native_codecs_prepared)
    priv_generate_sdp (obj);

  tp_svc_media_stream_handler_return_from_native_candidates_prepared (context);
}


/**
 * sip_media_stream_new_active_candidate_pair
 *
 * Implements DBus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_new_active_candidate_pair (TpSvcMediaStreamHandler *iface,
                                            const gchar *native_candidate_id,
                                            const gchar *remote_candidate_id,
                                            DBusGMethodInvocation *context)
{
  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  DEBUG("stream engine reported new active candidate pair %s-%s",
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
 * sip_media_stream_new_native_candidate
 *
 * Implements DBus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_new_native_candidate (TpSvcMediaStreamHandler *iface,
                                       const gchar *candidate_id,
                                       const GPtrArray *transports,
                                       DBusGMethodInvocation *context)
{
  /* purpose: "Inform this MediaStreamHandler that a new native transport candidate
   *
   * - decide whether it's time generate an offer/answer (based on gathered
   *   candidates); this should be done after candidates_prepared(),
   *   but current stream-engine never emits this
   * - create SDP segment for this stream (the m-line and associated
   *   c-line and attributes)
   * - mark that we've created SDP (so that additional new candidates 
   *   can be processed correced 
   * - emit 'Ready' when ready to send offer/answer
   */

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;
  GPtrArray *candidates;
  GValue candidate = { 0, };

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  if (priv->stream_sdp != NULL)
    {
      g_message ("Stream %u: SDP already generated, ignoring native candidate '%s'", priv->id, candidate_id);
      tp_svc_media_stream_handler_return_from_new_native_candidate (context);
      return;
    }

  g_free (priv->native_candidate_id);
  priv->native_candidate_id = g_strdup (candidate_id);

  candidates = g_value_get_boxed (&priv->native_candidates);

  g_value_init (&candidate, sip_tp_candidate_struct_type ());
  g_value_take_boxed (&candidate,
      dbus_g_type_specialized_construct (sip_tp_candidate_struct_type ()));

  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  SESSION_DEBUG(priv->session, "put native candidate '%s' from stream-engine into cache", candidate_id);

  if (priv->native_codecs_prepared)
    priv_generate_sdp (obj);

  tp_svc_media_stream_handler_return_from_new_native_candidate (context);
}


/**
 * sip_media_stream_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_ready (TpSvcMediaStreamHandler *iface,
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

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;
  GValue val = { 0, };

  DEBUG ("enter");

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  priv->ready_received = TRUE;

  SESSION_DEBUG(priv->session, "putting list of %d locally supported "
                "codecs from stream-engine into cache", codecs->len);
  g_value_init (&val, sip_tp_codec_list_type ());
  g_value_set_static_boxed (&val, codecs);
  g_value_copy (&val, &priv->native_codecs);

  priv->native_codecs_prepared = TRUE;
  if (priv->native_cands_prepared)
    priv_generate_sdp (obj);

  /* Push the initial sending/playing state */
  tp_svc_media_stream_handler_emit_set_stream_sending (
        iface, priv->sending);
  tp_svc_media_stream_handler_emit_set_stream_playing (
        iface, priv->playing);

  if (priv->push_remote_requested) {
    push_remote_candidates (obj);
    push_remote_codecs (obj);
    priv->push_remote_requested = FALSE;
  }

  /* note: for inbound sessions, emit active candidate pair once 
           remote info is set */
  push_active_candidate_pair (obj);

  tp_svc_media_stream_handler_return_from_ready (context);
}

/* FIXME: set_local_codecs not implemented */

/**
 * sip_media_stream_stream_state
 *
 * Implements DBus method StreamState
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_stream_state (TpSvcMediaStreamHandler *iface,
                               guint state,
                               DBusGMethodInvocation *context)
{
  /* purpose: "Informs the connection manager of the stream's current state
   *           State is as specified in *ChannelTypeStreamedMedia::GetStreams."
   *
   * - set the stream state for session
   */

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  if (priv->state != state)
    {
      DEBUG("changing stream state from %u to %u", priv->state, state);
      priv->state = state;
      g_signal_emit (obj, signals[SIG_STATE_CHANGED], 0, state);
    }

  tp_svc_media_stream_handler_return_from_stream_state (context);
}

/**
 * sip_media_stream_supported_codecs
 *
 * Implements DBus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
sip_media_stream_supported_codecs (TpSvcMediaStreamHandler *iface,
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

  SIPMediaStream *self = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  SESSION_DEBUG(priv->session, "got codec intersection containing %u "
                "codecs from stream-engine", codecs->len);

  g_assert (priv->codec_intersect_pending);
  priv->codec_intersect_pending = FALSE;

  /* store the intersection for later on */
  g_value_set_boxed (&priv->native_codecs, codecs);

  g_signal_emit (self, signals[SIG_SUPPORTED_CODECS], 0, codecs->len);

  tp_svc_media_stream_handler_return_from_supported_codecs (context);
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

guint
sip_media_stream_get_id (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->id;
}

guint
sip_media_stream_get_media_type (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->media_type;
}

void
sip_media_stream_close (SIPMediaStream *self)
{
  tp_svc_media_stream_handler_emit_close (self);
}

/**
 * Described the local stream configuration in SDP (RFC2327),
 * or NULL if stream not configured yet.
 */
const char *sip_media_stream_local_sdp (SIPMediaStream *obj)
{
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);
  return priv->stream_sdp;
}

static inline guint
sip_tp_stream_direction_from_remote (sdp_mode_t mode)
{
  return ((mode & sdp_recvonly)? TP_MEDIA_STREAM_DIRECTION_SEND : 0)
       | ((mode & sdp_sendonly)? TP_MEDIA_STREAM_DIRECTION_RECEIVE : 0);
}

/** 
 * Sets the remote candidates and codecs for this stream, as 
 * received via signaling.
 * 
 * Parses the SDP information, updates TP remote candidates and
 * codecs if the client is ready.
 * 
 * Note that the pointer to the media description structure is saved,
 * implying that the structure shall not go away for the lifetime of
 * the stream, preferably kept in the memory home attached to
 * the session object.
 *
 * @return TRUE if the remote information has been accepted,
 *         FALSE if the update is not acceptable.
 */
gboolean
sip_media_stream_set_remote_media (SIPMediaStream *stream,
                                   const sdp_media_t *new_media,
                                   gboolean authoritative)
{
  SIPMediaStreamPrivate *priv;
  sdp_connection_t *sdp_conn;
  const sdp_media_t *old_media;
  gboolean transport_changed = TRUE;
  guint new_direction;

  DEBUG ("enter");

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  /* Do sanity checks */

  g_return_val_if_fail (new_media != NULL, -1);

  if (new_media->m_rejected || new_media->m_port == 0)
    {
      DEBUG("the stream is rejected remotely");
      return FALSE;
    }

  if (new_media->m_proto != sdp_proto_rtp)
    {
      g_warning ("Stream %u: the remote protocol is not RTP/AVP", priv->id);
      return FALSE;
    }

  sdp_conn = sdp_media_connections (new_media);
  if (sdp_conn == NULL)
    {
      g_warning ("Stream %u: no valid remote connections", priv->id);
      return FALSE;
    }

  /* Note: always update the pointer to the current media structure
   * because of memory management done in the session object */
  old_media = priv->remote_media;
  priv->remote_media = new_media;

  /* Check if there was any media update at all */

  if (sdp_media_cmp (old_media, new_media) == 0)
    {
      DEBUG("no media changes detected for the stream");
      return TRUE;
    }

  new_direction = sip_tp_stream_direction_from_remote (new_media->m_mode);

  /* Make sure the answer can only remove sending or receiving bits
   * of the offer */
  if (!authoritative)
    new_direction &= priv->direction;

  /* Check if the transport candidate needs to be changed */
  if (old_media != NULL)
    {
      if (!sdp_connection_cmp (sdp_media_connections (old_media), sdp_conn))
        transport_changed = FALSE;

      /* Disable sending at this point if it will be disabled
       * accordingly to the new direction */
      priv_update_sending (stream,
                           priv->direction & new_direction,
                           priv->pending_send_flags);
    }

  /* First add the new candidate, then update the codec set.
   * The offerer isn't supposed to send us anything from the new transport
   * until we accept; if it's the answer, both orderings have problems. */

  if (transport_changed)
    {
     /* Make sure we stop sending before we use the new set of codecs
      * intended for the new connection */
      sip_media_stream_set_sending (stream, FALSE);

      push_remote_candidates (stream);
    }

  push_remote_codecs (stream);

  priv->codec_intersect_pending = TRUE;

  /* TODO: this will go to session change commit code */

  /* note: for outbound sessions (for which remote cands become
   *       available at a later stage), emit active candidate pair 
   *       (and playing status?) once remote info set */
  push_active_candidate_pair (stream);

  /* Set the final direction and sending status */
  sip_media_stream_set_direction (stream,
                                  new_direction,
                                  priv->pending_send_flags);

  return TRUE;
}

/**
 * Converts a sofia-sip media type enum to Telepathy media type.
 * See <sofia-sip/sdp.h> and <telepathy-constants.h>.
 *
 * @return G_MAXUINT if the media type cannot be mapped
 */
guint
sip_tp_media_type (sdp_media_e sip_mtype)
{
  switch (sip_mtype)
    {
      case sdp_media_audio: return TP_MEDIA_STREAM_TYPE_AUDIO;
      case sdp_media_video: return TP_MEDIA_STREAM_TYPE_VIDEO; 
      default: return G_MAXUINT;
    }

  g_assert_not_reached();
}

/**
 * Sets the media state to playing or non-playing. When not playing,
 * received RTP packets may not be played locally.
 */
void sip_media_stream_set_playing (SIPMediaStream *stream, gboolean playing)
{
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  if (priv->playing == playing)
    return;

  DEBUG("set playing to %s", playing? "TRUE" : "FALSE");

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
sip_media_stream_set_sending (SIPMediaStream *stream, gboolean sending)
{
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  if (priv->sending == sending)
    return;

  DEBUG("set sending to %s", sending? "TRUE" : "FALSE");

  priv->sending = sending;

  if (priv->ready_received)
    tp_svc_media_stream_handler_emit_set_stream_sending (
        (TpSvcMediaStreamHandler *)stream, sending);
}

static void
priv_update_sending (SIPMediaStream *stream,
                     TpMediaStreamDirection direction,
                     guint pending_send_flags)
{
  sip_media_stream_set_sending (stream,
        (direction & TP_MEDIA_STREAM_DIRECTION_SEND)
        && !(pending_send_flags
             & (TP_MEDIA_STREAM_PENDING_REMOTE_SEND
                | TP_MEDIA_STREAM_PENDING_LOCAL_SEND)));
}

void
sip_media_stream_set_direction (SIPMediaStream *stream,
                                TpMediaStreamDirection direction,
                                guint pending_send_flags)
{
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  if (priv->direction != direction
      || priv->pending_send_flags != pending_send_flags)
    {
      DEBUG("setting direction %u, pending send flags %u", direction, pending_send_flags);

      priv->direction = direction;
      priv->pending_send_flags = pending_send_flags;

      /* TODO: SDP should not be cached, but created on demand */
      if (priv->native_cands_prepared && priv->native_codecs_prepared)
        priv_update_local_sdp (stream);

      g_signal_emit (stream, signals[SIG_DIRECTION_CHANGED], 0,
                     direction, pending_send_flags);
    }

  if (priv->remote_media != NULL)
    priv_update_sending (stream, direction, pending_send_flags);
}

/**
 * Returns true if the stream has a valid SDP description and
 * connection has been established with the stream engine.
 */
gboolean sip_media_stream_is_ready (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv;
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);
  g_assert (priv->stream_sdp == NULL || priv->ready_received);
  return (priv->stream_sdp != NULL);
}

gboolean
sip_media_stream_is_codec_intersect_pending (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->codec_intersect_pending;
}

void
sip_media_stream_start_telephony_event (SIPMediaStream *self, guchar event)
{
  tp_svc_media_stream_handler_emit_start_telephony_event (
        (TpSvcMediaStreamHandler *)self, event);
}

void
sip_media_stream_stop_telephony_event  (SIPMediaStream *self)
{
  tp_svc_media_stream_handler_emit_stop_telephony_event (
        (TpSvcMediaStreamHandler *)self);
}

static void
priv_generate_sdp (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->stream_sdp != NULL)
    return;

  priv_update_local_sdp (self);

  g_assert (priv->stream_sdp != NULL);

  g_signal_emit (self, signals[SIG_READY], 0);
}

/**
 * Notify StreamEngine of remote codecs.
 *
 * @pre Ready signal must be receiveid (priv->ready_received)
 */
static void push_remote_codecs (SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  GPtrArray *codecs;
  GType codecs_type;
  GType codec_type;
  const sdp_media_t *sdpmedia;
  const sdp_rtpmap_t *rtpmap;
  GHashTable *opt_params;

  DEBUG ("enter");

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  sdpmedia = priv->remote_media; 
  if (sdpmedia == NULL)
    {
      DEBUG("remote media description is not received yet");
      return;
    }

  if (!priv->ready_received)
    {
      DEBUG("the stream engine is not ready, SetRemoteCodecs is pending");
      priv->push_remote_requested = TRUE;
      return;
    }

  codec_type = sip_tp_codec_struct_type ();
  codecs_type = sip_tp_codec_list_type ();

  codecs = dbus_g_type_specialized_construct (codecs_type);

  opt_params = g_hash_table_new (g_str_hash, g_str_equal);

  rtpmap = sdpmedia->m_rtpmaps;
  while (rtpmap)
    {
      GValue codec = { 0, };

      g_value_init (&codec, codec_type);
      g_value_take_boxed (&codec,
                          dbus_g_type_specialized_construct (codec_type));

      /* FIXME: parse the optional parameters line for the codec
       * and populate the hash table */
      g_assert (g_hash_table_size (opt_params) == 0);

      /* RFC2327: see "m=" line definition 
       *  - note, 'encoding_params' is assumed to be channel
       *    count (i.e. channels in farsight) */

      dbus_g_type_struct_set (&codec,
                              /* payload type: */
                              0, rtpmap->rm_pt,
                              /* encoding name: */
                              1, rtpmap->rm_encoding,
                              /* media type */
                              2, (guint)priv->media_type,
                              /* clock-rate */
                              3, rtpmap->rm_rate,
                              /* number of supported channels: */
                              4, rtpmap->rm_params ? atoi(rtpmap->rm_params) : 0,
                              /* optional params: */
                              5, opt_params,
                              G_MAXUINT);

      g_ptr_array_add (codecs, g_value_get_boxed (&codec));

      rtpmap = rtpmap->rm_next;
    }
  
  g_assert (g_hash_table_size (opt_params) == 0);
  g_hash_table_destroy (opt_params);

  SESSION_DEBUG(priv->session, "passing %d remote codecs to stream engine",
                codecs->len);

  if (codecs->len > 0) {
    tp_svc_media_stream_handler_emit_set_remote_codecs (
        (TpSvcMediaStreamHandler *)stream, codecs);
  }

  g_boxed_free (codecs_type, codecs);
}

static void push_remote_candidates (SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  GValue candidate = { 0 };
  GValue transport = { 0 };
  GPtrArray *candidates;
  GPtrArray *transports;
  GType candidate_type;
  GType candidates_type;
  GType transport_type;
  GType transports_type;
  const sdp_media_t *media;
  const sdp_connection_t *sdp_conn;
  gchar *candidate_id;
  guint port;

  DEBUG("enter");

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  media = priv->remote_media; 
  if (media == NULL)
    {
      DEBUG("remote media description is not received yet");
      return;
    }

  if (!priv->ready_received)
    {
      DEBUG("the stream engine is not ready, SetRemoteCandidateList is pending");
      priv->push_remote_requested = TRUE;
      return;
    }

  /* use the address from SDP c-line as the only remote candidate */

  sdp_conn = sdp_media_connections (media);
  g_return_if_fail (sdp_conn != NULL);

  port = (guint) media->m_port;

  transport_type = sip_tp_transport_struct_type ();
  g_value_init (&transport, transport_type);
  g_value_take_boxed (&transport,
                      dbus_g_type_specialized_construct (transport_type));
  dbus_g_type_struct_set (&transport,
                          0, 0,         /* component number */
                          1, sdp_conn->c_address,
                          2, port,
                          3, TP_MEDIA_STREAM_BASE_PROTO_UDP,
                          4, "RTP",
                          5, "AVP",
                          /* 6, 0.0f, */
                          7, TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL,
                          /* 8, "", */
                          /* 9, "", */
                          G_MAXUINT);

  DEBUG("remote address=<%s>, port=<%u>", sdp_conn->c_address, port);

  transports_type = sip_tp_transport_list_type ();
  transports = dbus_g_type_specialized_construct (transports_type);
  g_ptr_array_add (transports, g_value_get_boxed (&transport));

  g_free (priv->remote_candidate_id);
  candidate_id = g_strdup_printf ("L%u", ++priv->remote_candidate_counter);
  priv->remote_candidate_id = candidate_id;

  candidate_type = sip_tp_candidate_struct_type ();
  g_value_init (&candidate, candidate_type);
  g_value_take_boxed (&candidate,
                      dbus_g_type_specialized_construct (candidate_type));
  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  candidates_type = sip_tp_candidate_list_type ();
  candidates = dbus_g_type_specialized_construct (candidates_type);
  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  DEBUG("emitting SetRemoteCandidateList with %s", candidate_id);

  tp_svc_media_stream_handler_emit_set_remote_candidate_list (
          (TpSvcMediaStreamHandler *)stream, candidates);

  g_boxed_free (candidates_type, candidates);
  g_boxed_free (transports_type, transports);

#if 0
  if (priv->playing) {
    g_debug ("%s: emitting SetStreamPlaying TRUE", G_STRFUNC);
    tp_svc_media_stream_handler_emit_set_stream_playing (
        (TpSvcMediaStreamHandler *)stream, TRUE);
  }
#endif
}

static void
push_active_candidate_pair (SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;

  DEBUG("enter");

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  if (priv->ready_received
      && priv->native_candidate_id != NULL
      && priv->remote_candidate_id != NULL)
    {
      DEBUG("emitting SetActiveCandidatePair for %s-%s",
            priv->native_candidate_id, priv->remote_candidate_id);
      tp_svc_media_stream_handler_emit_set_active_candidate_pair (
                stream, priv->native_candidate_id, priv->remote_candidate_id);
    }
}

static const char* priv_media_type_to_str(guint media_type)
{
switch (media_type)
  {
  case TP_MEDIA_STREAM_TYPE_AUDIO: return "audio";
  case TP_MEDIA_STREAM_TYPE_VIDEO: return "video";
  default: g_assert_not_reached ();
    ;
  }
return "-";
}

/**
* Refreshes the local SDP based on Farsight stream, and current
* object, state.
*/
static void
priv_update_local_sdp(SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  GString *mline;
  GString *alines;
  gchar *cline;
  GValue transport = { 0 };
  GValue codec = { 0, };
  const GPtrArray *codecs, *candidates;
  gchar *tr_addr = NULL;
  /* gchar *tr_user = NULL; */
  /* gchar *tr_pass = NULL; */
  gchar *tr_subtype = NULL;
  gchar *tr_profile = NULL;
  guint tr_port;
  /* guint tr_component; */
  guint tr_proto;
  /* guint tr_type; */
  /* gdouble tr_pref; */
  const gchar *dirline;
  const gchar *iproto;
  int i;

  /* Note: impl limits ...
   * - no multi-stream support
   * - no IPv6 support (missing from the Farsight API?)
   */

  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  candidates = g_value_get_boxed (&priv->native_candidates);
  codecs = g_value_get_boxed (&priv->native_codecs);

  g_value_init (&transport, sip_tp_transport_struct_type ());
  g_value_init (&codec, sip_tp_codec_struct_type ());

  /* Find the last acceptable candidate */ 

  for (i = candidates->len - 1; i >= 0; --i)
    {
      GValueArray *candidate;
      const GPtrArray *ca_tports;

      candidate = g_ptr_array_index (candidates, i);
      ca_tports = g_value_get_boxed (g_value_array_get_nth (candidate, 1));

      g_return_if_fail (ca_tports->len >= 1);

      g_value_set_static_boxed (&transport, g_ptr_array_index (ca_tports, 0));

      dbus_g_type_struct_get (&transport,
                              /* 0, &tr_component, */
                              1, &tr_addr,
                              2, &tr_port,
                              3, &tr_proto,
                              4, &tr_subtype,
                              5, &tr_profile,
                              /* 6, &tr_pref, */
                              /* 7, &tr_type, */
                              /* 8, &tr_user, */
                              /* 9, &tr_pass, */
                              G_MAXUINT);

      if (tr_proto == TP_MEDIA_STREAM_BASE_PROTO_UDP)
        break;
    }
  g_return_if_fail (i >= 0);
  g_return_if_fail (tr_addr != NULL);
  g_return_if_fail (tr_subtype != NULL);
  g_return_if_fail (tr_profile != NULL);

  mline = g_string_new ("m=");
  g_string_append_printf (mline,
                          "%s %u %s/%s",
                          priv_media_type_to_str (priv->media_type),
                          tr_port,
                          tr_subtype,
                          tr_profile);

  iproto = (strchr (tr_addr, ':') == NULL)? "IP4" : "IP6";
  cline = g_strdup_printf ("c=IN %s %s\r\n", iproto, tr_addr);

  switch (priv->direction)
    {
    case TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL:
      dirline = "";
      break;
    case TP_MEDIA_STREAM_DIRECTION_SEND:
      dirline = "a=sendonly\r\n";
      break;
    case TP_MEDIA_STREAM_DIRECTION_RECEIVE:
      dirline = "a=recvonly\r\n";
      break;
    case TP_MEDIA_STREAM_DIRECTION_NONE:
      dirline = "a=inactive\r\n";
      break;
    default:
      g_assert_not_reached();
    }

  alines = g_string_new (dirline);

  for (i = 0; i < codecs->len; i++) {
    guint co_id, co_type, co_clockrate, co_channels;
    gchar *co_name;
    GHashTable *co_params;

    g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));

    dbus_g_type_struct_get (&codec,
			    0, &co_id,
			    1, &co_name,
			    2, &co_type, 
			    3, &co_clockrate,
			    4, &co_channels,
                            5, &co_params,
			    G_MAXUINT);

    g_return_if_fail (co_type == priv->media_type);

    /* Add rtpmap entry to media a-lines */
    g_string_append_printf (alines,
                            "a=rtpmap:%u %s/%u",
                            co_id,
                            co_name,
                            co_clockrate);
    if (co_channels > 1)
      g_string_append_printf (alines, "/%u", co_channels);
    g_string_append (alines, "\r\n");

    /* TODO: marshal parameters into the fmtp attribute */

    /* Add PT id to mline */
    g_string_append_printf (mline, " %u", co_id);

    g_free (co_name);
    g_hash_table_destroy (co_params);
  }

  g_free(priv->stream_sdp);
  priv->stream_sdp = g_strconcat(mline->str, "\r\n",
                                 cline,
                                 "b=RS:0\r\nb=RR:0\r\n",
                                 alines->str,
                                 NULL);

  g_free (tr_addr);
  g_free (tr_profile);
  g_free (tr_subtype);
  /* g_free (tr_user); */
  /* g_free (tr_pass); */

  g_string_free (mline, TRUE);
  g_free (cline);
  g_string_free (alines, TRUE);
}

static void
stream_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaStreamHandlerClass *klass = (TpSvcMediaStreamHandlerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_media_stream_handler_implement_##x (\
    klass, (tp_svc_media_stream_handler_##x##_impl) sip_media_stream_##x)
  IMPLEMENT(codec_choice);
  IMPLEMENT(error);
  IMPLEMENT(native_candidates_prepared);
  IMPLEMENT(new_active_candidate_pair);
  IMPLEMENT(new_native_candidate);
  IMPLEMENT(ready);
  /* IMPLEMENT(set_local_codecs); */
  IMPLEMENT(stream_state);
  IMPLEMENT(supported_codecs);
#undef IMPLEMENT
}
