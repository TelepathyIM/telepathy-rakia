/*
 * sip-media-session.c - Source for SIPMediaSession
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-session).
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
#include <time.h>
#include <string.h>

#include <sofia-sip/sip_status.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "config.h"

#include "sip-media-session.h"
#include "sip-media-channel.h"
#include "sip-media-stream.h"
#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "telepathy-helpers.h"

#define DEBUG_FLAG SIP_DEBUG_MEDIA
#include "debug.h"

static void session_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(SIPMediaSession,
    sip_media_session,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init)
    )

#define DEFAULT_SESSION_TIMEOUT 50000

/* signal enum */
enum
{
  SIG_STREAM_ADDED,
  SIG_LAST_SIGNAL
};

/* properties */
enum
{
  PROP_MEDIA_CHANNEL = 1,
  PROP_OBJECT_PATH,
  PROP_SESSION_ID,
  PROP_INITIATOR,
  PROP_PEER,
  PROP_STATE,
  LAST_PROPERTY
};

static guint signals[SIG_LAST_SIGNAL] = {0};

#ifdef ENABLE_DEBUG

/**
 * StreamEngine session states:
 * - pending-created, objects created, local cand/codec query ongoing
 * - pending-initiated, 'Ready' signal received
 * - active, remote codecs delivered to StreamEngine (note,
 *   SteamEngine might still fail to verify connectivity and report
 *   an error)
 * - ended, session has ended
 */
static const char* session_states[] =
{
    "pending-created",
    "pending-initiated",
    "active",
    "ended"
};

#endif /* ENABLE_DEBUG */

/* private structure */
typedef struct _SIPMediaSessionPrivate SIPMediaSessionPrivate;

struct _SIPMediaSessionPrivate
{
  SIPMediaChannel *channel;             /** see gobj. prop. 'media-channel' */
  gchar *object_path;                   /** see gobj. prop. 'object-path' */
  gchar *id;                            /** see gobj. prop. 'session-id' */
  TpHandle initiator;                   /** see gobj. prop. 'initator' */
  TpHandle peer;                        /** see gobj. prop. 'peer' */
  SIPMediaSessionState state;           /** see gobj. prop. 'state' */
  guint timer_id;
  su_home_t *home;                      /** memory home for Sofia objects */
  sdp_session_t *remote_sdp;            /** last received remote session */
  gboolean accepted;                    /**< session has been locally accepted for use */
  gboolean oa_pending;                  /**< offer/answer waiting to be sent */
  gboolean se_ready;                    /**< connection established with stream-engine */
  gboolean dispose_has_run;
  GPtrArray *streams;
};

#define SIP_MEDIA_SESSION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_SESSION, SIPMediaSessionPrivate))

static void sip_media_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec);
static void sip_media_session_set_property (GObject      *object,
					    guint         property_id,
					    const GValue *value,
					    GParamSpec   *pspec);
static gboolean priv_timeout_session (gpointer data);
static SIPMediaStream* priv_create_media_stream (SIPMediaSession *session, guint media_type);

static nua_handle_t *priv_get_nua_handle_for_session (SIPMediaSession *session);
static void priv_offer_answer_step (SIPMediaSession *session);

static void sip_media_session_init (SIPMediaSession *obj)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (obj);

  g_debug ("%s called", G_STRFUNC);

  /* allocate any data required by the object here */
  priv->home = su_home_create ();
  priv->streams = g_ptr_array_new ();
}

