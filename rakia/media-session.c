/*
 * sip-media-session.c - Source for RakiaMediaSession
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-media-session).
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

#include "rakia/media-session.h"

#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <sofia-sip/sip_status.h>

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "config.h"

#include <rakia/base-connection.h>

#include "rakia/media-channel.h"
#include "rakia/media-stream.h"
#include "rakia/sip-session.h"

#include "signals-marshal.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"

/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define RAKIA_REINVITE_TIMEOUT 180

static void session_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(RakiaMediaSession,
    rakia_media_session,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init)
    )

/* signal enum */
enum
{
  SIG_DTMF_READY,
  SIG_LAST_SIGNAL
};

/* properties */
enum
{
  PROP_MEDIA_CHANNEL = 1,
  PROP_DBUS_DAEMON,
  PROP_OBJECT_PATH,
  PROP_SIP_SESSION,
  PROP_PEER,
  PROP_HOLD_STATE,
  PROP_HOLD_STATE_REASON,
  PROP_LOCAL_IP_ADDRESS,
  PROP_STUN_SERVERS,
  LAST_PROPERTY
};

static guint signals[SIG_LAST_SIGNAL] = {0};

#ifdef ENABLE_DEBUG

#define SESSION_DEBUG(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "session: " format, \
      ##__VA_ARGS__)

#define SESSION_MESSAGE(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "session: " format, \
      ##__VA_ARGS__)

#else /* !ENABLE_DEBUG */

#define SESSION_DEBUG(session, format, ...) G_STMT_START { } G_STMT_END
#define SESSION_MESSAGE(session, format, ...) G_STMT_START { } G_STMT_END

#endif /* ENABLE_DEBUG */

/* private structure */
struct _RakiaMediaSessionPrivate
{
  TpDBusDaemon *dbus_daemon;
  RakiaMediaChannel *channel;             /* see gobj. prop. 'media-channel' */
  gchar *object_path;                     /* see gobj. prop. 'object-path' */
  RakiaSipSession *sipsession;
  TpHandle peer;                          /* see gobj. prop. 'peer' */
  gchar *local_ip_address;                /* see gobj. prop. 'local-ip-address' */
  TpLocalHoldState hold_state;         /* local hold state aggregated from stream directions */
  TpLocalHoldStateReason hold_reason;  /* last used hold state change reason */

  gint local_non_ready;                   /* number of streams with local information update pending */
  guint remote_stream_count;              /* number of streams last seen in a remote offer */
  GPtrArray *streams;
  gboolean remote_initiated;              /*< session is remotely intiated */

  gboolean se_ready;                      /*< connection established with stream-engine */
  gboolean audio_connected;               /*< an audio stream has reached connected state */
  gboolean dispose_has_run;
};

#define RAKIA_MEDIA_SESSION_GET_PRIVATE(session) ((session)->priv)

static void rakia_media_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec);
static void rakia_media_session_set_property (GObject      *object,
					    guint         property_id,
					    const GValue *value,
					    GParamSpec   *pspec);

static RakiaMediaStream *
rakia_media_session_get_stream (RakiaMediaSession *self,
                              guint stream_id,
                              GError **error);

static void rakia_media_session_init (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_MEDIA_SESSION, RakiaMediaSessionPrivate);

  self->priv = priv;

  priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
  priv->hold_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;

  /* allocate any data required by the object here */
  priv->streams = g_ptr_array_new ();
}

static GObject *
rakia_media_session_constructor (GType type, guint n_props,
			       GObjectConstructParam *props)
{
  GObject *obj;
  RakiaMediaSessionPrivate *priv;

  obj = G_OBJECT_CLASS (rakia_media_session_parent_class)->
           constructor (type, n_props, props);
  priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (RAKIA_MEDIA_SESSION (obj));

  g_assert (TP_IS_DBUS_DAEMON (priv->dbus_daemon));
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path, obj);

  return obj;
}

