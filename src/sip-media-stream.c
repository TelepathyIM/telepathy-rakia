/*
 * sip-media-stream.c - Source for SIPMediaStream
 * Copyright (C) 2006 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "config.h"

#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "sip-media-channel.h"
#include "sip-media-session.h"

#include "sip-media-stream.h"
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
    SIG_NEW_ACTIVE_CANDIDATE_PAIR,
    SIG_NEW_NATIVE_CANDIDATE,
    SIG_READY,
    SIG_SUPPORTED_CODECS,

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
  LAST_PROPERTY
};

/* private structure */
typedef struct _SIPMediaStreamPrivate SIPMediaStreamPrivate;

struct _SIPMediaStreamPrivate
{
  SIPConnection *conn;
  SIPMediaSession *session;       /** see gobj. prop. 'media-session' */
  gchar *object_path;             /** see gobj. prop. 'object-path' */
  guint id;                       /** see gobj. prop. 'id' */
  guint media_type;               /** see gobj. prop. 'media-type' */

  gchar *stream_sdp;              /** SDP description of the stream */
  
  gboolean sdp_generated;
  gboolean ready_received;        /** our ready method has been called */
  gboolean native_cands_prepared; /** all candidates discovered */
  gboolean native_codecs_prepared; /** all codecs discovered */
  gboolean playing;               /** stream set to playing */

  GValue native_codecs;           /** intersected codec list */
  GValue native_candidates;

  GValue remote_codecs;
  GValue remote_candidates;
  gboolean push_remote_requested;
  gboolean remote_cands_sent;

  gboolean dispose_has_run;
};

#define SIP_MEDIA_STREAM_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_STREAM, SIPMediaStreamPrivate))

static gboolean priv_set_remote_codecs(SIPMediaStream *stream,
                                       const sdp_media_t *sdpmedia);
static void push_remote_codecs (SIPMediaStream *stream);
static void push_remote_candidates (SIPMediaStream *stream);
static int priv_update_local_sdp(SIPMediaStream *stream);
static void priv_generate_sdp (SIPMediaStream *stream);

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
sip_media_stream_init (SIPMediaStream *obj)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  /* allocate any data required by the object here */

  priv = NULL;
}

