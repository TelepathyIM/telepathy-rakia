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

#include "signals-marshal.h"

#define DEBUG_FLAG TPSIP_DEBUG_MEDIA
#include "rakia/debug.h"

/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define TPSIP_REINVITE_TIMEOUT 180

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
  SIG_STATE_CHANGED,
  SIG_LAST_SIGNAL
};

/* properties */
enum
{
  PROP_MEDIA_CHANNEL = 1,
  PROP_DBUS_DAEMON,
  PROP_OBJECT_PATH,
  PROP_NUA_OP,
  PROP_PEER,
  PROP_HOLD_STATE,
  PROP_HOLD_STATE_REASON,
  PROP_REMOTE_PTIME,
  PROP_REMOTE_MAX_PTIME,
  PROP_RTCP_ENABLED,
  PROP_LOCAL_IP_ADDRESS,
  PROP_STUN_SERVERS,
  LAST_PROPERTY
};

static guint signals[SIG_LAST_SIGNAL] = {0};

#ifdef ENABLE_DEBUG

/**
 * Media session states:
 * - created, objects created, local cand/codec query ongoing
 * - invite-sent, an INVITE with local SDP sent, awaiting response
 * - invite-received, a remote INVITE received, response is pending
 * - response-received, a 200 OK received, codec intersection is in progress
 * - active, codecs and candidate pairs have been negotiated (note,
 *   SteamEngine might still fail to verify connectivity and report
 *   an error)
 * - reinvite-sent, a local re-INVITE sent, response is pending
 * - reinvite-received, a remote re-INVITE received, response is pending
 * - ended, session has ended
 */
static const char* session_states[] =
{
    "created",
    "invite-sent",
    "invite-received",
    "response-received",
    "active",
    "reinvite-sent",
    "reinvite-received",
    "reinvite-pending",
    "ended"
};

#endif /* ENABLE_DEBUG */

/* private structure */
typedef struct _RakiaMediaSessionPrivate RakiaMediaSessionPrivate;

struct _RakiaMediaSessionPrivate
{
  TpDBusDaemon *dbus_daemon;
  RakiaMediaChannel *channel;             /* see gobj. prop. 'media-channel' */
  gchar *object_path;                     /* see gobj. prop. 'object-path' */
  nua_handle_t *nua_op;                   /* see gobj. prop. 'nua-handle' */
  TpHandle peer;                          /* see gobj. prop. 'peer' */
  gchar *local_ip_address;                /* see gobj. prop. 'local-ip-address' */
  gchar *remote_ptime;                    /* see gobj. prop. 'remote-ptime' */
  gchar *remote_max_ptime;                /* see gobj. prop. 'remote-max-ptime' */
  gboolean rtcp_enabled;                  /* see gobj. prop. 'rtcp-enabled' */
  RakiaMediaSessionState state;           /* session state */
  TpLocalHoldState hold_state;         /* local hold state aggregated from stream directions */
  TpLocalHoldStateReason hold_reason;  /* last used hold state change reason */
  nua_saved_event_t saved_event[1];       /* Saved incoming request event */
  gint local_non_ready;                   /* number of streams with local information update pending */
  guint remote_stream_count;              /* number of streams last seen in a remote offer */
  guint glare_timer_id;
  su_home_t *home;                        /* Sofia memory home for remote SDP session structure */
  su_home_t *backup_home;                 /* Sofia memory home for previous generation remote SDP session*/
  sdp_session_t *remote_sdp;              /* last received remote session */
  sdp_session_t *backup_remote_sdp;       /* previous remote session */
  GPtrArray *streams;
  gboolean remote_initiated;              /*< session is remotely intiated */
  gboolean accepted;                      /*< session has been locally accepted for use */
  gboolean se_ready;                      /*< connection established with stream-engine */
  gboolean pending_offer;                 /*< local media have been changed, but a re-INVITE is pending */
  gboolean dispose_has_run;
};

#define TPSIP_MEDIA_SESSION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_MEDIA_SESSION, RakiaMediaSessionPrivate))

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

static void priv_request_response_step (RakiaMediaSession *session);
static void priv_session_invite (RakiaMediaSession *session, gboolean reinvite);
static void priv_local_media_changed (RakiaMediaSession *session);
static gboolean priv_update_remote_media (RakiaMediaSession *session,
                                          gboolean authoritative);
static void priv_save_event (RakiaMediaSession *self);
static void priv_zap_event (RakiaMediaSession *self);

static void rakia_media_session_init (RakiaMediaSession *obj)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (obj);

  priv->state = TPSIP_MEDIA_SESSION_STATE_CREATED;
  priv->hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
  priv->hold_reason = TP_LOCAL_HOLD_STATE_REASON_NONE;
  priv->rtcp_enabled = TRUE;

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
  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (TPSIP_MEDIA_SESSION (obj));

  g_assert (TP_IS_DBUS_DAEMON (priv->dbus_daemon));
  tp_dbus_daemon_register_object (priv->dbus_daemon, priv->object_path, obj);

  return obj;
}