static void rakia_media_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
  RakiaMediaSession *session = RAKIA_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id)
    {
    case PROP_DBUS_DAEMON:
      g_value_set_object (value, priv->dbus_daemon);
      break;
    case PROP_MEDIA_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_SIP_SESSION:
      g_value_set_object (value, priv->sipsession);
      break;
    case PROP_PEER:
      g_value_set_uint (value, priv->peer);
      break;
    case PROP_HOLD_STATE:
      g_value_set_uint (value, priv->hold_state);
      break;
    case PROP_HOLD_STATE_REASON:
      g_value_set_uint (value, priv->hold_reason);
      break;
    case PROP_LOCAL_IP_ADDRESS:
      g_value_set_string (value, priv->local_ip_address);
      break;

    case PROP_STUN_SERVERS:
      {
        /* TODO: should be able to get all entries from the DNS lookup(s).
         * At the moment, rawudp ignores all servers except the first one. */
        GPtrArray *servers;
        gchar *stun_server = NULL;
        guint stun_port = RAKIA_DEFAULT_STUN_PORT;

        g_return_if_fail (priv->channel != NULL);

        g_object_get (priv->channel,
            "stun-server", &stun_server,
            "stun-port", &stun_port,
            NULL);

        servers = g_ptr_array_new ();

        if (stun_server != NULL)
          {
            GValue addr = { 0 };
            const GType addr_type = TP_STRUCT_TYPE_SOCKET_ADDRESS_IP;

            g_value_init (&addr, addr_type);
            g_value_take_boxed (&addr,
                dbus_g_type_specialized_construct (addr_type));

            dbus_g_type_struct_set (&addr,
                0, stun_server,
                1, (guint16) stun_port,
                G_MAXUINT);

            g_ptr_array_add (servers, g_value_get_boxed (&addr));

            g_free (stun_server);
          }

        g_value_take_boxed (value, servers);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void rakia_media_session_set_property (GObject      *object,
					    guint         property_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
  RakiaMediaSession *session = RAKIA_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id)
    {
    case PROP_DBUS_DAEMON:
      g_assert (priv->dbus_daemon == NULL);       /* construct-only */
      priv->dbus_daemon = g_value_dup_object (value);
      break;
    case PROP_MEDIA_CHANNEL:
      priv->channel = RAKIA_MEDIA_CHANNEL (g_value_get_object (value));
      break;
    case PROP_OBJECT_PATH:
      g_assert (priv->object_path == NULL);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_SIP_SESSION:
      priv->sipsession = g_value_dup_object (value);
      break;
    case PROP_PEER:
      priv->peer = g_value_get_uint (value);
      break;
    case PROP_LOCAL_IP_ADDRESS:
      g_assert (priv->local_ip_address == NULL);
      priv->local_ip_address = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void rakia_media_session_dispose (GObject *object);
static void rakia_media_session_finalize (GObject *object);

static void
rakia_media_session_class_init (RakiaMediaSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (RakiaMediaSessionPrivate));

  object_class->constructor = rakia_media_session_constructor;

  object_class->get_property = rakia_media_session_get_property;
  object_class->set_property = rakia_media_session_set_property;

  object_class->dispose = rakia_media_session_dispose;
  object_class->finalize = rakia_media_session_finalize;

  param_spec = g_param_spec_object ("dbus-daemon", "TpDBusDaemon",
      "Connection to D-Bus.", TP_TYPE_DBUS_DAEMON,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_DBUS_DAEMON, param_spec);

  param_spec = g_param_spec_object ("media-channel", "RakiaMediaChannel object",
      "SIP media channel object that owns this media session object"
      " (not reference counted).",
      RAKIA_TYPE_MEDIA_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_object ("sip-session", "RakiaSipSession object",
      "SIP session object that is used for this SIP media channel object.",
      RAKIA_TYPE_SIP_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SIP_SESSION, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
      "The TpHandle representing the contact with whom this session communicates.",
      0, G_MAXUINT32,
      0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_uint ("hold-state", "Local hold state",
      "The current Local_Hold_State value as reported by the Hold interface",
      TP_LOCAL_HOLD_STATE_UNHELD, TP_LOCAL_HOLD_STATE_PENDING_UNHOLD,
      TP_LOCAL_HOLD_STATE_UNHELD,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HOLD_STATE, param_spec);

  param_spec = g_param_spec_uint ("hold-state-reason",
      "Local hold state change reason",
      "The last Local_Hold_State_Reason value as reported by the Hold interface",
      TP_LOCAL_HOLD_STATE_REASON_NONE,
      TP_LOCAL_HOLD_STATE_REASON_RESOURCE_NOT_AVAILABLE,
      TP_LOCAL_HOLD_STATE_REASON_NONE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HOLD_STATE_REASON, param_spec);

  param_spec = g_param_spec_string ("local-ip-address", "Local IP address",
      "The local IP address preferred for media streams",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_IP_ADDRESS, param_spec);

  param_spec = g_param_spec_boxed ("stun-servers", "STUN servers",
      "Array of IP address-port pairs for available STUN servers",
      TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS, param_spec);

  signals[SIG_DTMF_READY] =
      g_signal_new ("dtmf-ready",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0, NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);
}

static void
rakia_media_session_dispose (GObject *object)
{
  RakiaMediaSession *self = RAKIA_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG("enter");

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->dbus_daemon);

  tp_clear_object (&priv->sipsession);

  if (G_OBJECT_CLASS (rakia_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_media_session_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
rakia_media_session_finalize (GObject *object)
{
  RakiaMediaSession *self = RAKIA_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  guint i;

  /* free any data held directly by the object here */

  for (i = 0; i < priv->streams->len; i++) {
    RakiaMediaStream *stream = g_ptr_array_index (priv->streams, i);
    if (stream != NULL)
      {
        WARNING ("stream %u (%p) left over, reaping", i, stream);
        g_object_unref (stream);
      }
  }
  g_ptr_array_free(priv->streams, TRUE);

  g_free (priv->local_ip_address);
  g_free (priv->object_path);

  G_OBJECT_CLASS (rakia_media_session_parent_class)->finalize (object);

  DEBUG("exit");
}



/**
 * rakia_media_session_error
 *
 * Implements DBus method Error
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
rakia_media_session_error (TpSvcMediaSessionHandler *iface,
                         guint errno,
                         const gchar *message,
                         DBusGMethodInvocation *context)
{
  RakiaMediaSession *self = RAKIA_MEDIA_SESSION (iface);
  RakiaMediaSessionPrivate *priv = self->priv;

  SESSION_DEBUG (obj, "Media.SessionHandler::Error called (%s), terminating session", message);

  rakia_sip_session_terminate (priv->sipsession);

  tp_svc_media_session_handler_return_from_error (context);
}

static void priv_emit_new_stream (RakiaMediaSession *self,
				  RakiaMediaStream *stream)
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
 * rakia_media_session_ready
 *
 * Implements DBus method Ready
 * on interface org.freedesktop.Telepathy.Media.SessionHandler
 */
static void
rakia_media_session_ready (TpSvcMediaSessionHandler *iface,
                           DBusGMethodInvocation *context)
{
  RakiaMediaSession *self = RAKIA_MEDIA_SESSION (iface);
  RakiaMediaSessionPrivate *priv = self->priv;
  guint i;

  SESSION_DEBUG (self, "Media.SessionHandler.Ready called");

  if (!priv->se_ready)
    {
      priv->se_ready = TRUE;

      for (i = 0; i < priv->streams->len; i++)
        {
          RakiaMediaStream *stream = g_ptr_array_index (priv->streams, i);
          if (stream)
            priv_emit_new_stream (self, stream);
        }
    }

  tp_svc_media_session_handler_return_from_ready (context);
}

/***********************************************************************
 * Helper functions follow (not based on generated templates)
 ***********************************************************************/

TpHandle
rakia_media_session_get_peer (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
  return priv->peer;
}

static gboolean
rakia_media_session_supports_media_type (guint media_type)
{
  switch (media_type)
    {
    case TP_MEDIA_STREAM_TYPE_AUDIO:
    case TP_MEDIA_STREAM_TYPE_VIDEO:
      return TRUE;
    }
  return FALSE;
}

static void
priv_apply_streams_pending_direction (RakiaMediaSession *session,
                                      guint pending_send_mask)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  guint i;

  /* If there has been a local change pending a re-INVITE,
   * suspend remote approval until the next transaction */
  if (rakia_sip_session_pending_offer (priv->sipsession))
    pending_send_mask &= ~(guint)TP_MEDIA_STREAM_PENDING_REMOTE_SEND;

  /* Apply the pending direction changes */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        rakia_media_stream_apply_pending_direction (stream, pending_send_mask);
    }
}


void
priv_add_stream_list_entry (GPtrArray *list,
                            RakiaMediaStream *stream,
                            RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
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
                "state", &connection_state,
                "direction", &direction,
                "pending-send-flags", &pending_send_flags,
                NULL);

  stream_type = TP_STRUCT_TYPE_MEDIA_STREAM_INFO;

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

gboolean rakia_media_session_request_streams (RakiaMediaSession *session,
					    const GArray *media_types,
					    GPtrArray *ret,
					    GError **error)
{
  guint i;

  DEBUG ("enter");

  /* Validate the media types before creating any streams */
  for (i = 0; i < media_types->len; i++) {
    guint media_type = g_array_index (media_types, guint, i);
    if (!rakia_media_session_supports_media_type (media_type))
      {
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                     "media type #%u is not supported", i);
        return FALSE;
      }
  }

  for (i = 0; i < media_types->len; i++) {
    guint media_type = g_array_index (media_types, guint, i);
    RakiaMediaStream *stream;

    stream = rakia_media_session_add_stream (session,
        media_type,
        TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        TRUE);

    if (stream == NULL)
      {
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                     "creation of stream %u failed", i);
        /* XXX: should we close the streams already created as part of
         * this request, despite having emitted signals about them? */
        return FALSE;
      }

    priv_add_stream_list_entry (ret, stream, session);
  }

  return TRUE;
}

gboolean
rakia_media_session_remove_streams (RakiaMediaSession *self,
                                  const GArray *stream_ids,
                                  GError **error)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  RakiaSipMedia *media;
  guint stream_id;
  guint i;

  DEBUG ("enter");

  for (i = 0; i < stream_ids->len; i++)
    {
      stream_id = g_array_index (stream_ids, guint, i);
      stream = rakia_media_session_get_stream (self, stream_id, error);
      if (stream == NULL)
        return FALSE;
      media = rakia_media_stream_get_media (stream);
      rakia_media_stream_close (stream);
      rakia_sip_session_remove_media (priv->sipsession, media);
    }

  rakia_sip_session_media_changed (priv->sipsession);

  return TRUE;
}

void rakia_media_session_list_streams (RakiaMediaSession *session,
                                     GPtrArray *ret)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream)
        priv_add_stream_list_entry (ret, stream, session);
    }
}