static GObject *
sip_media_stream_constructor (GType type, guint n_props,
			      GObjectConstructParam *props)
{
  GObject *obj;
  SIPMediaStreamPrivate *priv;
  SIPMediaChannel *chan;
  DBusGConnection *bus;

  /* call base class constructor */
  obj = G_OBJECT_CLASS (sip_media_stream_parent_class)->
           constructor (type, n_props, props);
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (SIP_MEDIA_STREAM (obj));

  /* get the connection handle once */
  g_object_get (priv->session, "media-channel", &chan, NULL);
  g_object_get (chan, "connection", &priv->conn, NULL);
  g_object_unref (chan);

  priv->playing = FALSE;

  g_value_init (&priv->native_codecs, sip_tp_codec_list_type ());
  g_value_take_boxed (&priv->native_codecs,
      dbus_g_type_specialized_construct (sip_tp_codec_list_type ()));

  g_value_init (&priv->native_candidates, sip_tp_candidate_list_type ());
  g_value_take_boxed (&priv->native_candidates,
      dbus_g_type_specialized_construct (sip_tp_candidate_list_type ()));

  g_value_init (&priv->remote_codecs, sip_tp_codec_list_type ());
  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (sip_tp_codec_list_type ()));

  g_value_init (&priv->remote_candidates, sip_tp_candidate_list_type ());
  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (sip_tp_candidate_list_type ()));

  priv->native_cands_prepared = FALSE;
  priv->native_codecs_prepared = FALSE;

  priv->push_remote_requested = FALSE;

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

  switch (property_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
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

  switch (property_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
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

  /* signals not exported by DBus interface */
  signals[SIG_NEW_ACTIVE_CANDIDATE_PAIR] =
    g_signal_new ("new-active-candidate-pair",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tpsip_marshal_VOID__STRING_STRING,
                  G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  signals[SIG_NEW_NATIVE_CANDIDATE] =
    g_signal_new ("new-native-candidate",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _tpsip_marshal_VOID__STRING_BOXED,
                  G_TYPE_NONE, 2, G_TYPE_STRING, sip_tp_transport_list_type ());

  signals[SIG_READY] =
    g_signal_new ("ready",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, sip_tp_codec_list_type ());

  signals[SIG_SUPPORTED_CODECS] =
    g_signal_new ("supported-codecs",
                  G_OBJECT_CLASS_TYPE (sip_media_stream_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE, 1, sip_tp_codec_list_type ());
}

void
sip_media_stream_dispose (GObject *object)
{
  SIPMediaStream *self = SIP_MEDIA_STREAM (object);
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release ref taken in constructor */
  g_object_unref (priv->conn);

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

  g_value_unset (&priv->remote_codecs);
  g_value_unset (&priv->remote_candidates);

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

  g_debug ("%s: unexpected - stream-engine called NativeCandidatesPrepared, possibly in non-libjingle mode", G_STRFUNC);

  g_assert (SIP_IS_MEDIA_STREAM (obj));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);
  
  priv->native_cands_prepared = TRUE;
  if (priv->native_codecs_prepared == TRUE) {
    priv_generate_sdp(obj);
  }

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

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  g_signal_emit (obj, signals[SIG_NEW_ACTIVE_CANDIDATE_PAIR], 0,
                 native_candidate_id, remote_candidate_id);

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
  SIPMediaSessionState state;
  GPtrArray *candidates;
  GValue candidate = { 0, };
  GValueArray *transport;
  const gchar *addr;

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  g_object_get (priv->session, "state", &state, NULL);

  /* FIXME: maybe this should be an assertion in case the channel
   * isn't closed early enough right now? */
  if (state > SIP_MEDIA_SESSION_STATE_ACTIVE) {
    g_debug ("%s: state > SIP_MEDIA_SESSION_STATE_ACTIVE, doing nothing", G_STRFUNC);
    tp_svc_media_stream_handler_return_from_new_native_candidate (context);
    return;
  }

  if (priv->sdp_generated == TRUE) {
    g_debug ("%s: SDP for stream already generated, ignoring candidate '%s'", G_STRFUNC, candidate_id);
      tp_svc_media_stream_handler_return_from_new_native_candidate (context);
      return;
  }

  candidates = g_value_get_boxed (&priv->native_candidates);

  g_value_init (&candidate, sip_tp_candidate_struct_type ());
  g_value_set_static_boxed (&candidate,
      dbus_g_type_specialized_construct (sip_tp_candidate_struct_type ()));

  dbus_g_type_struct_set (&candidate,
      0, candidate_id,
      1, transports,
      G_MAXUINT);

  transport = g_ptr_array_index (transports, 0);
  addr = g_value_get_string (g_value_array_get_nth (transport, 1));
  if (!strcmp (addr, "127.0.0.1"))
    {
      SESSION_DEBUG(priv->session, "ignoring native localhost candidate");
      tp_svc_media_stream_handler_return_from_new_native_candidate (context);
      return;
    }

  g_ptr_array_add (candidates, g_value_get_boxed (&candidate));

  SESSION_DEBUG(priv->session, "put 1 native candidate from stream-engine into cache");

  if (candidates->len > 1 &&
      priv->native_codecs_prepared == TRUE &&
      priv->sdp_generated != TRUE) {
    priv_generate_sdp(obj);
  }
  
  g_signal_emit (obj, signals[SIG_NEW_NATIVE_CANDIDATE], 0,
                 candidate_id, transports);

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

  SESSION_DEBUG(priv->session, "putting list of all %d locally supported "
                "codecs from stream-engine into cache", codecs->len);
  g_value_init (&val, sip_tp_codec_list_type ());
  g_value_set_static_boxed (&val, codecs);
  g_value_copy (&val, &priv->native_codecs);

  priv->native_codecs_prepared = TRUE;
  if (priv->native_cands_prepared == TRUE &&
      priv->sdp_generated != TRUE) {
    priv_generate_sdp(obj);
  }

  if (priv->push_remote_requested) {
    push_remote_candidates (obj);
    push_remote_codecs (obj);
  }

  /* note: for inbound sessions, emit active candidate pair once 
           remote info is set */
  if (priv->remote_cands_sent == TRUE) {
    /* XXX: hack, find out the correct candidate ids from somewhere */
    g_debug ("emitting SetActiveCandidatePair for L1-L1 (2)");
    tp_svc_media_stream_handler_emit_set_active_candidate_pair (
        iface, "L1", "L1");
  }

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

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  sip_media_session_stream_state (priv->session, priv->id, state);

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

  SIPMediaStream *obj = SIP_MEDIA_STREAM (iface);
  SIPMediaStreamPrivate *priv;
  g_assert (SIP_IS_MEDIA_STREAM (obj));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  SESSION_DEBUG(priv->session, "got codec intersection containing %d "
                "codecs from stream-engine", codecs->len);

  /* store the intersection for later on */
  g_value_set_boxed (&priv->native_codecs, codecs);

  g_signal_emit (obj, signals[SIG_SUPPORTED_CODECS], 0, codecs);

  tp_svc_media_stream_handler_return_from_supported_codecs (context);
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

guint
sip_media_stream_get_media_type (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);
  return priv->media_type;
}

void
sip_media_stream_close (SIPMediaStream *self)
{
  tp_svc_media_stream_handler_emit_close ((TpSvcMediaStreamHandler *) self);
}

/**
 * Described the local stream configuration in SDP (RFC2327),
 * or NULL if stream not configured yet.
 */
const char *sip_media_stream_local_sdp (SIPMediaStream *obj)
{
  SIPMediaStreamPrivate *priv;
  g_assert (SIP_IS_MEDIA_STREAM (obj));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  if (priv->sdp_generated != TRUE) {
    g_warning ("Stream not in ready state, cannot describe SDP.");
    return NULL;
  }

  return priv->stream_sdp;
}


/** 
 * Sets the remote candidates and codecs for this stream, as 
 * received via signaling.
 * 
 * Parses the SDP information, generated TP candidates and 
 * stores the information to 'priv->remote_candidates'.
 */
gboolean
sip_media_stream_set_remote_info (SIPMediaStream *stream,
                                  const sdp_media_t *media)
{
  SIPMediaStreamPrivate *priv;
  gboolean res = TRUE;
  GPtrArray *tp_candidates;
  GValue tp_candidate = { 0, };
  GPtrArray *tp_transports;
  GValue tp_transport = { 0, };
  unsigned long r_port = media->m_port;
  sdp_connection_t *sdp_conns;

  DEBUG ("enter");

  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  tp_candidates = g_value_get_boxed (&priv->remote_candidates);

  g_value_init (&tp_candidate, sip_tp_candidate_struct_type ());
  g_value_init (&tp_transport, sip_tp_transport_struct_type ());

  /* use the address from SDP c-line as the only remote candidate */

  sdp_conns = sdp_media_connections(media);
  if (sdp_conns && r_port > 0)
    {
      /* remote side does not support ICE/jingle */
      g_value_take_boxed (&tp_transport,
                          dbus_g_type_specialized_construct (sip_tp_transport_struct_type ()));

      dbus_g_type_struct_set (&tp_transport,
                              0, "0",         /* component number */
                              1, sdp_conns->c_address,
                              2, r_port,
                              3, "UDP",
                              4, "RTP",
                              5, "AVP",
                              6, 0.0f, /* qvalue */
                              7, "local",
                              8, "",
                              9, "",
                              G_MAXUINT);

      DEBUG("c_address=<%s>, c_port=<%lu>", sdp_conns->c_address, r_port);

      tp_transports = g_ptr_array_sized_new (1);
      g_ptr_array_add (tp_transports, g_value_get_boxed (&tp_transport));

      g_value_take_boxed (&tp_candidate,
                          dbus_g_type_specialized_construct (sip_tp_candidate_struct_type ()));

      dbus_g_type_struct_set (&tp_candidate,
                              0, "L1", /* candidate id */
                              1, tp_transports,
                              G_MAXUINT);

      g_ptr_array_add (tp_candidates, g_value_get_boxed (&tp_candidate));
    }
  else
    {
      g_warning ("No valid remote candidates, unable to configure stream engine for sending.");
      res = FALSE;
    }

  if (res == TRUE) {
    /* note: convert from sdp to priv->remote_codecs */
    res = priv_set_remote_codecs (stream, media);
  
    if (priv->ready_received) {
      push_remote_candidates (stream);
      push_remote_codecs (stream);
    }
    else {
      /* cannot push until the local candidates are available */
      priv->push_remote_requested = TRUE;
    }
  }

  return res;
}

static gboolean priv_set_remote_codecs(SIPMediaStream *stream,
                                       const sdp_media_t *sdpmedia)
{
  SIPMediaStreamPrivate *priv;
  gboolean res = TRUE;
  GPtrArray *codecs;
  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  DEBUG ("enter");

  codecs = g_value_get_boxed (&priv->remote_codecs);

  while (sdpmedia) {
    if (sdpmedia->m_type ==  sdp_media_audio ||
	sdpmedia->m_type == sdp_media_video) {
      sdp_rtpmap_t *rtpmap = sdpmedia->m_rtpmaps;
      while (rtpmap) {
	GValue codec = { 0, };
	
	g_value_init (&codec, sip_tp_codec_struct_type ());
	g_value_take_boxed (&codec,
			    dbus_g_type_specialized_construct (sip_tp_codec_struct_type ()));
	
	/* RFC2327: see "m=" line definition 
	 *  - note, 'encoding_params' is assumed to be channel
	 *    count (i.e. channels in farsight) */ 
	
	dbus_g_type_struct_set (&codec,
				/* payload type: */
				0, rtpmap->rm_pt,
				/* encoding name: */
				1, rtpmap->rm_encoding,
				/* media type */
				2, (sdpmedia->m_type == sdp_media_audio ? 
				    TP_MEDIA_STREAM_TYPE_AUDIO : TP_MEDIA_STREAM_TYPE_VIDEO),
				/* clock-rate */
				3, rtpmap->rm_rate,
				/* number of supported channels: */
				4, rtpmap->rm_params ? atoi(rtpmap->rm_params) : 0,
				/* optional params: */
				5, g_hash_table_new (g_str_hash, g_str_equal),
				G_MAXUINT);
	
	g_ptr_array_add (codecs, g_value_get_boxed (&codec));
	
	rtpmap = rtpmap->rm_next;
      }
      
      /* note: only describes the first matching audio/video media */
      break;
    }

    sdpmedia = sdpmedia->m_next;
  }
  
  return res;
}

/**
 * Sets the media state to playing or disabled. When not playing,
 * received RTP packets are not played locally, nor recorded audio
 * is sent to the network.
 */
void sip_media_stream_set_playing (SIPMediaStream *stream, gboolean playing)
{
  SIPMediaStreamPrivate *priv;
  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  g_debug ("emitting SetStreamPlaying signal with %d", playing);
  priv->playing = playing;

  if (priv->ready_received) {
    g_debug ("%s: emitting SetStreamPlaying TRUE", G_STRFUNC);
    tp_svc_media_stream_handler_emit_set_stream_playing (
        (TpSvcMediaStreamHandler *)stream, playing);
  }
}

/**
 * Returns true if the stream has a valid SDP description and
 * connection has been established with the stream engine.
 */
gboolean sip_media_stream_is_ready (SIPMediaStream *self)
{
  SIPMediaStreamPrivate *priv;
  g_assert (SIP_IS_MEDIA_STREAM (self));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (self);

  return priv->sdp_generated && priv->ready_received;
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

#if 0
static void priv_session_stream_state_changed_cb (SIPMediaSession *session,
						  GParamSpec *arg1,
						  SIPMediaStream *stream)
{
  SIPMediaSessionState state;

  g_object_get (session, "state", &state, NULL);
  g_debug ("stream state cb: session js-state to %d.", state);
}
#endif

static void priv_generate_sdp (SIPMediaStream *obj)
{
  SIPMediaStreamPrivate *priv;
  GPtrArray *codecs;

  g_assert (SIP_IS_MEDIA_STREAM (obj));

  priv_update_local_sdp (obj);

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (obj);

  priv->sdp_generated = TRUE;

  codecs = g_value_get_boxed (&priv->native_codecs);
  g_signal_emit (obj, signals[SIG_READY], 0, codecs);
}

/**
 * Notify StreamEngine of remote codecs.
 *
 * @pre Ready signal must be receiveid (priv->ready_received)
 */
static void push_remote_codecs (SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  SIPMediaSessionState state;
  GPtrArray *codecs;

  DEBUG ("enter");

  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  g_assert (priv);
  g_assert (priv->ready_received);

  g_object_get (priv->session, "state", &state, NULL);

  codecs = g_value_get_boxed (&priv->remote_codecs);

  SESSION_DEBUG(priv->session, "passing %d remote codecs to stream-engine",
                codecs->len);

  if (codecs->len > 0) {
    tp_svc_media_stream_handler_emit_set_remote_codecs (
        (TpSvcMediaStreamHandler *)stream, codecs);

    /* XXX: would g_value_unset() be sufficient here? */
    g_value_take_boxed (&priv->remote_codecs,
                        dbus_g_type_specialized_construct (
                                sip_tp_codec_list_type ()));
  }
}

static void push_remote_candidates (SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  SIPMediaSessionState state;
  GPtrArray *candidates;
  guint i;

  DEBUG ("enter");

  g_assert (SIP_IS_MEDIA_STREAM (stream));

  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  g_assert (priv);
  g_assert (priv->ready_received);

  candidates = g_value_get_boxed (&priv->remote_candidates);

  g_object_get (priv->session, "state", &state, NULL);
  g_assert (state < SIP_MEDIA_SESSION_STATE_ENDED);

  g_debug ("%s: number of candidates to push %d.", G_STRFUNC, candidates->len);

  for (i = 0; i < candidates->len; i++)
    {
      GValueArray *candidate = g_ptr_array_index (candidates, i);
      const gchar *candidate_id;
      const GPtrArray *transports;

      candidate_id = g_value_get_string (g_value_array_get_nth (candidate, 0));
      transports = g_value_get_boxed (g_value_array_get_nth (candidate, 1));

      SESSION_DEBUG(priv->session,
                    "passing 1 remote candidate to stream-engine");

      tp_svc_media_stream_handler_emit_add_remote_candidate (
          (TpSvcMediaStreamHandler *)stream, candidate_id, transports);
    }

  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (sip_tp_candidate_list_type ()));

  priv->remote_cands_sent = TRUE;

  /* note: for outbound sessions (for which remote cands become
   *       available at a later stage), emit active candidate pair 
   *       and playing status once remote info set */

  if (priv->ready_received) {
    /* XXX: hack, find out the correct candidate ids from somewhere */
    g_debug ("%s: emitting SetActiveCandidatePair for L1-L1", G_STRFUNC);
    tp_svc_media_stream_handler_emit_set_active_candidate_pair (
        (TpSvcMediaStreamHandler *)stream, "L1", "L1");
  }

  if (priv->playing) {
    g_debug ("%s: emitting SetStreamPlaying TRUE", G_STRFUNC);
    tp_svc_media_stream_handler_emit_set_stream_playing (
        (TpSvcMediaStreamHandler *)stream, TRUE);
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
static int priv_update_local_sdp(SIPMediaStream *stream)
{
  SIPMediaStreamPrivate *priv;
  const char *c_sdp_version = "v=0\r\n"; 
  const char *c_crlf = "\r\n";
  gchar *tmpa_str = NULL, *tmpb_str;
  gchar *aline_str = NULL, *cline_str = NULL, *mline_str = NULL, *malines_str = NULL;
  GValue transport = { 0 };
  GValue codec = { 0, };
  GValue candidate = { 0 };
  const GPtrArray *codecs, *candidates;
  const gchar *ca_id;
  const GPtrArray *ca_tports;
  gchar *tr_addr;
  gchar *tr_user = NULL;
  gchar *tr_pass = NULL;
  gchar *tr_subtype, *tr_profile;
  gulong tr_port, tr_component;
  gdouble tr_pref;
  TpMediaStreamBaseProto tr_proto;
  TpMediaStreamTransportType tr_type;
  int i;
  /* int result = -1; */

  /* Note: impl limits ...
   * - no multi-stream support
   * - no IPv6 support (missing from the Farsight API?)
   */

  g_assert (SIP_IS_MEDIA_STREAM (stream));
  priv = SIP_MEDIA_STREAM_GET_PRIVATE (stream);

  candidates = g_value_get_boxed (&priv->native_candidates);
  codecs = g_value_get_boxed (&priv->native_codecs);

  g_value_init (&candidate, sip_tp_candidate_struct_type ());
  g_value_init (&transport, sip_tp_transport_struct_type ());
  g_value_init (&codec, sip_tp_codec_struct_type ());

  for (i = 0; i < candidates->len; i++) {
    g_value_set_static_boxed (&candidate, g_ptr_array_index (candidates, i));

    dbus_g_type_struct_get (&candidate,
			    0, &ca_id,
			    1, &ca_tports,
			    G_MAXUINT);

    g_assert (ca_tports->len >= 1);

    /* XXX: should select the most preferable transport
     * or use some other criteria */
    g_value_set_static_boxed (&transport, g_ptr_array_index (ca_tports, 0));

    dbus_g_type_struct_get (&transport,
			    0, &tr_component,	
			    1, &tr_addr,
			    2, &tr_port,
			    3, &tr_proto,
			    4, &tr_subtype,
			    5, &tr_profile,
			    6, &tr_pref,
			    7, &tr_type,
			    8, &tr_user,
			    9, &tr_pass,
			    G_MAXUINT);

    if (i == candidates->len - 1) {
      /* generate the c= line based on last candidate */
      if (tr_proto == TP_MEDIA_STREAM_BASE_PROTO_UDP) {
	cline_str = g_strdup_printf("c=IN IP4 %s%s", tr_addr, c_crlf);
	/* leave end of 'mline_str' for PT ids */
	mline_str = g_strdup_printf("m=%s %lu %s/%s", 
				    priv_media_type_to_str (priv->media_type), 
				    tr_port, tr_subtype, tr_profile);
      }
      else {
	cline_str = g_strdup("c=IN IP4 0.0.0.0\r\n");
	/* no transport, so need to check codecs */
      }
    }

    /* for ice-09, the "typ" attribute needs to be added for srflx and
     * relay attributes */
  }

  malines_str = g_strdup ("");

  for (i = 0; i < codecs->len; i++) {
    guint co_id, co_type, co_clockrate, co_channels;
    gchar *co_name;
    
    g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));
    
    dbus_g_type_struct_get (&codec,
			    0, &co_id,
			    1, &co_name,
			    2, &co_type, 
			    3, &co_clockrate,
			    4, &co_channels,
			    G_MAXUINT);
    
    /* step: add entry to media a-lines */
    tmpa_str = g_strdup_printf("%sa=rtpmap:%u %s/%u", 
			       malines_str,
			       co_id,
			       co_name,
			       co_clockrate);
    if (co_channels > 1) {
      tmpb_str = g_strdup_printf("%s/%u%s", 
				 tmpa_str,
				 co_channels,
				 c_crlf);
    }
    else {
      tmpb_str = g_strdup_printf("%s%s", 
				 tmpa_str,
				 c_crlf);
    }
    g_free (malines_str);
    g_free (tmpa_str);
    malines_str = tmpb_str;

    /* step: add PT id to mline */
    tmpa_str = mline_str;
    tmpb_str = g_strdup_printf(" %u", co_id);
    mline_str = g_strconcat(mline_str, tmpb_str, NULL);
    g_free(tmpa_str), g_free(tmpb_str);
  }
  

  SESSION_DEBUG(priv->session,
                "from Telepathy DBus struct: [\"%s\", [1, \"%s\", %d, %s, "
                "\"RTP\", \"AVP\", %f, %s, \"%s\", \"%s\"]]",
                ca_id, tr_addr, tr_port, debug_tp_protocols[tr_proto],
                tr_pref, debug_tp_transports[tr_type], tr_user, tr_pass);

  tmpa_str = g_strconcat(c_sdp_version, mline_str, c_crlf, cline_str, malines_str, NULL);

  g_free(priv->stream_sdp);
  priv->stream_sdp = tmpa_str;
  DEBUG("Updated stream SDP:{\n%s}", priv->stream_sdp);

  g_free(tr_addr);
  g_free(tr_user);
  g_free(tr_pass);
  g_free(tr_profile);
  g_free(tr_subtype);

  g_free(aline_str);
  g_free(cline_str);
  g_free(mline_str);
  g_free(malines_str);

  return 0;
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