static GObject *
sip_media_session_constructor (GType type, guint n_props,
			       GObjectConstructParam *props)
{
  GObject *obj;
  SIPMediaSessionPrivate *priv;
  DBusGConnection *bus;

  obj = G_OBJECT_CLASS (sip_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = SIP_MEDIA_SESSION_GET_PRIVATE (SIP_MEDIA_SESSION (obj));

  priv->state = SIP_MEDIA_SESSION_STATE_PENDING_CREATED;

  /* note: session is always created to either create a new outbound
   *       request for a media channel, or to respond to an incoming 
   *       request ... thus oa_pending is TRUE at start */
  priv->oa_pending = TRUE;

  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void sip_media_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
  SIPMediaSession *session = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id) {
    case PROP_MEDIA_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_SESSION_ID:
      g_value_set_string (value, priv->id);
      break;
    case PROP_INITIATOR:
      g_value_set_uint (value, priv->initiator);
      break;
    case PROP_PEER:
      g_value_set_uint (value, priv->peer);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void priv_session_state_changed (SIPMediaSession *session,
					SIPMediaSessionState prev_state,
					SIPMediaSessionState new_state);

static void sip_media_session_set_property (GObject      *object,
					    guint         property_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
  SIPMediaSession *session = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  SIPMediaSessionState prev_state;

  switch (property_id) {
    case PROP_MEDIA_CHANNEL:
      priv->channel = SIP_MEDIA_CHANNEL (g_value_dup_object (value));
      break;
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_SESSION_ID:
      g_free (priv->id);
      priv->id = g_value_dup_string (value);
      break;
    case PROP_INITIATOR:
      priv->initiator = g_value_get_uint (value);
      break;
    case PROP_PEER:
      priv->peer = g_value_get_uint (value);
      break;
    case PROP_STATE:
      prev_state = priv->state;
      priv->state = g_value_get_uint (value);

      if (priv->state != prev_state)
        priv_session_state_changed (session, prev_state, priv->state);

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void sip_media_session_dispose (GObject *object);
static void sip_media_session_finalize (GObject *object);

static void
sip_media_session_class_init (SIPMediaSessionClass *sip_media_session_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (sip_media_session_class);
  GParamSpec *param_spec;

  g_type_class_add_private (sip_media_session_class, sizeof (SIPMediaSessionPrivate));

  object_class->constructor = sip_media_session_constructor;

  object_class->get_property = sip_media_session_get_property;
  object_class->set_property = sip_media_session_set_property;

  object_class->dispose = sip_media_session_dispose;
  object_class->finalize = sip_media_session_finalize;

  param_spec = g_param_spec_object ("media-channel", "SIPMediaChannel object",
                                    "SIP media channel object that owns this "
                                    "media session object.",
                                    SIP_TYPE_MEDIA_CHANNEL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("session-id", "Session ID",
                                    "A unique session identifier used "
                                    "throughout all communication.",
				    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SESSION_ID, param_spec);

  param_spec = g_param_spec_uint ("initiator", "Session initiator",
                                  "The TpHandle representing the contact "
                                  "who initiated the session.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_INITIATOR, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The TpHandle representing the contact "
                                  "with whom this session communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_uint ("state", "Session state",
                                  "The current state that the session is in.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STATE, param_spec);

  signals[SIG_STREAM_ADDED] =
    g_signal_new ("stream-added",
                  G_OBJECT_CLASS_TYPE (sip_media_session_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static void
sip_media_session_dispose (GObject *object)
{
  SIPMediaSession *self = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->timer_id)
    g_source_remove (priv->timer_id);

  if (priv->channel)
    g_object_unref (priv->channel);

  if (G_OBJECT_CLASS (sip_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_session_parent_class)->dispose (object);

  DEBUG ("exit");
}

void
sip_media_session_finalize (GObject *object)
{
  SIPMediaSession *self = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  guint i;

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (sip_media_session_parent_class)->finalize (object);

  for (i = 0; i < priv->streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index (priv->streams, i);
    if (stream)
      g_object_unref (stream);
  }

  g_ptr_array_free(priv->streams, TRUE);

  su_home_unref (priv->home);

  DEBUG ("exit");
}



/**
 * sip_media_session_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
sip_media_session_error (TpSvcMediaSessionHandler *iface,
                         guint errno,
                         const gchar *message,
                         DBusGMethodInvocation *context)
{
  SIPMediaSession *obj = SIP_MEDIA_SESSION (iface);

  SESSION_DEBUG(obj, "Media.SessionHandler::Error called (%s) terminating session", message);

  sip_media_session_terminate (obj);

  tp_svc_media_session_handler_return_from_error (context);
}

static void priv_emit_new_stream (SIPMediaSession *self,
				  SIPMediaStream *stream)
{
  gchar *object_path;
  guint id;
  guint media_type;
  guint direction;

  g_object_get (stream,
                "object-path", &object_path,
                "id", &id,
                "media-type", &media_type,
                "direction", &direction,
                NULL);

  /* note: all of the streams are bidirectional from farsight's point of view, it's
   * just in the signalling they change */

  tp_svc_media_session_handler_emit_new_stream_handler (
      (TpSvcMediaSessionHandler *)self, object_path, id, media_type,
      direction);

  g_free (object_path);
}


/**
 * sip_media_session_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
sip_media_session_ready (TpSvcMediaSessionHandler *iface,
                         DBusGMethodInvocation *context)
{
  SIPMediaSession *obj = SIP_MEDIA_SESSION (iface);
  SIPMediaSessionPrivate *priv;
  guint i;

  DEBUG ("enter");

  g_assert (SIP_IS_MEDIA_SESSION (obj));

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (obj);

  priv->se_ready = TRUE;

  /* note: streams are generated in priv_create_media_stream() */

  for (i = 0; i < priv->streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index (priv->streams, i);
    if (stream)
      priv_emit_new_stream (obj, stream);
  }
  
  DEBUG ("exit");

  tp_svc_media_session_handler_return_from_ready (context);
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

TpHandle
sip_media_session_get_peer (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  return priv->peer;
}

static void priv_session_state_changed (SIPMediaSession *session,
					SIPMediaSessionState prev_state,
					SIPMediaSessionState new_state)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  SESSION_DEBUG(session, "state changed from %s to %s",
                session_states[prev_state],
                session_states[new_state]);

  if (new_state == SIP_MEDIA_SESSION_STATE_PENDING_INITIATED)
    {
      priv->timer_id =
        g_timeout_add (DEFAULT_SESSION_TIMEOUT, priv_timeout_session, session);
    }
  else if (new_state == SIP_MEDIA_SESSION_STATE_ACTIVE)
    {
      if (priv->timer_id) {
	g_source_remove (priv->timer_id);
	priv->timer_id = 0;
      }
    }
}

#ifdef ENABLE_DEBUG
void
sip_media_session_debug (SIPMediaSession *session,
			 const gchar *format, ...)
{
  va_list list;
  gchar buf[512];
  SIPMediaSessionPrivate *priv;

  g_assert (SIP_IS_MEDIA_SESSION (session));

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  va_start (list, format);

  g_vsnprintf (buf, sizeof (buf), format, list);

  va_end (list);

  sip_debug (DEBUG_FLAG, "SIP media session [%-17s]: %s",
      session_states[priv->state],
      buf);
}
#endif /* ENABLE_DEBUG */

static gboolean priv_timeout_session (gpointer data)
{
  SIPMediaSession *session = data;
  TpIntSet *set;
  TpHandle peer;

  DEBUG("session timed out");
  if (session)
    {
      SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session); 

      peer = sip_media_session_get_peer (session);

      set = tp_intset_new ();
      tp_intset_add (set, peer);
      tp_group_mixin_change_members ((GObject *)priv->channel, "Timed out",
                                     NULL, set, NULL, NULL, 0,
                                     TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER);
      tp_intset_destroy (set);

      sip_media_session_terminate (session);
    }

  return FALSE;
}

void sip_media_session_terminate (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  
  DEBUG ("enter");

  if (priv->state == SIP_MEDIA_SESSION_STATE_ENDED)
    return;

  if (priv->state == SIP_MEDIA_SESSION_STATE_PENDING_INITIATED ||
      priv->state == SIP_MEDIA_SESSION_STATE_ACTIVE) {
    nua_handle_t *nh = priv_get_nua_handle_for_session (session);
    if (nh != NULL)
      {
        DEBUG("sending SIP BYE (handle %p)", nh);
        nua_bye (nh, TAG_END());
      }
  }

  g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_ENDED, NULL);
}

gboolean
sip_media_session_set_remote_info (SIPMediaSession *session,
                                   const sdp_session_t* sdp)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  const sdp_media_t *media;
  guint supported_media_cnt = 0;
  guint i;
  gboolean res = TRUE;

  DEBUG ("enter");

  if (sdp_session_cmp (priv->remote_sdp, sdp) == 0)
    {
      SESSION_DEBUG(session, "no session changes detected");
      return TRUE;
    }

  /* Store the session description structure */
  priv->remote_sdp = sdp_session_dup (priv->home, sdp);
  g_return_val_if_fail (priv->remote_sdp != NULL, FALSE);

  media = priv->remote_sdp->sdp_media;

  /* note: for each session, we maintain an ordered list of 
   *       streams (SDP m-lines) which are matched 1:1 to 
   *       the streams of the remote SDP */

  for (i = 0; media; i++)
    {
      SIPMediaStream *stream = NULL;
      guint media_type;

      media_type = sip_tp_media_type (media->m_type);

      if (i >= priv->streams->len)
	stream = priv_create_media_stream (session, media_type);
      else 
	stream = g_ptr_array_index(priv->streams, i);

      /* note: it is ok for the stream to be NULL (unsupported media type) */
      if (stream == NULL)
        goto next_media;

      DEBUG("setting remote SDP for stream (%u:%p).", i, stream);    

      if (media->m_rejected)
        {
          DEBUG("the stream has been rejected, closing");
        }
      else if (sip_media_stream_get_media_type (stream) != media_type)
        {
          /* XXX: close this stream and create a new one in its place? */
          g_warning ("The peer has changed the media type, don't know what to do");
        }
      else if (sip_media_stream_set_remote_info (stream, media))
        {
          ++supported_media_cnt;
          goto next_media;
        }

      /* There have been problems with the stream update, kill the stream */
      /* XXX: fast and furious, not tested */
      sip_media_stream_close (stream);
      g_object_unref (stream);
      g_ptr_array_index(priv->streams, i) = NULL;

    next_media:
      media = media->m_next;
    }

  if (supported_media_cnt == 0)
    {
      g_warning ("No supported media in the session, aborting.");
      res = FALSE;
    }

  g_assert(media == NULL);
  if (i != priv->streams->len)
    {
      g_warning ("There were %u parsed SDP m-lines but we have %u stream entries - "
                 "is someone failing to comply with RFCs?", i, priv->streams->len);
    }

  /* XXX: hmm, this is not the correct place really */
  g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_ACTIVE, NULL);

  DEBUG ("exit");

  return res;
}

void sip_media_session_stream_state (SIPMediaSession *sess,
                                     guint stream_id,
                                     guint state)
{
  SIPMediaSessionPrivate *priv;
  priv = SIP_MEDIA_SESSION_GET_PRIVATE (sess);
  g_assert (priv);
  sip_media_channel_stream_state (priv->channel, stream_id, state);
}

DEFINE_TP_STRUCT_TYPE(sip_tp_stream_struct_type,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_UINT,
                      G_TYPE_UINT)

DEFINE_TP_LIST_TYPE(sip_tp_stream_list_type,
                    sip_tp_stream_struct_type ())


void
priv_add_stream_list_entry (GPtrArray *list,
                            SIPMediaStream *stream,
                            SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  GValue entry = { 0 };
  GType stream_type;
  guint id;
  TpMediaStreamType type = TP_MEDIA_STREAM_TYPE_AUDIO;
  TpMediaStreamState connection_state = TP_MEDIA_STREAM_STATE_CONNECTED;
  TpMediaStreamDirection direction = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
  guint pending_send_flags = 0;

  g_assert(stream != NULL);

  g_object_get (stream,
                "id", &id,
                "media-type", &type,
                /* XXX: add to sip-stream -> "connection-state", &connection_state, */
                "direction", &direction,
                "pending-send-flags", &pending_send_flags,
                NULL);

  stream_type = sip_tp_stream_struct_type ();

  g_value_init (&entry, stream_type);
  g_value_take_boxed (&entry,
                      dbus_g_type_specialized_construct (stream_type));

  dbus_g_type_struct_set (&entry,
                          0, id,
                          1, priv->peer,
                          2, type,
                          3, connection_state,
                          4, direction,
                          5, pending_send_flags,
                          G_MAXUINT);

  g_ptr_array_add (list, g_value_get_boxed (&entry));
}

gboolean sip_media_session_request_streams (SIPMediaSession *session,
					    const GArray *media_types,
					    GPtrArray **ret,
					    GError **error)
{
  guint i;

  DEBUG ("enter");

  *ret = g_ptr_array_sized_new (media_types->len);

  for (i = 0; i < media_types->len; i++) {
    guint media_type = g_array_index (media_types, guint, i);
    SIPMediaStream *stream;

    stream = priv_create_media_stream (session, media_type);

    priv_add_stream_list_entry (*ret, stream, session);
  }

  return TRUE;
}

/**
 * Returns a list of pointers to SIPMediaStream objects 
 * associated with this session.
 */
gboolean sip_media_session_list_streams (SIPMediaSession *session,
					 GPtrArray **ret)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  SIPMediaStream *stream;
  guint i;

  if (priv->streams == NULL || priv->streams->len == 0)
    return FALSE;

  *ret = g_ptr_array_sized_new (priv->streams->len);

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream)
	priv_add_stream_list_entry (*ret, stream, session);
    }

  return TRUE;
}