static gboolean
rakia_media_session_is_local_hold_ongoing (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  return (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD
          || priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_HOLD);
}

gboolean
rakia_media_session_request_stream_direction (RakiaMediaSession *self,
                                              guint stream_id,
                                              guint direction,
                                              GError **error)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  RakiaSipSessionState state;

  stream = rakia_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "stream %u does not exist", stream_id);
      return FALSE;
    }

  state = rakia_sip_session_get_state (priv->sipsession);

  SESSION_DEBUG (self, "direction %u requested for stream %u",
      direction, stream_id);

  if (state == RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED
      || state == RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED)
    {
      /* While processing a session offer, we can only mask out direction
       * requested by the remote peer */
      direction &= rakia_sip_media_get_requested_direction (
          rakia_media_stream_get_media (stream));
    }

  if (rakia_media_session_is_local_hold_ongoing (self))
    direction = RAKIA_DIRECTION_NONE;

  rakia_sip_media_set_requested_direction (
      rakia_media_stream_get_media (stream), direction);

  return TRUE;
}


void
rakia_media_session_accept (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);

  if (rakia_sip_session_is_accepted (priv->sipsession))
    return;

  SESSION_DEBUG (self, "accepting the session");

  rakia_sip_session_accept (priv->sipsession);

  /* Apply the pending send flags */
  priv_apply_streams_pending_direction (self,
      TP_MEDIA_STREAM_PENDING_LOCAL_SEND |
      TP_MEDIA_STREAM_PENDING_REMOTE_SEND);

  /* Can play the DTMF dialstring if an audio stream is connected */
  if (priv->audio_connected)
    g_signal_emit (self, signals[SIG_DTMF_READY], 0);
}