static void rakia_media_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
  RakiaMediaSession *session = TPSIP_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

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
    case PROP_NUA_OP:
      g_value_set_pointer (value, priv->nua_op);
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
    case PROP_REMOTE_PTIME:
      g_value_set_string (value, priv->remote_ptime);
      break;
    case PROP_REMOTE_MAX_PTIME:
      g_value_set_string (value, priv->remote_max_ptime);
      break;
    case PROP_LOCAL_IP_ADDRESS:
      g_value_set_string (value, priv->local_ip_address);
      break;
    case PROP_RTCP_ENABLED:
      g_value_set_boolean (value, priv->rtcp_enabled);
      break;
    case PROP_STUN_SERVERS:
      {
        /* TODO: should be able to get all entries from the DNS lookup(s).
         * At the moment, rawudp ignores all servers except the first one. */
        GPtrArray *servers;
        gchar *stun_server = NULL;
        guint stun_port = TPSIP_DEFAULT_STUN_PORT;

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
  RakiaMediaSession *session = TPSIP_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id)
    {
    case PROP_DBUS_DAEMON:
      g_assert (priv->dbus_daemon == NULL);       /* construct-only */
      priv->dbus_daemon = g_value_dup_object (value);
      break;
    case PROP_MEDIA_CHANNEL:
      priv->channel = TPSIP_MEDIA_CHANNEL (g_value_get_object (value));
      break;
    case PROP_OBJECT_PATH:
      g_assert (priv->object_path == NULL);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_NUA_OP:
      /* you can only set the NUA handle once - migrating a media session
       * between two NUA handles makes no sense */
      g_return_if_fail (priv->nua_op == NULL);
      priv->nua_op = g_value_get_pointer (value);
      nua_handle_ref (priv->nua_op);
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
      TPSIP_TYPE_MEDIA_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_CHANNEL, param_spec);

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
      "The D-Bus object path used for this object on the bus.",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_pointer ("nua-handle", "NUA handle",
      "NUA stack operation handle associated with this media session.",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NUA_OP, param_spec);

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

  param_spec = g_param_spec_string ("remote-ptime",
      "a=ptime value of remote media session",
      "Value of the a=ptime attribute of the remote media session, or NULL",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_PTIME, param_spec);

  param_spec = g_param_spec_string ("remote-max-ptime",
      "a=maxptime value of remote media session",
      "Value of the a=maxptime attribute of the remote media session, or NULL",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_MAX_PTIME, param_spec);

  param_spec = g_param_spec_string ("local-ip-address", "Local IP address",
      "The local IP address preferred for media streams",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_LOCAL_IP_ADDRESS, param_spec);

  param_spec = g_param_spec_boolean ("rtcp-enabled", "RTCP enabled",
      "Is RTCP enabled session-wide",
      TRUE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RTCP_ENABLED, param_spec);

  param_spec = g_param_spec_boxed ("stun-servers", "STUN servers",
      "Array of IP address-port pairs for available STUN servers",
      TP_ARRAY_TYPE_SOCKET_ADDRESS_IP_LIST,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS, param_spec);

  signals[SIG_STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_OBJECT_CLASS_TYPE (klass),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                  0,
                  NULL, NULL,
                  _rakia_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
}