void sip_media_session_accept (SIPMediaSession *self, gboolean accept)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  gboolean p = priv->accepted;

  g_debug ("%s: accepting session: %d", G_STRFUNC, accept);

  priv->accepted = accept;

  if (accept != p)
    priv_offer_answer_step (self);
}

static SIPMediaStream *
sip_media_session_get_stream (SIPMediaSession *self,
                              guint stream_id,
                              GError **error)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  SIPMediaStream *stream;

  g_assert (priv->streams != NULL);

  if (stream_id >= priv->streams->len)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "stream ID %u is invalid", stream_id);
      return NULL;
    }

  stream = g_ptr_array_index (priv->streams, stream_id);

  if (stream == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "stream %u does not exist", stream_id);
      return NULL;
    }

  return stream;
}

gboolean
sip_media_session_start_telephony_event (SIPMediaSession *self,
                                         guint stream_id,
                                         guchar event,
                                         GError **error)
{
  SIPMediaStream *stream;

  stream = sip_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    return FALSE;

  if (sip_media_stream_get_media_type (stream) != TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                   "non-audio stream %u does not support telephony events", stream_id);
      return FALSE;
    }

  DEBUG("starting telephony event %u on stream %u", (guint) event, stream_id);

  sip_media_stream_start_telephony_event (stream, event);

  return TRUE;
}