static RakiaMediaStream *
rakia_media_session_get_stream (RakiaMediaSession *self,
                                guint stream_id,
                                GError **error)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;

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

TpLocalHoldState
rakia_media_session_get_hold_state (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  return priv->hold_state;
}

static void
priv_initiate_hold (RakiaMediaSession *self,
                    gboolean hold,
                    TpLocalHoldStateReason reason)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  gboolean stream_hold_requested = FALSE;
  RakiaMediaStream *stream;
  guint i;

  DEBUG("enter");

  if (hold)
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD
          || priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_HOLD)
        {
          MESSAGE ("redundant hold request");
          return;
        }
    }
  else
    {
      if (priv->hold_state == TP_LOCAL_HOLD_STATE_UNHELD
          || priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_UNHOLD)
        {
          MESSAGE ("redundant unhold request");
          return;
        }
    }

  /* Emit the hold notification for every stream that needs it */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL
          && rakia_media_stream_request_hold_state (stream, hold))
        stream_hold_requested = TRUE;
    }

  if (stream_hold_requested)
    {
      priv->hold_state = hold? TP_LOCAL_HOLD_STATE_PENDING_HOLD
                             : TP_LOCAL_HOLD_STATE_PENDING_UNHOLD;
    }
  else
    {
      /* There were no streams to flip, short cut to the final state */
      priv->hold_state = hold? TP_LOCAL_HOLD_STATE_HELD
                             : TP_LOCAL_HOLD_STATE_UNHELD;
    }
  priv->hold_reason = reason;

  tp_svc_channel_interface_hold_emit_hold_state_changed (priv->channel,
      priv->hold_state, reason);
}