static void
rakia_media_session_dispose (GObject *object)
{
  RakiaMediaSession *self = TPSIP_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  DEBUG("enter");

  priv->dispose_has_run = TRUE;

  if (priv->glare_timer_id)
    g_source_remove (priv->glare_timer_id);

  tp_clear_object (&priv->dbus_daemon);

  if (G_OBJECT_CLASS (rakia_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_media_session_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
rakia_media_session_finalize (GObject *object)
{
  RakiaMediaSession *self = TPSIP_MEDIA_SESSION (object);
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  guint i;

  /* terminating the session should have discarded the NUA handle */
  g_assert (priv->nua_op == NULL);

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

  priv_zap_event (self);

  if (priv->home != NULL)
    su_home_unref (priv->home);
  if (priv->backup_home != NULL)
    su_home_unref (priv->backup_home);

  g_free (priv->remote_ptime);
  g_free (priv->remote_max_ptime);
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
  RakiaMediaSession *obj = TPSIP_MEDIA_SESSION (iface);

  SESSION_DEBUG (obj, "Media.SessionHandler::Error called (%s), terminating session", message);

  rakia_media_session_terminate (obj);

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
  RakiaMediaSession *obj = TPSIP_MEDIA_SESSION (iface);
  RakiaMediaSessionPrivate *priv;
  guint i;

  DEBUG ("enter");

  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (obj);

  if (!priv->se_ready)
    {
      priv->se_ready = TRUE;

      /* note: streams are generated in priv_create_media_stream() */

      for (i = 0; i < priv->streams->len; i++)
        {
          RakiaMediaStream *stream = g_ptr_array_index (priv->streams, i);
          if (stream)
            priv_emit_new_stream (obj, stream);
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
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  return priv->peer;
}

RakiaMediaSessionState
rakia_media_session_get_state (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  return priv->state;
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
priv_close_all_streams (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;
  for (i = 0; i < priv->streams->len; i++)
    {
      RakiaMediaStream *stream;
      stream = g_ptr_array_index (priv->streams, i);
      if (stream != NULL)
        rakia_media_stream_close (stream);
      g_assert (g_ptr_array_index (priv->streams, i) == NULL);
    }
}

static void
priv_apply_streams_pending_direction (RakiaMediaSession *session,
                                      guint pending_send_mask)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  guint i;

  /* If there has been a local change pending a re-INVITE,
   * suspend remote approval until the next transaction */
  if (priv->pending_offer)
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
rakia_media_session_change_state (RakiaMediaSession *session,
                                RakiaMediaSessionState new_state)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint old_state;

  if (priv->state == new_state)
    return;

  old_state = priv->state;
  priv->state = new_state;

  SESSION_DEBUG (session, "state change: %s -> %s",
      session_states[old_state],
      session_states[new_state]);

  switch (new_state)
    {
    case TPSIP_MEDIA_SESSION_STATE_CREATED:
    case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
    case TPSIP_MEDIA_SESSION_STATE_INVITE_SENT:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT:
    case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING:
      break;
    case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
      /* Apply any pending remote send after outgoing INVITEs.
       * We don't want automatic removal of pending local send after
       * responding to incoming re-INVITEs, however */
      priv_apply_streams_pending_direction (session,
          TP_MEDIA_STREAM_PENDING_REMOTE_SEND);
      break;
    case TPSIP_MEDIA_SESSION_STATE_ENDED:
      priv_close_all_streams (session);
      DEBUG("destroying the NUA handle %p", priv->nua_op);
      if (priv->nua_op != NULL)
        {
          nua_handle_destroy (priv->nua_op);
          priv->nua_op = NULL;
        }
      break;

      /* Don't add default because we want to be warned by the compiler
       * about unhandled states */
    }

  g_signal_emit (session, signals[SIG_STATE_CHANGED], 0, old_state, new_state);

  if (new_state == TPSIP_MEDIA_SESSION_STATE_ACTIVE && priv->pending_offer)
    priv_session_invite (session, TRUE);
}

#ifdef ENABLE_DEBUG
void
rakia_media_session_debug (RakiaMediaSession *session,
                           const gchar *format, ...)
{
  RakiaMediaSessionPrivate *priv;
  va_list list;
  gchar buf[240];

  if (!rakia_debug_flag_is_set (DEBUG_FLAG))
    return;

  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  va_start (list, format);

  g_vsnprintf (buf, sizeof (buf), format, list);

  va_end (list);

  DEBUG ("SIP media session [%-17s]: %s",
      session_states[priv->state], buf);
}
#endif /* ENABLE_DEBUG */

void rakia_media_session_terminate (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  DEBUG ("enter");

  if (priv->state == TPSIP_MEDIA_SESSION_STATE_ENDED)
    return;

  /* XXX: taken care of by the state change? */
  priv_close_all_streams (session);

  if (priv->nua_op != NULL)
    {
      /* XXX: should the stack do pretty much the same
       * (except freeing the saved event) upon nua_handle_destroy()? */
      switch (priv->state)
        {
        case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
        case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
        case TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT:
        case TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING:
          DEBUG("sending BYE");
          nua_bye (priv->nua_op, TAG_END());
          break;
        case TPSIP_MEDIA_SESSION_STATE_INVITE_SENT:
          DEBUG("sending CANCEL");
          nua_cancel (priv->nua_op, TAG_END());
          break;
        case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
          DEBUG("sending the 480 response to an incoming INVITE");
          nua_respond (priv->nua_op, 480, "Terminated", TAG_END());
          break;
        case TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
          if (priv->saved_event[0])
            {
              DEBUG("sending the 480 response to an incoming re-INVITE");
              nua_respond (priv->nua_op, 480, "Terminated",
                           NUTAG_WITH(nua_saved_event_request (priv->saved_event)),
                           TAG_END());
              nua_destroy_event (priv->saved_event);
            }
          DEBUG("sending BYE to terminate the call itself");
          nua_bye (priv->nua_op, TAG_END());
          break;
        default:
          /* let the Sofia stack decide what do to */;
        }
    }

  rakia_media_session_change_state (session, TPSIP_MEDIA_SESSION_STATE_ENDED);
}

gboolean
rakia_media_session_set_remote_media (RakiaMediaSession *session,
                                    const sdp_session_t* sdp)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  gboolean authoritative;

  DEBUG ("enter");

  if (priv->state == TPSIP_MEDIA_SESSION_STATE_INVITE_SENT
      || priv->state == TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT)
    {
      rakia_media_session_change_state (
                session,
                TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED);
    }
  else
    {
      /* Remember the m= line count in the remote offer,
       * to match it with exactly this number of answer lines */
      sdp_media_t *media;
      guint count = 0;

      for (media = sdp->sdp_media; media != NULL; media = media->m_next)
        ++count;

      priv->remote_stream_count = count;
    }

  /* Shortcut session non-updates */
  if (!sdp_session_cmp (priv->remote_sdp, sdp))
    goto finally;

  /* Delete a backup session structure, if any */
  if (priv->backup_remote_sdp != NULL)
    {
      priv->backup_remote_sdp = NULL;
      g_assert (priv->backup_home != NULL);
      su_home_unref (priv->backup_home);
      priv->backup_home = NULL;
    }
  /* Back up the old session.
   * The streams still need the old media descriptions */
  if (priv->remote_sdp != NULL)
    {
      g_assert (priv->home != NULL);
      g_assert (priv->backup_home == NULL);
      priv->backup_home = priv->home;
      priv->backup_remote_sdp = priv->remote_sdp;
    }

  /* Store the session description structure */
  priv->home = su_home_create ();
  priv->remote_sdp = sdp_session_dup (priv->home, sdp);
  g_return_val_if_fail (priv->remote_sdp != NULL, FALSE);

  authoritative = (priv->state == TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED
                || priv->state == TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED);
  if (!priv_update_remote_media (session, authoritative))
    return FALSE;

finally:
  /* Make sure to always transition states and send out the response,
   * even if no stream-engine roundtrips were initiated */
  priv_request_response_step (session);
  return TRUE;
}

void
priv_add_stream_list_entry (GPtrArray *list,
                            RakiaMediaStream *stream,
                            RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
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

  priv_local_media_changed (session);

  return TRUE;
}

gboolean
rakia_media_session_remove_streams (RakiaMediaSession *self,
                                  const GArray *stream_ids,
                                  GError **error)
{
  RakiaMediaStream *stream;
  guint stream_id;
  guint i;

  DEBUG ("enter");

  for (i = 0; i < stream_ids->len; i++)
    {
      stream_id = g_array_index (stream_ids, guint, i);
      stream = rakia_media_session_get_stream (self, stream_id, error);
      if (stream == NULL)
        return FALSE;
      rakia_media_stream_close (stream);
    }

  priv_local_media_changed (self);

  return TRUE;
}

void rakia_media_session_list_streams (RakiaMediaSession *session,
                                     GPtrArray *ret)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream)
        priv_add_stream_list_entry (ret, stream, session);
    }
}

gboolean
rakia_media_session_request_stream_direction (RakiaMediaSession *self,
                                              guint stream_id,
                                              guint direction,
                                              GError **error)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;

  stream = rakia_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "stream %u does not exist", stream_id);
      return FALSE;
    }

  SESSION_DEBUG (self, "direction %u requested for stream %u",
      direction, stream_id);

  if (priv->state == TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED
      || priv->state == TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED)
    {
      /* While processing a session offer, we can only mask out direction
       * requested by the remote peer */
      direction &= rakia_media_stream_get_requested_direction (stream);
    }

  rakia_media_stream_set_direction (stream,
                                    direction,
                                    TP_MEDIA_STREAM_PENDING_REMOTE_SEND);

  return TRUE;
}