gboolean
sip_media_session_stop_telephony_event  (SIPMediaSession *self,
                                         guint stream_id,
                                         GError **error)
{
  SIPMediaStream *stream;

  stream = sip_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    return FALSE;

  if (sip_media_stream_get_media_type (stream) != TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                   "non-audio stream %u does not support telephony events; spurious use of the stop event?", stream_id);
      return FALSE;
    }

  DEBUG("stopping the telephony event on stream %u", stream_id);

  sip_media_stream_stop_telephony_event (stream);

  return TRUE;
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

static nua_handle_t *priv_get_nua_handle_for_session (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  nua_handle_t *tmp = NULL;

  if (priv->channel) 
    g_object_get (priv->channel, "nua-handle", &tmp, NULL);

  return tmp;
}

static void priv_stream_new_active_candidate_pair_cb (SIPMediaStream *stream,
						      const gchar *native_candidate_id,
						      const gchar *remote_candidate_id,
						      SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;

  g_assert (SIP_IS_MEDIA_SESSION (session));

  DEBUG ("enter");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  /* g_assert (priv->state < SIP_MEDIA_SESSION_STATE_ACTIVE); */

  SESSION_DEBUG(session, "stream-engine reported a new active candidate pair [\"%s\" - \"%s\"]",
                native_candidate_id, remote_candidate_id);

  /* XXX: active candidate pair, requires signaling action, 
   *      but currently done in priv_stream_ready_cb() */
}