static void
priv_finalize_hold (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  TpLocalHoldState final_hold_state;
  guint i;
  gboolean held = FALSE;
  RakiaDirection dir;

  DEBUG("enter");

  switch (priv->hold_state)
    {
    case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
      held = TRUE;
      break;
    case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
      held = FALSE;
      break;
    default:
      /* Streams changed state without request, signal this to the client.
       * All streams should have the same hold state at this point,
       * so just query one of them for the current hold state */
      stream = NULL;
      for (i = 0; i < priv->streams->len; i++)
        {
          stream = g_ptr_array_index(priv->streams, i);
          if (stream != NULL)
            break;
        }
      g_return_if_fail (stream != NULL);

      g_object_get (stream, "hold-state", &held, NULL);
    }

  if (held)
    {
      final_hold_state = TP_LOCAL_HOLD_STATE_HELD;
      dir = RAKIA_DIRECTION_NONE;
    }
  else
    {
      final_hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
      dir = RAKIA_DIRECTION_BIDIRECTIONAL;
    }

  priv->hold_state = final_hold_state;
  tp_svc_channel_interface_hold_emit_hold_state_changed (priv->channel,
      final_hold_state, priv->hold_reason);

  /* Set stream directions accordingly to the achieved hold state */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        {
          rakia_sip_media_set_requested_direction (
              rakia_media_stream_get_media (stream), dir);
        }
    }
}

void
rakia_media_session_request_hold (RakiaMediaSession *self,
                                  gboolean hold)
{
  priv_initiate_hold (self,
                      hold,
                      TP_LOCAL_HOLD_STATE_REASON_REQUESTED);
}