static void
priv_save_event (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaBaseConnection *conn = NULL;

  priv_zap_event (self);

  g_object_get (priv->channel, "connection", &conn, NULL);

  g_return_if_fail (conn != NULL);

  rakia_base_connection_save_event (conn, priv->saved_event);

  g_object_unref (conn);

#ifdef ENABLE_DEBUG
  {
    nua_event_data_t const *ev_data = nua_event_data (priv->saved_event);
    g_assert (ev_data != NULL);
    DEBUG("saved the last event: %s %hd %s", nua_event_name (ev_data->e_event), ev_data->e_status, ev_data->e_phrase);
  }
#endif
}

static void
priv_zap_event (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->saved_event[0])
    {
      nua_event_data_t const *ev_data = nua_event_data (priv->saved_event);
      g_assert (ev_data != NULL);
      WARNING ("zapping unhandled saved event '%s'", nua_event_name (ev_data->e_event));
      nua_destroy_event (priv->saved_event);
    }
}

void
rakia_media_session_receive_invite (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  g_return_if_fail (priv->state == TPSIP_MEDIA_SESSION_STATE_CREATED);  
  g_return_if_fail (priv->nua_op != NULL);

  priv->remote_initiated = TRUE;

  nua_respond (priv->nua_op, SIP_180_RINGING, TAG_END());

  rakia_media_session_change_state (self, TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED);
}

void
rakia_media_session_receive_reinvite (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  /* Check for permitted state transitions */
  switch (priv->state)
    {
    case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
    case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      break;
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING:
      g_source_remove (priv->glare_timer_id);
      break;
    default:
      g_return_if_reached ();
    }

  priv_save_event (self);

  rakia_media_session_change_state (self, TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED);
}

void
rakia_media_session_accept (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->accepted)
    return;

  SESSION_DEBUG (self, "accepting the session");

  priv->accepted = TRUE;

  /* Apply the pending send flags */
  priv_apply_streams_pending_direction (self,
      TP_MEDIA_STREAM_PENDING_LOCAL_SEND |
      TP_MEDIA_STREAM_PENDING_REMOTE_SEND);

  /* Will change session state to active when streams are ready */
  priv_request_response_step (self);
}

void
rakia_media_session_respond (RakiaMediaSession *self,
                             gint status,
                             const char *message)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "responding: %03d %s", status, message ? message : "");

  if (message != NULL && !message[0])
    message = NULL;

  if (priv->nua_op)
    nua_respond (priv->nua_op, status, message, TAG_END());
}

gboolean rakia_media_session_is_accepted (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  return priv->accepted;
}

static gboolean
priv_glare_retry (gpointer session)
{
  RakiaMediaSession *self = session;
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "glare resolution interval is over");

  if (priv->state == TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING)
    priv_session_invite (self, TRUE);

  /* Reap the timer */
  priv->glare_timer_id = 0;
  return FALSE;
}