static void priv_session_media_state (SIPMediaSession *session, gboolean playing)
{
  guint i;
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  for (i = 0; i < priv->streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index(priv->streams, i);
    if (stream)
      sip_media_stream_set_playing (stream, playing);
  }
}

/**
 * Sends an outbound offer/answer if all streams of the session
 * are prepared.
 * 
 * Following inputs are considered in decision making:
 *  - status of local streams (set up with stream-engine)
 *  - whether session is locally accepted
 *  - whether we are the initiator or not
 *  - whether an offer/answer step is pending (either initial,
 *    or a requested update to the session state)  
 */
static void priv_offer_answer_step (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;
  gint non_ready_streams = 0;

  DEBUG ("enter");

  /* step: check status of streams */
  for (i = 0; i < priv->streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index(priv->streams, i);
    if (stream &&
	sip_media_stream_is_ready (stream) != TRUE)
      ++non_ready_streams;
  }

  /* step: if all stream are ready, send an offer/answer */
  if (non_ready_streams == 0 &&
      priv->oa_pending) {
    GString *user_sdp = g_string_sized_new (0);

    for (i = 0; i < priv->streams->len; i++) {
      SIPMediaStream *stream = g_ptr_array_index(priv->streams, i);
      if (stream)
	user_sdp = g_string_append (user_sdp, sip_media_stream_local_sdp(stream));
      else 
	user_sdp = g_string_append (user_sdp, "m=unknown 0 -/-");
    }

    /* send an offer if the session was initiated by us */
    if (priv->initiator != priv->peer) {
      SIPConnection *conn = NULL;
      nua_handle_t *nh;

      g_object_get (priv->channel,
                    "connection", &conn,
                    NULL);

      nh = sip_conn_create_request_handle (conn, priv->peer);
      if (nh != NULL) {

	g_object_set (priv->channel, "nua-handle", nh, NULL);

	/* note:  we need to be prepared to receive media right after the
	 *       offer is sent, so we must set state to playing */
	priv_session_media_state (session, TRUE);
	
	nua_invite (nh,
		    SOATAG_USER_SDP_STR(user_sdp->str),
		    SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
		    SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
		    TAG_END());

        /* The reference is now kept by the channel */
        nua_handle_unref (nh);

	priv->oa_pending = FALSE;
      }
      else 
	g_warning ("Unable to create the request handle, probably due to invalid destination URI");
    }
    else {
      /* note: only send a reply if session is locally accepted */
      if (priv->accepted == TRUE) {
	nua_handle_t *handle = priv_get_nua_handle_for_session(session);
	g_debug("Answering with SDP: <<<%s>>>.", user_sdp->str);
	if (handle) {
	  nua_respond (handle, 200, sip_200_OK,
		       SOATAG_USER_SDP_STR (user_sdp->str),
		       SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
		       SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
		       TAG_END());
	  
	  priv->oa_pending = FALSE;
	  
	  /* note: we have accepted the call, set state to playing */ 
	  priv_session_media_state (session, TRUE);
	}
	else
	  g_warning ("Unable to answer to the incoming INVITE, channel handle not available.");
      }
    }
  }
}