gboolean
rakia_media_session_has_media (RakiaMediaSession *self,
                               TpMediaStreamType type)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream == NULL)
        continue;
      if (rakia_media_stream_get_media_type (stream) == type)
        return TRUE;
    }

  return FALSE;
}

void
rakia_media_session_start_telephony_event (RakiaMediaSession *self,
                                           guchar event)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream == NULL)
        continue;
      if (rakia_media_stream_get_media_type (stream)
          != TP_MEDIA_STREAM_TYPE_AUDIO)
        continue;

      SESSION_DEBUG (self, "starting telephony event %u on stream %u",
          (guint) event, i);

      rakia_media_stream_start_telephony_event (stream, event);
    }
}

void
rakia_media_session_stop_telephony_event  (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream == NULL)
        continue;
      if (rakia_media_stream_get_media_type (stream)
          != TP_MEDIA_STREAM_TYPE_AUDIO)
        continue;

      SESSION_DEBUG (self, "stopping the telephony event on stream %u", i);

      rakia_media_stream_stop_telephony_event (stream);
    }
}

gint
rakia_media_session_rate_native_transport (RakiaMediaSession *session,
                                         const GValue *transport)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
  gint result = 0;
  gchar *address = NULL;
  guint proto = TP_MEDIA_STREAM_BASE_PROTO_UDP;

  dbus_g_type_struct_get (transport,
                          1, &address,
                          3, &proto,
                          G_MAXUINT);

  g_assert (address != NULL);

  if (proto != TP_MEDIA_STREAM_BASE_PROTO_UDP)
    result = -1;
  /* XXX: this will not work properly when IPv6 support comes */
  else if (priv->local_ip_address != NULL
      && strcmp (address, priv->local_ip_address) == 0)
    result = 1;

  g_free (address);

  return result;
}

static void
priv_stream_close_cb (RakiaMediaStream *stream,
                      RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv;
  RakiaSipMedia *media;
  guint id;

  DEBUG("enter");

  priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);

  id = rakia_media_stream_get_id (stream);
  g_return_if_fail (g_ptr_array_index(priv->streams, id) == stream);

  media = rakia_media_stream_get_media (stream);
  rakia_sip_session_remove_media (priv->sipsession, media);

  g_object_unref (stream);

  g_ptr_array_index(priv->streams, id) = NULL;

  tp_svc_channel_type_streamed_media_emit_stream_removed (priv->channel, id);
}

static void
priv_stream_state_changed_cb (RakiaMediaStream *stream,
                              guint state,
                              RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = session->priv;

  tp_svc_channel_type_streamed_media_emit_stream_state_changed(
        priv->channel,
        rakia_media_stream_get_id (stream), state);

  /* Check if DTMF can now be played */
  if (!priv->audio_connected
      && state == TP_MEDIA_STREAM_STATE_CONNECTED
      && rakia_media_stream_get_media_type (stream)
         == TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      priv->audio_connected = TRUE;

      if (rakia_sip_session_is_accepted (priv->sipsession))
        g_signal_emit (session, signals[SIG_DTMF_READY], 0);
    }
}

static void
priv_stream_direction_changed_cb (RakiaMediaStream *stream,
                                  guint direction,
                                  guint pending_send_flags,
                                  RakiaMediaChannel *channel)
{
  g_assert (RAKIA_IS_MEDIA_CHANNEL (channel));
  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
        channel,
        rakia_media_stream_get_id (stream), direction, pending_send_flags);
}