void
rakia_media_session_resolve_glare (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  guint interval;

  if (priv->state != TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT)
    {
      SESSION_DEBUG (self, "glare resolution triggered in unexpected state");
      return;
    }

  /*
   * Set the grace interval accordinlgly to RFC 3261 section 14.1:
   *
   *  1. If the UAC is the owner of the Call-ID of the dialog ID
   *     (meaning it generated the value), T has a randomly chosen value
   *     between 2.1 and 4 seconds in units of 10 ms.
   *  2. If the UAC is not the owner of the Call-ID of the dialog ID, T
   *     has a randomly chosen value of between 0 and 2 seconds in units
   *     of 10 ms.
   */
  if (priv->pending_offer)
    interval = 0;       /* cut short, we have new things to negotiate */
  else if (priv->remote_initiated)
    interval = g_random_int_range (0, 200) * 10;
  else
    interval = g_random_int_range (210, 400) * 10;

  if (priv->glare_timer_id != 0)
    g_source_remove (priv->glare_timer_id);

  priv->glare_timer_id = g_timeout_add (interval, priv_glare_retry, self);

  SESSION_DEBUG (self, "glare resolution interval %u msec", interval);

  rakia_media_session_change_state (
        self, TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING);
}

static RakiaMediaStream *
rakia_media_session_get_stream (RakiaMediaSession *self,
                                guint stream_id,
                                GError **error)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
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
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  return priv->hold_state;
}

static gboolean
rakia_media_session_is_local_hold_ongoing (RakiaMediaSession *self)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  return (priv->hold_state == TP_LOCAL_HOLD_STATE_HELD
          || priv->hold_state == TP_LOCAL_HOLD_STATE_PENDING_HOLD);
}

static void
priv_initiate_hold (RakiaMediaSession *self,
                    gboolean hold,
                    TpLocalHoldStateReason reason)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
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
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream;
  TpLocalHoldState final_hold_state;
  guint hold_mask;
  guint unhold_mask;
  guint i;
  gboolean held = FALSE;

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
      hold_mask = TP_MEDIA_STREAM_DIRECTION_SEND;
      unhold_mask = 0;
    }
  else
    {
      final_hold_state = TP_LOCAL_HOLD_STATE_UNHELD;
      hold_mask = TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
      unhold_mask = TP_MEDIA_STREAM_DIRECTION_RECEIVE;
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
          guint direction = rakia_media_stream_get_requested_direction (stream);
          direction &= hold_mask;
          direction |= unhold_mask;
          rakia_media_stream_set_direction (stream,
              direction,
              TP_MEDIA_STREAM_PENDING_REMOTE_SEND
                  | TP_MEDIA_STREAM_PENDING_LOCAL_SEND);
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
rakia_media_session_start_telephony_event (RakiaMediaSession *self,
                                         guint stream_id,
                                         guchar event,
                                         GError **error)
{
  RakiaMediaStream *stream;

  stream = rakia_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    return FALSE;

  if (rakia_media_stream_get_media_type (stream) != TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                   "non-audio stream %u does not support telephony events", stream_id);
      return FALSE;
    }

  DEBUG("starting telephony event %u on stream %u", (guint) event, stream_id);

  rakia_media_stream_start_telephony_event (stream, event);

  return TRUE;
}

gboolean
rakia_media_session_stop_telephony_event  (RakiaMediaSession *self,
                                         guint stream_id,
                                         GError **error)
{
  RakiaMediaStream *stream;

  stream = rakia_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    return FALSE;

  if (rakia_media_stream_get_media_type (stream) != TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                   "non-audio stream %u does not support telephony events; spurious use of the stop event?", stream_id);
      return FALSE;
    }

  DEBUG("stopping the telephony event on stream %u", stream_id);

  rakia_media_stream_stop_telephony_event (stream);

  return TRUE;
}

gint
rakia_media_session_rate_native_transport (RakiaMediaSession *session,
                                         const GValue *transport)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
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
priv_session_set_streams_playing (RakiaMediaSession *session, gboolean playing)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        rakia_media_stream_set_playing (stream, playing);
    }
}

static void
priv_local_media_changed (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  switch (priv->state)
    {
    case TPSIP_MEDIA_SESSION_STATE_CREATED:
      /* If all streams are ready, send an offer now */
      priv_request_response_step (session);
      break;
    case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
      /* The changes to existing streams will be included in the
       * eventual answer (FIXME: implement postponed direction changes,
       * which are applied after the remote offer has been processed).
       * Check, however, if there are new streams not present in the
       * remote offer, that will need another offer-answer round */
      if (priv->remote_stream_count < priv->streams->len)
        priv->pending_offer = TRUE;
      break;
    case TPSIP_MEDIA_SESSION_STATE_INVITE_SENT:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT:
    case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      /* Cannot send another offer right now */
      priv->pending_offer = TRUE;
      break;
    case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
      /* Check if we are allowed to send re-INVITES */
      {
        gboolean immutable_streams = FALSE;
        g_object_get (priv->channel,
            "immutable-streams", &immutable_streams,
            NULL);
        if (immutable_streams) {
          g_message ("sending of a local media update disabled by parameter 'immutable-streams'");
          break;
        }
      }
      /* Fall through to the next case */
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING:
      if (priv->local_non_ready == 0)
        priv_session_invite (session, TRUE);
      else
        priv->pending_offer = TRUE;
      break;
    default:
      g_assert_not_reached();
    }
}