static void priv_stream_ready_cb (SIPMediaStream *stream,
				  const GPtrArray *codecs,
				  SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;

  g_assert (SIP_IS_MEDIA_SESSION (session));
  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
 
  DEBUG ("enter");

  if (priv->state < SIP_MEDIA_SESSION_STATE_PENDING_INITIATED)
    g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_PENDING_INITIATED, NULL);

  priv_offer_answer_step (session);
}

static void priv_stream_supported_codecs_cb (SIPMediaStream *stream,
					     const GPtrArray *codecs,
					     SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;

  g_assert (SIP_IS_MEDIA_SESSION (session));

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->initiator != priv->peer)
    {
      SESSION_DEBUG(session,
                    "session not initiated by peer so we're "
                    "not preparing an accept message");
      return;
    }
}

static void
priv_stream_direction_changed_cb (SIPMediaStream *stream,
                                  guint direction,
                                  guint pending_send_flags,
                                  SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;
  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
        priv->channel,
        sip_media_stream_get_id (stream), direction, pending_send_flags);
}

static SIPMediaStream* priv_create_media_stream (SIPMediaSession *self, guint media_type)
{
  SIPMediaSessionPrivate *priv;
  gchar *object_path;
  SIPMediaStream *stream = NULL;

  g_assert (SIP_IS_MEDIA_SESSION (self));

  DEBUG ("enter");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO ||
      media_type == TP_MEDIA_STREAM_TYPE_VIDEO) {

    object_path = g_strdup_printf ("%s/MediaStream%d", priv->object_path, priv->streams->len);
    
    stream = g_object_new (SIP_TYPE_MEDIA_STREAM,
			   "media-session", self,
			   "media-type", media_type,
			   "object-path", object_path,
			   "id", priv->streams->len,
			   NULL);

    g_free (object_path);
 
    g_signal_connect (stream, "new-active-candidate-pair",
		      (GCallback) priv_stream_new_active_candidate_pair_cb,
		      self);
    g_signal_connect (stream, "ready",
		      (GCallback) priv_stream_ready_cb,
		      self);
    g_signal_connect (stream, "supported-codecs",
		      (GCallback) priv_stream_supported_codecs_cb,
		      self);
    g_signal_connect (stream, "direction-changed",
                      (GCallback) priv_stream_direction_changed_cb,
                      self);

    if (priv->se_ready == TRUE) {
      priv_emit_new_stream (self, stream);
    }

    g_signal_emit (self, signals[SIG_STREAM_ADDED], 0, stream);
  }

  /* note: we add an entry even for unsupported media types */
  g_ptr_array_add (priv->streams, stream);

  DEBUG ("exit");

  return stream;
}

static void
session_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaSessionHandlerClass *klass = (TpSvcMediaSessionHandlerClass *)g_iface;

#define IMPLEMENT(x) tp_svc_media_session_handler_implement_##x (\
    klass, (tp_svc_media_session_handler_##x##_impl) sip_media_session_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}