static void
priv_stream_hold_state_cb (RakiaMediaStream *stream,
                           GParamSpec *pspec,
                           RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (session);
  gboolean hold;
  guint i;

  /* Determine the hold state all streams shall come to */
  switch (priv->hold_state)
    {
    case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
      hold = TRUE;
      break;
    case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
      hold = FALSE;
      break;
    default:
      SESSION_MESSAGE (session, "unexpected hold state change from a stream");

      /* Try to follow the changes and report the resulting hold state */
      g_object_get (stream, "hold-state", &hold, NULL);
      priv->hold_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
    }

  /* Check if all streams have reached the desired hold state */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index (priv->streams, i);
      if (stream != NULL)
        {
          gboolean stream_held = FALSE;
          g_object_get (stream, "hold-state", &stream_held, NULL);
          if ((!stream_held) != (!hold))
            {
              SESSION_DEBUG (session, "hold/unhold not complete yet");
              return;
            }
        }
    }

  priv_finalize_hold (session);
}

static void
priv_stream_unhold_failure_cb (RakiaMediaStream *stream,
                               RakiaMediaSession *session)
{
  priv_initiate_hold (session,
                      TRUE,
                      TP_LOCAL_HOLD_STATE_REASON_RESOURCE_NOT_AVAILABLE);
}

RakiaMediaStream*
rakia_media_session_add_stream (RakiaMediaSession *self,
                                guint media_type,
                                TpMediaStreamDirection direction,
                                gboolean created_locally)
{
  RakiaMediaSessionPrivate *priv = RAKIA_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream = NULL;

  DEBUG ("enter");

  if (rakia_media_session_supports_media_type (media_type)) {
    guint stream_id;
    gchar *object_path;
    guint pending_send_flags;
    RakiaSipMedia *media;

    stream_id = priv->streams->len;
    object_path = g_strdup_printf ("%s/MediaStream%u",
                                   priv->object_path,
                                   stream_id);
    pending_send_flags = created_locally
        ? TP_MEDIA_STREAM_PENDING_REMOTE_SEND
        : TP_MEDIA_STREAM_PENDING_LOCAL_SEND;

    if (!created_locally)
      direction &= ~TP_MEDIA_STREAM_DIRECTION_SEND;

    if (rakia_media_session_is_local_hold_ongoing (self))
      direction &= ~TP_MEDIA_STREAM_DIRECTION_RECEIVE;

    media = rakia_sip_session_add_media (priv->sipsession,
        media_type, direction, created_locally);

    stream = g_object_new (RAKIA_TYPE_MEDIA_STREAM,
                           "dbus-daemon", priv->dbus_daemon,
			   "media-session", self,
                           "sip-media", media,
			   "media-type", media_type,
			   "object-path", object_path,
			   "id", stream_id,
                           "direction", direction,
                           "pending-send-flags", pending_send_flags,
                           "created-locally", created_locally,
			   NULL);

    g_free (object_path);

    g_signal_connect (stream, "close",
                      G_CALLBACK (priv_stream_close_cb),
                      self);
    g_signal_connect (stream, "state-changed",
                      G_CALLBACK (priv_stream_state_changed_cb),
                      self);
    g_signal_connect (stream, "direction-changed",
                      G_CALLBACK (priv_stream_direction_changed_cb),
                      priv->channel);
    g_signal_connect (stream, "notify::hold-state",
                      G_CALLBACK (priv_stream_hold_state_cb),
                      self);
    g_signal_connect (stream, "unhold-failure",
                      G_CALLBACK (priv_stream_unhold_failure_cb),
                      self);

    g_assert (priv->local_non_ready >= 0);
    ++priv->local_non_ready;

    if (priv->se_ready)
      priv_emit_new_stream (self, stream);

    tp_svc_channel_type_streamed_media_emit_stream_added (priv->channel,
                                                          stream_id,
                                                          priv->peer,
                                                          media_type);
    if (direction != TP_MEDIA_STREAM_DIRECTION_RECEIVE
        || pending_send_flags != TP_MEDIA_STREAM_PENDING_LOCAL_SEND)
      {
        tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
            priv->channel,
            stream_id,
            direction,
            pending_send_flags);
      }
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
    klass, (tp_svc_media_session_handler_##x##_impl) rakia_media_session_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}


gboolean
rakia_media_session_is_accepted (RakiaMediaSession *self)
{
  return rakia_sip_session_is_accepted (self->priv->sipsession);
}