static void
priv_update_remote_hold (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  RakiaMediaStream *stream;
  gboolean has_streams = FALSE;
  gboolean remote_held = TRUE;
  guint direction;
  guint i;

  /* The call is remotely unheld if there's at least one sending stream */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        {
          direction = rakia_media_stream_get_requested_direction (stream);

          if ((direction & TP_MEDIA_STREAM_DIRECTION_SEND) != 0)
            remote_held = FALSE;

          has_streams = TRUE;
        }
    }

  if (!has_streams)
    return;

  DEBUG("the session is remotely %s", remote_held? "held" : "unheld");

  if (remote_held)
    rakia_media_channel_change_call_state (priv->channel,
                                           priv->peer,
                                           TP_CHANNEL_CALL_STATE_HELD,
                                           0);
  else
    rakia_media_channel_change_call_state (priv->channel,
                                           priv->peer,
                                           0,
                                           TP_CHANNEL_CALL_STATE_HELD);
}

gchar *
rakia_sdp_get_string_attribute (const sdp_attribute_t *attrs, const char *name)
{
  sdp_attribute_t *attr;

  attr = sdp_attribute_find (attrs, name);
  if (attr == NULL)
    return NULL;

  return g_strdup (attr->a_value);
}

static gboolean
priv_update_remote_media (RakiaMediaSession *session, gboolean authoritative)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  const sdp_session_t *sdp = priv->remote_sdp;
  const sdp_media_t *media;
  gboolean has_supported_media = FALSE;
  guint direction_up_mask;
  guint pending_send_mask;
  guint i;

  g_return_val_if_fail (sdp != NULL, FALSE);

  /* Update the session-wide parameters
   * before updating streams' media */

  priv->remote_ptime     = rakia_sdp_get_string_attribute (
      sdp->sdp_attributes, "ptime");
  priv->remote_max_ptime = rakia_sdp_get_string_attribute (
      sdp->sdp_attributes, "maxptime");

  priv->rtcp_enabled = !rakia_sdp_rtcp_bandwidth_throttled (
                                sdp->sdp_bandwidths);

  /*
   * Do not allow:
   * 1) an answer to bump up directions beyond what's been offered;
   * 2) an offer to remove the local hold.
   */
  if (authoritative)
    direction_up_mask
        = rakia_media_session_is_local_hold_ongoing (session)
                ? TP_MEDIA_STREAM_DIRECTION_SEND
                : TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL;
  else
    direction_up_mask = 0;

  /* A remote media requesting to enable sending would need local approval.
   * Also, if there have been any local media updates pending a re-INVITE,
   * keep or bump the pending remote send flag on the streams: it will
   * be resolved in the next re-INVITE transaction */
  pending_send_mask = TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
  if (priv->pending_offer)
    pending_send_mask |= TP_MEDIA_STREAM_PENDING_REMOTE_SEND;

  media = sdp->sdp_media;

  /* note: for each session, we maintain an ordered list of
   *       streams (SDP m-lines) which are matched 1:1 to
   *       the streams of the remote SDP */

  for (i = 0; media != NULL; media = media->m_next, i++)
    {
      RakiaMediaStream *stream = NULL;
      guint media_type;

      media_type = rakia_tp_media_type (media->m_type);

      if (i >= priv->streams->len)
        stream = rakia_media_session_add_stream (
                        session,
                        media_type,
                        rakia_media_stream_direction_from_remote_media (media),
                        FALSE);
      else
        stream = g_ptr_array_index(priv->streams, i);

      /* note: it is ok for the stream to be NULL (unsupported media type) */
      if (stream == NULL)
        continue;

      DEBUG("setting remote SDP for stream %u", i);

      if (media->m_rejected)
        {
          DEBUG("the stream has been rejected, closing");
        }
      else if (rakia_media_stream_get_media_type (stream) != media_type)
        {
          /* XXX: close this stream and create a new one in its place? */
          WARNING ("The peer has changed the media type, don't know what to do");
        }
      else if (rakia_media_stream_set_remote_media (stream,
                                                    media,
                                                    direction_up_mask,
                                                    pending_send_mask))
        {
          has_supported_media = TRUE;
          continue;
        }

      /* There have been problems with the stream update, kill the stream */
      rakia_media_stream_close (stream);
    }
  g_assert(media == NULL);
  g_assert(i <= priv->streams->len);
  g_assert(!authoritative || i == priv->remote_stream_count);

  if (i < priv->streams->len && !priv->pending_offer)
    {
      /*
       * It's not defined what we should do if there are previously offered
       * streams not accounted in the remote SDP, in violation of RFC 3264.
       * Closing them off serves resource preservation and gives better
       * clue to the client as to the real state of the session.
       * Note that this situation is masked if any local media updates
       * have been requested and are pending until the present remote session
       * answer is received and applied. In such a case, we'll issue a new offer
       * at the closest available time, with the "overhanging" stream entries
       * intact.
       */
      do
        {
          RakiaMediaStream *stream;
          stream = g_ptr_array_index(priv->streams, i);
          if (stream != NULL)
            {
              MESSAGE ("closing a mismatched stream %u", i);
              rakia_media_stream_close (stream);
            }
        }
      while (++i < priv->streams->len);
    }

  if (has_supported_media)
    priv_update_remote_hold (session);

  DEBUG("exit");

  return has_supported_media;
}

static void
priv_session_rollback (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  DEBUG("enter");

  if (priv->remote_sdp != NULL)
    {
      priv->remote_sdp = NULL;
      g_assert (priv->home != NULL);
      su_home_unref (priv->home);
      priv->home = NULL;
    }
  if (priv->backup_remote_sdp == NULL)
    {
      rakia_media_session_terminate (session);
      return;
    }

  /* restore remote SDP from the backup */
  priv->remote_sdp = priv->backup_remote_sdp;
  g_assert (priv->backup_home != NULL);
  priv->home = priv->backup_home;
  priv->backup_remote_sdp = NULL;
  priv->backup_home = NULL;

  priv_update_remote_media (session, FALSE);

  if (priv->saved_event[0])
    {
      nua_respond (priv->nua_op, SIP_488_NOT_ACCEPTABLE,
                   NUTAG_WITH(nua_saved_event_request (priv->saved_event)),
                   TAG_END());
      nua_destroy_event (priv->saved_event);
    }
  else
    {
      nua_respond (priv->nua_op, SIP_488_NOT_ACCEPTABLE,
                   TAG_END());
    }

  rakia_media_session_change_state (session, TPSIP_MEDIA_SESSION_STATE_ACTIVE);
}

static gboolean
priv_session_local_sdp (RakiaMediaSession *session,
                        GString *user_sdp,
                        gboolean authoritative)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  gboolean has_supported_media = FALSE;
  guint len;
  guint i;

  g_return_val_if_fail (priv->local_non_ready == 0, FALSE);

  len = priv->streams->len;
  if (!authoritative && len > priv->remote_stream_count)
    {
      len = priv->remote_stream_count;
      DEBUG("clamped response to %u streams seen in the offer", len);
    }

  g_string_append (user_sdp, "v=0\r\n");

  for (i = 0; i < len; i++)
    {
      RakiaMediaStream *stream = g_ptr_array_index (priv->streams, i);
      if (stream)
        {
          user_sdp = g_string_append (user_sdp,
                                      rakia_media_stream_local_sdp (stream));
          has_supported_media = TRUE;
        }
      else
        { 
          user_sdp = g_string_append (user_sdp, "m=audio 0 RTP/AVP 0\r\n");
        }
    }

  return has_supported_media;
}

static void
priv_session_invite (RakiaMediaSession *session, gboolean reinvite)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  GString *user_sdp;

  DEBUG("enter");

  g_return_if_fail (priv->nua_op != NULL);

  user_sdp = g_string_new (NULL);

  if (priv_session_local_sdp (session, user_sdp, TRUE))
    {
      /* We need to be prepared to receive media right after the
       * offer is sent, so we must set the streams to playing */
      priv_session_set_streams_playing (session, TRUE);

      nua_invite (priv->nua_op,
                  SOATAG_USER_SDP_STR(user_sdp->str),
                  SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
                  SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
                  NUTAG_AUTOANSWER(0),
                  TAG_IF(reinvite,
                         NUTAG_INVITE_TIMER (TPSIP_REINVITE_TIMEOUT)),
                  TAG_END());
      priv->pending_offer = FALSE;

      rakia_media_session_change_state (
                session,
                reinvite? TPSIP_MEDIA_SESSION_STATE_REINVITE_SENT
                        : TPSIP_MEDIA_SESSION_STATE_INVITE_SENT);
    }
  else
    WARNING ("cannot send a valid SDP offer, are there no streams?");

  g_string_free (user_sdp, TRUE);
}

static void
priv_session_respond (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  GString *user_sdp;

  g_return_if_fail (priv->nua_op != NULL);

  user_sdp = g_string_new (NULL);

  if (priv_session_local_sdp (session, user_sdp, FALSE))
    {
      msg_t *msg;

      /* We need to be prepared to receive media right after the
       * answer is sent, so we must set the streams to playing */
      priv_session_set_streams_playing (session, TRUE);

      msg = (priv->saved_event[0])
                ? nua_saved_event_request (priv->saved_event) : NULL;

      nua_respond (priv->nua_op, SIP_200_OK,
                   TAG_IF(msg, NUTAG_WITH(msg)),
                   SOATAG_USER_SDP_STR (user_sdp->str),
                   SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
                   SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
                   NUTAG_AUTOANSWER(0),
                   TAG_END());

      if (priv->saved_event[0])
        nua_destroy_event (priv->saved_event);

      rakia_media_session_change_state (session, TPSIP_MEDIA_SESSION_STATE_ACTIVE);
    }
  else
    {
      WARNING ("cannot respond with a valid SDP answer, were all streams closed?");

      priv_session_rollback (session);
    }

  g_string_free (user_sdp, TRUE);
}

static gboolean
priv_is_codec_intersect_pending (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      RakiaMediaStream *stream = g_ptr_array_index (priv->streams, i);
      if (stream != NULL
          && rakia_media_stream_is_codec_intersect_pending (stream))
        return TRUE;
    }

  return FALSE;
}

/**
 * Sends requests and responses with an outbound offer/answer
 * if all streams of the session are prepared.
 * 
 * Following inputs are considered in decision making:
 *  - state of the session (is remote INVITE being handled)  
 *  - status of local streams (set up with stream-engine)
 *  - whether session is locally accepted
 */
static void
priv_request_response_step (RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->local_non_ready != 0)
    {
      DEBUG("there are local streams not ready, postponed");
      return;
    }

  switch (priv->state)
    {
    case TPSIP_MEDIA_SESSION_STATE_CREATED:
      priv_session_invite (session, FALSE);
      break;
    case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        rakia_media_session_change_state (session,
                                        TPSIP_MEDIA_SESSION_STATE_ACTIVE);
      break;
    case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
      /* TODO: if the call has not yet been accepted locally
       * and the remote endpoint supports 100rel, send them
       * an early session answer in a reliable 183 response */
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        priv_session_respond (session);
      break;
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
      if (!priv_is_codec_intersect_pending (session))
        priv_session_respond (session);
      break;
    case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
    case TPSIP_MEDIA_SESSION_STATE_REINVITE_PENDING:
      if (priv->pending_offer)
        priv_session_invite (session, TRUE);
      break;
    default:
      SESSION_DEBUG (session, "no action taken in the current state");
    }
}

static void
priv_stream_close_cb (RakiaMediaStream *stream,
                      RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv;
  guint id;

  DEBUG("enter");

  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  id = rakia_media_stream_get_id (stream);
  g_return_if_fail (g_ptr_array_index(priv->streams, id) == stream);

  if (!rakia_media_stream_is_local_ready (stream))
    {
      g_assert (priv->local_non_ready > 0);
      --priv->local_non_ready;
      DEBUG("stream wasn't ready, decrement the local non ready counter to %d", priv->local_non_ready);
    }

  g_object_unref (stream);

  g_ptr_array_index(priv->streams, id) = NULL;

  tp_svc_channel_type_streamed_media_emit_stream_removed (priv->channel, id);
}

static void priv_stream_ready_cb (RakiaMediaStream *stream,
				  RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv;

  DEBUG ("enter");

  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  g_assert (priv->local_non_ready > 0);
  --priv->local_non_ready;

  priv_request_response_step (session);
}

static void priv_stream_supported_codecs_cb (RakiaMediaStream *stream,
					     guint num_codecs,
					     RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv;

  priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);

  g_assert (!rakia_media_stream_is_codec_intersect_pending (stream));

  if (num_codecs == 0)
    {
      /* This remote media description got no codec intersection. */
      switch (priv->state)
        {
        case TPSIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
        case TPSIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
          DEBUG("no codec intersection, closing the stream");
          rakia_media_stream_close (stream);
          break;
        case TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
          /* In this case, we have the stream negotiated already,
           * and we don't want to close it just because the remote party
           * offers a different set of codecs.
           * Roll back the whole session to the previously negotiated state. */
          priv_session_rollback (session);
          return;
        case TPSIP_MEDIA_SESSION_STATE_ACTIVE:
          /* We've most likely rolled back from
           * TPSIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED,
           * but we may receive more than one empty codec intersection
           * in the session, so we ignore the rest */
          return;
        default:
          g_assert_not_reached();
        }
    }

  priv_request_response_step (session);
}

static void
priv_stream_state_changed_cb (RakiaMediaStream *stream,
                              guint state,
                              RakiaMediaChannel *channel)
{
  g_assert (TPSIP_IS_MEDIA_CHANNEL (channel));
  tp_svc_channel_type_streamed_media_emit_stream_state_changed(
        channel,
        rakia_media_stream_get_id (stream), state);
}

static void
priv_stream_direction_changed_cb (RakiaMediaStream *stream,
                                  guint direction,
                                  guint pending_send_flags,
                                  RakiaMediaChannel *channel)
{
  g_assert (TPSIP_IS_MEDIA_CHANNEL (channel));
  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
        channel,
        rakia_media_stream_get_id (stream), direction, pending_send_flags);
}

static void
priv_stream_hold_state_cb (RakiaMediaStream *stream,
                           GParamSpec *pspec,
                           RakiaMediaSession *session)
{
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (session);
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
      MESSAGE ("unexpected hold state change from a stream");

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
              DEBUG("hold/unhold not complete yet");
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
  RakiaMediaSessionPrivate *priv = TPSIP_MEDIA_SESSION_GET_PRIVATE (self);
  RakiaMediaStream *stream = NULL;

  DEBUG ("enter");

  if (rakia_media_session_supports_media_type (media_type)) {
    guint stream_id;
    gchar *object_path;
    guint pending_send_flags;

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

    stream = g_object_new (TPSIP_TYPE_MEDIA_STREAM,
                           "dbus-daemon", priv->dbus_daemon,
			   "media-session", self,
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
    g_signal_connect (stream, "ready",
		      G_CALLBACK (priv_stream_ready_cb),
		      self);
    g_signal_connect (stream, "supported-codecs",
		      G_CALLBACK (priv_stream_supported_codecs_cb),
		      self);
    g_signal_connect (stream, "state-changed",
                      G_CALLBACK (priv_stream_state_changed_cb),
                      priv->channel);
    g_signal_connect (stream, "direction-changed",
                      G_CALLBACK (priv_stream_direction_changed_cb),
                      priv->channel);
    g_signal_connect_swapped (stream, "local-media-updated",
                              G_CALLBACK (priv_local_media_changed),
                              self);
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

/* Checks if RTCP is not disabled with bandwidth modifiers
 * as described in RFC 3556 */
gboolean
rakia_sdp_rtcp_bandwidth_throttled (const sdp_bandwidth_t *b)
{
  const sdp_bandwidth_t *b_RS = NULL;
  const sdp_bandwidth_t *b_RR = NULL;

  while (b != NULL)
    {
      if (b->b_modifier_name != NULL)
        {
          if (strcmp (b->b_modifier_name, "RS") == 0)
            b_RS = b;
          else if (strcmp (b->b_modifier_name, "RR") == 0)
            b_RR = b;
        }
      b = b->b_next;
    }

  return (b_RS != NULL && b_RS->b_value == 0
       && b_RR != NULL && b_RR->b_value == 0);
}
