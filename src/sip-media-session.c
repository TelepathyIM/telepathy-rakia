/*
 * sip-media-session.c - Source for SIPMediaSession
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005,2006,2007 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <first.surname@nokia.com>
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
  PROP_NUA_OP,
  PROP_PEER,
  PROP_LOCAL_IP_ADDRESS,
  PROP_STATE,
  LAST_PROPERTY
};

static guint signals[SIG_LAST_SIGNAL] = {0};

#ifdef ENABLE_DEBUG

/**
 * StreamEngine session states:
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
    "ended"
};

#endif /* ENABLE_DEBUG */

/* private structure */
typedef struct _SIPMediaSessionPrivate SIPMediaSessionPrivate;

struct _SIPMediaSessionPrivate
{
  SIPMediaChannel *channel;             /** see gobj. prop. 'media-channel' */
  gchar *object_path;                   /** see gobj. prop. 'object-path' */
  nua_handle_t *nua_op;                 /** see gobj. prop. 'nua-handle' */
  TpHandle peer;                        /** see gobj. prop. 'peer' */
  gchar *local_ip_address;              /** see gobj. prop. 'local-ip-address' */
  SIPMediaSessionState state;           /** see gobj. prop. 'state' */
  nua_saved_event_t saved_event[1];     /** Saved incoming request event */
  gint local_non_ready;                 /** number of streams with local information update pending */
  guint catcher_id;
  guint timer_id;
  su_home_t *home;                      /** Sofia memory home for remote SDP session structure */
  su_home_t *backup_home;               /** Sofia memory home for previous generation remote SDP session*/
  sdp_session_t *remote_sdp;            /** last received remote session */
  sdp_session_t *backup_remote_sdp;     /** previous remote session */
  GPtrArray *streams;
  gboolean accepted;                    /**< session has been locally accepted for use */
  gboolean se_ready;                    /**< connection established with stream-engine */
  gboolean pending_offer;               /**< local media have been changed, but a re-INVITE is pending */
  gboolean dispose_has_run;
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

static SIPMediaStream *
sip_media_session_get_stream (SIPMediaSession *self,
                              guint stream_id,
                              GError **error);

static void priv_session_state_changed (SIPMediaSession *session,
                                        SIPMediaSessionState prev_state);
static gboolean priv_catch_remote_nonupdate (gpointer data);
static gboolean priv_timeout_session (gpointer data);
static SIPMediaStream* priv_create_media_stream (SIPMediaSession *session,
                                                 guint media_type,
                                                 guint pending_send_flags);
static void priv_request_response_step (SIPMediaSession *session);
static void priv_session_invite (SIPMediaSession *session, gboolean reinvite);
static void priv_local_media_changed (SIPMediaSession *session);
static gboolean priv_update_remote_media (SIPMediaSession *session,
                                          gboolean authoritative);
static void priv_save_event (SIPMediaSession *self);
static void priv_zap_event (SIPMediaSession *self);

static void sip_media_session_init (SIPMediaSession *obj)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (obj);

  priv->state = SIP_MEDIA_SESSION_STATE_CREATED;

  /* allocate any data required by the object here */
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

  if (priv->nua_op)
    {
      nua_hmagic_t *nua_op_magic;

      g_assert (priv->channel != NULL);

      /* migrating a NUA handle between two active media channels
       * makes no sense either */
      nua_op_magic = nua_handle_magic (priv->nua_op);
      g_return_val_if_fail (nua_op_magic == NULL || nua_op_magic == priv->channel, NULL);

      /* tell the NUA that we're handling this call */
      nua_handle_bind (priv->nua_op, priv->channel);
    }

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
    case PROP_NUA_OP:
      g_value_set_pointer (value, priv->nua_op);
      break;
    case PROP_PEER:
      g_value_set_uint (value, priv->peer);
      break;
    case PROP_LOCAL_IP_ADDRESS:
      g_value_set_string (value, priv->local_ip_address);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void sip_media_session_set_property (GObject      *object,
					    guint         property_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
  SIPMediaSession *session = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  switch (property_id) {
    case PROP_MEDIA_CHANNEL:
      priv->channel = SIP_MEDIA_CHANNEL (g_value_get_object (value));
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
    case PROP_STATE:
      priv_session_state_changed (session, g_value_get_uint (value));
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
                                    "media session object (not reference counted).",
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

  param_spec = g_param_spec_pointer("nua-handle", "Sofia-SIP NUA operator handle",
                                    "NUA stack operation handle associated "
                                    "with this media session.",
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NUA_OP, param_spec);

  param_spec = g_param_spec_uint ("peer", "Session peer",
                                  "The TpHandle representing the contact "
                                  "with whom this session communicates.",
                                  0, G_MAXUINT32, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_string ("local-ip-address", "Local IP address",
                                    "The local IP address preferred for "
                                    "media streams",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_LOCAL_IP_ADDRESS, param_spec);

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

  DEBUG("enter");

  priv->dispose_has_run = TRUE;

  if (priv->catcher_id)
    g_source_remove (priv->catcher_id);

  if (priv->timer_id)
    g_source_remove (priv->timer_id);

  if (G_OBJECT_CLASS (sip_media_session_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_session_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
sip_media_session_finalize (GObject *object)
{
  SIPMediaSession *self = SIP_MEDIA_SESSION (object);
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  guint i;

  /* terminating the session should have discarded the NUA handle */
  g_assert (priv->nua_op == NULL);

  /* free any data held directly by the object here */

  for (i = 0; i < priv->streams->len; i++) {
    SIPMediaStream *stream = g_ptr_array_index (priv->streams, i);
    if (stream != NULL)
      {
        g_warning ("stream %u (%p) left over, reaping", i, stream);
        g_object_unref (stream);
      }
  }
  g_ptr_array_free(priv->streams, TRUE);

  priv_zap_event (self);

  if (priv->home != NULL)
    su_home_unref (priv->home);
  if (priv->backup_home != NULL)
    su_home_unref (priv->backup_home);

  g_free (priv->local_ip_address);
  g_free (priv->object_path);

  G_OBJECT_CLASS (sip_media_session_parent_class)->finalize (object);

  DEBUG("exit");
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

static void
priv_close_all_streams (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;
  for (i = 0; i < priv->streams->len; i++)
    {
      SIPMediaStream *stream;
      stream = g_ptr_array_index (priv->streams, i);
      if (stream != NULL)
        sip_media_stream_close (stream);
      g_assert (g_ptr_array_index (priv->streams, i) == NULL);
    }
}

static void
priv_release_streams_pending_send (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  SIPMediaStream *stream;
  guint i;

  /* Clear the local pending send flags where applicable */
  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        sip_media_stream_release_pending_send (stream);
    }
}

static void
priv_session_state_changed (SIPMediaSession *session,
                            SIPMediaSessionState new_state)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->state == new_state)
    return;

  SESSION_DEBUG(session, "state changed from %s to %s",
                session_states[priv->state],
                session_states[new_state]);

  priv->state = new_state;

  switch (new_state)
    {
    case SIP_MEDIA_SESSION_STATE_CREATED:
      break;
    case SIP_MEDIA_SESSION_STATE_ENDED:
      priv_close_all_streams (session);
      DEBUG("freeing the NUA handle %p", priv->nua_op);
      if (priv->nua_op != NULL)
        {
          nua_handle_bind (priv->nua_op, SIP_NH_EXPIRED);
          nua_handle_unref (priv->nua_op);
          priv->nua_op = NULL;
        }
      break;
    case SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
    case SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
      priv->catcher_id = g_idle_add (priv_catch_remote_nonupdate, session);
      /* Fall through to the next case */
    case SIP_MEDIA_SESSION_STATE_INVITE_SENT:
    case SIP_MEDIA_SESSION_STATE_REINVITE_SENT:
      if (priv->timer_id)
        {
          g_source_remove (priv->timer_id);
        }
      priv->timer_id =
        g_timeout_add (DEFAULT_SESSION_TIMEOUT, priv_timeout_session, session);
      break;
    case SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      break;
    case SIP_MEDIA_SESSION_STATE_ACTIVE:
      if (priv->timer_id)
        {
	  g_source_remove (priv->timer_id);
	  priv->timer_id = 0;
        }
      priv_release_streams_pending_send (session);
      if (priv->pending_offer)
        {
          priv_session_invite (session, TRUE);
        }
      break;

    /* Don't add default because we want to be warned by the compiler
     * about unhandled states */
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

static gboolean
priv_catch_remote_nonupdate (gpointer data)
{
  SIPMediaSession *session = data;

  DEBUG("called");

  /* Accordingly to the last experimental data, non-modifying INVITEs
   * cause the stack to emit nua_i_state nonetheless */
  g_assert_not_reached();

  /* TODO: figure out what happens in the 3pcc scenario when we get
   * an INVITE but no session offer */

  /* Should do the right thing if there were no remote media updates */
  priv_request_response_step (session);

  return FALSE;
}

static gboolean priv_timeout_session (gpointer data)
{
  SIPMediaSession *session = data;
  SIPMediaSessionPrivate *priv;
  TpChannelGroupChangeReason reason;
  gboolean change = FALSE;
  TpHandle actor;

  DEBUG("session timed out");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session); 

  if (priv->state == SIP_MEDIA_SESSION_STATE_INVITE_SENT)
    {
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER;
      actor = 0;
      change = TRUE;
    }
  else if (priv->state == SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED)
    {
      reason = TP_CHANNEL_GROUP_CHANGE_REASON_NONE;
      actor = priv->peer;
      change = TRUE;
    }

  if (change)
    {
      TpIntSet *set = tp_intset_new ();
      tp_intset_add (set, priv->peer);
      tp_group_mixin_change_members (G_OBJECT (priv->channel), "Timed out",
                                     NULL, set, NULL, NULL, actor, reason);
      tp_intset_destroy (set);
    }

  sip_media_session_terminate (session);

  return FALSE;
}

void sip_media_session_terminate (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  DEBUG ("enter");

  if (priv->state == SIP_MEDIA_SESSION_STATE_ENDED)
    return;

  /* XXX: taken care of by the state change? */
  priv_close_all_streams (session);

  if (priv->nua_op != NULL)
    {
      g_assert (nua_handle_magic (priv->nua_op) == priv->channel);

      switch (priv->state)
        {
        case SIP_MEDIA_SESSION_STATE_ACTIVE:
        case SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
        case SIP_MEDIA_SESSION_STATE_REINVITE_SENT:
          DEBUG("sending BYE");
          nua_bye (priv->nua_op, TAG_END());
          break;
        case SIP_MEDIA_SESSION_STATE_INVITE_SENT:
          DEBUG("sending CANCEL");
          nua_cancel (priv->nua_op, TAG_END());
          break;
        case SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
          DEBUG("sending the 480 response to an incoming INVITE");
          nua_respond (priv->nua_op, 480, "Terminated", TAG_END());
          break;
        case SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
          DEBUG("sending the 480 response to an incoming re-INVITE");
          {
            msg_t *msg;

            msg = (priv->saved_event[0])
                        ? nua_saved_event_request (priv->saved_event) : NULL;
                
            nua_respond (priv->nua_op, 480, "Terminated",
                         TAG_IF(msg, NUTAG_WITH(msg)),
                         TAG_END());
          }
          DEBUG("sending BYE to terminate the call itself");
          nua_bye (priv->nua_op, TAG_END());
          break;
        default:
          /* let the Sofia stack decide what do to */;
        }
    }

  g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_ENDED, NULL);
}

gboolean
sip_media_session_set_remote_media (SIPMediaSession *session,
                                    const sdp_session_t* sdp)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  DEBUG ("enter");

  /* Remove the non-update catcher because we've got an update */
  if (priv->catcher_id)
    {
      g_source_remove (priv->catcher_id);
      priv->catcher_id = 0;
    }

  /* Switch the state machine to processing the response */
  if (priv->state == SIP_MEDIA_SESSION_STATE_INVITE_SENT
      || priv->state == SIP_MEDIA_SESSION_STATE_REINVITE_SENT)
    g_object_set (session,
                  "state", SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED,
                  NULL);

  /* Handle session non-updates */
  if (!sdp_session_cmp (priv->remote_sdp, sdp))
    {
      /* Should do the proper response etc. */
      priv_request_response_step (session);
      return TRUE;
    }

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

  return priv_update_remote_media (
                session,
                (priv->state == SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED
                 || priv->state == SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED));
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
                "state", &connection_state,
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

    stream = priv_create_media_stream (session,
                                       media_type,
                                       TP_MEDIA_STREAM_PENDING_REMOTE_SEND);

    priv_add_stream_list_entry (*ret, stream, session);
  }

  priv_local_media_changed (session);

  return TRUE;
}

gboolean
sip_media_session_remove_streams (SIPMediaSession *self,
                                  const GArray *stream_ids,
                                  GError **error)
{
  SIPMediaStream *stream;
  guint stream_id;
  guint i;

  DEBUG ("enter");

  for (i = 0; i < stream_ids->len; i++)
    {
      stream_id = g_array_index (stream_ids, guint, i);
      stream = sip_media_session_get_stream (self, stream_id, error);
      if (stream == NULL)
        return FALSE;
      sip_media_stream_close (stream);
    }

  priv_local_media_changed (self);

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

gboolean
sip_media_session_request_stream_direction (SIPMediaSession *self,
                                            guint stream_id,
                                            guint direction,
                                            GError **error)
{
  SIPMediaStream *stream;

  stream = sip_media_session_get_stream (self, stream_id, error);
  if (stream == NULL)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                   "stream %u does not exist", stream_id);
      return FALSE;
    }

  if (sip_media_stream_set_direction (stream, direction, FALSE))
    priv_local_media_changed (self);

  return TRUE;
}

static void
priv_save_event (SIPMediaSession *self)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  SIPConnection *conn = NULL;

  priv_zap_event (self);

  g_object_get (priv->channel, "connection", &conn, NULL);

  g_return_if_fail (conn != NULL);

  sip_conn_save_event (conn, priv->saved_event);

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
priv_zap_event (SIPMediaSession *self)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->saved_event[0])
    {
      nua_event_data_t const *ev_data = nua_event_data (priv->saved_event);
      g_assert (ev_data != NULL);
      g_warning ("zapping unhandled saved event '%s'", nua_event_name (ev_data->e_event));
      nua_destroy_event (priv->saved_event);
    }
}

void
sip_media_session_receive_invite (SIPMediaSession *self)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  g_return_if_fail (priv->state == SIP_MEDIA_SESSION_STATE_CREATED);  
  g_return_if_fail (priv->nua_op != NULL);

  nua_respond (priv->nua_op, SIP_180_RINGING, TAG_END());

  g_object_set (self, "state", SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED, NULL);
}

void
sip_media_session_receive_reinvite (SIPMediaSession *self)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);
  g_return_if_fail (priv->state == SIP_MEDIA_SESSION_STATE_ACTIVE
                    || priv->state == SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED);

  priv_save_event (self);

  g_object_set (self, "state", SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED, NULL);
}

void
sip_media_session_accept (SIPMediaSession *self)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (priv->accepted)
    return;

  SESSION_DEBUG(self, "accepting the session");

  priv->accepted = TRUE;

  /* Will change session state to active when streams are ready
   * and clear the pending send flags, enabling sending */
  priv_request_response_step (self);
}

void
sip_media_session_reject (SIPMediaSession *self,
                          gint status,
                          const char *message)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (message != NULL && !message[0])
    message = NULL;

  SESSION_DEBUG(self, "responding: %03d %s", status, message == NULL? "" : message);

  if (priv->nua_op)
    nua_respond (priv->nua_op, status, message, TAG_END());
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

gint
sip_media_session_rate_native_transport (SIPMediaSession *session,
                                         const GValue *transport)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
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

static void priv_session_media_state (SIPMediaSession *session, gboolean playing)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  SIPMediaStream *stream;
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      stream = g_ptr_array_index(priv->streams, i);
      if (stream != NULL)
        sip_media_stream_set_playing (stream, playing);
    }
}

static void
priv_local_media_changed (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  switch (priv->state)
    {
    case SIP_MEDIA_SESSION_STATE_CREATED:
    case SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
    case SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
      /* The changes will be sent when all streams are ready;
       * check if now's the time */
      priv_request_response_step (session);
      break;
    case SIP_MEDIA_SESSION_STATE_INVITE_SENT:
    case SIP_MEDIA_SESSION_STATE_REINVITE_SENT:
    case SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      /* Cannot send another offer right now */
      priv->pending_offer = TRUE;
      break;
    case SIP_MEDIA_SESSION_STATE_ACTIVE:
      if (priv->local_non_ready == 0)
        priv_session_invite (session, TRUE);
      else
        priv->pending_offer = TRUE;
      break;
    default:
      g_assert_not_reached();
    }
}

static gboolean
priv_update_remote_media (SIPMediaSession *session, gboolean authoritative)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  const sdp_media_t *media;
  gboolean has_supported_media = FALSE;
  guint i;

  g_return_val_if_fail (priv->remote_sdp != NULL, FALSE);

  media = priv->remote_sdp->sdp_media;

  /* note: for each session, we maintain an ordered list of
   *       streams (SDP m-lines) which are matched 1:1 to
   *       the streams of the remote SDP */

  for (i = 0; media; media = media->m_next, i++)
    {
      SIPMediaStream *stream = NULL;
      guint media_type;

      media_type = sip_tp_media_type (media->m_type);

      if (i >= priv->streams->len)
        stream = priv_create_media_stream (
                        session,
                        media_type,
                        (priv->accepted)?
                                0 : TP_MEDIA_STREAM_PENDING_LOCAL_SEND);
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
      else if (sip_media_stream_get_media_type (stream) != media_type)
        {
          /* XXX: close this stream and create a new one in its place? */
          g_warning ("The peer has changed the media type, don't know what to do");
        }
      else
        {
          if (sip_media_stream_set_remote_media (stream,
                                                 media,
                                                 authoritative))
            {
              has_supported_media = TRUE;
              continue;
            }
        }

      /* There have been problems with the stream update, kill the stream */
      sip_media_stream_close (stream);
    }

  g_assert(media == NULL);
  g_assert(i <= priv->streams->len);
  if (i < priv->streams->len)
    {
      do
        {
          SIPMediaStream *stream;
          stream = g_ptr_array_index(priv->streams, i);
          if (stream != NULL)
            {
              g_message ("closing a mismatched stream %u", i);
              sip_media_stream_close (stream);
            }
        }
      while (++i < priv->streams->len);
    }

  DEBUG("exit");

  return has_supported_media;
}

static void
priv_session_rollback (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  msg_t *msg;

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
      sip_media_session_terminate (session);
      return;
    }

  /* restore remote SDP from the backup */
  priv->remote_sdp = priv->backup_remote_sdp;
  g_assert (priv->backup_home != NULL);
  priv->home = priv->backup_home;
  priv->backup_remote_sdp = NULL;
  priv->backup_home = NULL;

  priv_update_remote_media (session, FALSE);

  msg = (priv->saved_event[0])
        ? nua_saved_event_request (priv->saved_event) : NULL;

  nua_respond (priv->nua_op, 488, sip_488_Not_acceptable,
               TAG_IF(msg, NUTAG_WITH(msg)),
               TAG_END());

  if (priv->saved_event[0])
    nua_destroy_event (priv->saved_event);

  g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_ACTIVE, NULL);
}

static gboolean
priv_session_local_sdp (SIPMediaSession *session, GString *user_sdp)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  gboolean has_supported_media = FALSE;
  guint i;

  g_return_val_if_fail (priv->local_non_ready == 0, FALSE);

  g_string_append (user_sdp, "v=0\r\n");

  for (i = 0; i < priv->streams->len; i++)
    {
      SIPMediaStream *stream = g_ptr_array_index (priv->streams, i);
      if (stream)
        {
          user_sdp = g_string_append (user_sdp,
                                      sip_media_stream_local_sdp (stream));
          has_supported_media = TRUE;
        }
      else
        { 
          user_sdp = g_string_append (user_sdp, "m=audio 0 RTP/AVP 0\r\n");
        }
    }

  SESSION_DEBUG(session, "generated SDP: {\n%s}", user_sdp->str);

  return has_supported_media;
}

static void
priv_session_invite (SIPMediaSession *session, gboolean reinvite)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  GString *user_sdp;

  DEBUG("enter");

  g_return_if_fail (priv->nua_op != NULL);

  user_sdp = g_string_new (NULL);

  if (priv_session_local_sdp (session, user_sdp))
    {
      nua_invite (priv->nua_op,
                  SOATAG_USER_SDP_STR(user_sdp->str),
                  SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
                  SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
                  NUTAG_AUTOANSWER(0),
                  TAG_END());
      priv->pending_offer = FALSE;
      g_object_set (session,
                    "state", reinvite? SIP_MEDIA_SESSION_STATE_REINVITE_SENT
                                     : SIP_MEDIA_SESSION_STATE_INVITE_SENT,
                    NULL);
    }
  else
    g_warning ("cannot send a valid SDP offer, are there no streams?");

  g_string_free (user_sdp, TRUE);
}

static void
priv_session_respond (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  GString *user_sdp;

  g_return_if_fail (priv->nua_op != NULL);

  user_sdp = g_string_new (NULL);

  if (priv_session_local_sdp (session, user_sdp))
    {
      msg_t *msg;

      msg = (priv->saved_event[0])
                ? nua_saved_event_request (priv->saved_event) : NULL;

      nua_respond (priv->nua_op, 200, sip_200_OK,
                   TAG_IF(msg, NUTAG_WITH(msg)),
                   SOATAG_USER_SDP_STR (user_sdp->str),
                   SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
                   SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
                   NUTAG_AUTOANSWER(0),
                   TAG_END());

      if (priv->saved_event[0])
        nua_destroy_event (priv->saved_event);

      g_object_set (session, "state", SIP_MEDIA_SESSION_STATE_ACTIVE, NULL);
    }
  else
    {
      g_warning ("cannot respond with a valid SDP answer, were all streams closed?");

      priv_session_rollback (session);
    }

  g_string_free (user_sdp, TRUE);
}

static gboolean
priv_is_codec_intersect_pending (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->streams->len; i++)
    {
      SIPMediaStream *stream = g_ptr_array_index (priv->streams, i);
      if (stream != NULL
          && sip_media_stream_is_codec_intersect_pending (stream))
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
priv_request_response_step (SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  if (priv->local_non_ready != 0)
    {
      DEBUG("there are local streams not ready, postponed");
      return;
    }

  switch (priv->state)
    {
    case SIP_MEDIA_SESSION_STATE_CREATED:
      /* note:  we need to be prepared to receive media right after the
       *       offer is sent, so we must set state to playing */
      priv_session_media_state (session, TRUE);
      priv_session_invite (session, FALSE);
      break;
    case SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        {
          g_object_set (session,
                        "state", SIP_MEDIA_SESSION_STATE_ACTIVE,
                        NULL);
        }
      break;
    case SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
      /* TODO: if the call has not yet been accepted locally
       * and the remote endpoint supports 100rel, send them
       * an early session answer in a reliable 183 response */
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        {
          priv_session_respond (session);

          /* note: we have accepted the call, set state to playing */ 
          priv_session_media_state (session, TRUE);
        }
      break;
    case SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
      if (!priv_is_codec_intersect_pending (session))
        priv_session_respond (session);
      break;
    case SIP_MEDIA_SESSION_STATE_ACTIVE:
      if (priv->pending_offer)
        priv_session_invite (session, TRUE);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
priv_stream_close_cb (SIPMediaStream *stream,
                      SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;
  guint id;

  DEBUG("enter");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  id = sip_media_stream_get_id (stream);
  g_return_if_fail (g_ptr_array_index(priv->streams, id) == stream);

  if (!sip_media_stream_is_local_ready (stream))
    {
      g_assert (priv->local_non_ready > 0);
      --priv->local_non_ready;
      DEBUG("stream wasn't ready, decrement the local non ready counter to %d", priv->local_non_ready);
    }

  g_object_unref (stream);

  g_ptr_array_index(priv->streams, id) = NULL;

  tp_svc_channel_type_streamed_media_emit_stream_removed (priv->channel, id);
}

static void priv_stream_ready_cb (SIPMediaStream *stream,
				  SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;

  DEBUG ("enter");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  g_assert (priv->local_non_ready > 0);
  --priv->local_non_ready;

  priv_request_response_step (session);
}

static void priv_stream_supported_codecs_cb (SIPMediaStream *stream,
					     guint num_codecs,
					     SIPMediaSession *session)
{
  SIPMediaSessionPrivate *priv;

  g_assert (SIP_IS_MEDIA_SESSION (session));

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (session);

  g_assert (!sip_media_stream_is_codec_intersect_pending (stream));

  if (num_codecs == 0)
    {
      /* This remote media description got no codec intersection. */
      switch (priv->state)
        {
        case SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED:
        case SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED:
          DEBUG("no codec intersection, closing the stream");
          sip_media_stream_close (stream);
          break;
        case SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED:
          /* In this case, we have the stream negotiated already,
           * and we don't want to close it just because the remote party
           * offers a different set of codecs.
           * Roll back the whole session to the previously negotiated state. */
          priv_session_rollback (session);
          return;
        default:
          g_assert_not_reached();
        }
    }

  priv_request_response_step (session);
}

static void
priv_stream_state_changed_cb (SIPMediaStream *stream,
                              guint state,
                              SIPMediaChannel *channel)
{
  g_assert (SIP_IS_MEDIA_CHANNEL (channel));
  tp_svc_channel_type_streamed_media_emit_stream_state_changed(
        channel,
        sip_media_stream_get_id (stream), state);
}

static void
priv_stream_direction_changed_cb (SIPMediaStream *stream,
                                  guint direction,
                                  guint pending_send_flags,
                                  SIPMediaChannel *channel)
{
  g_assert (SIP_IS_MEDIA_CHANNEL (channel));
  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
        channel,
        sip_media_stream_get_id (stream), direction, pending_send_flags);
}

static SIPMediaStream*
priv_create_media_stream (SIPMediaSession *self,
                          guint media_type,
                          guint pending_send_flags)
{
  SIPMediaSessionPrivate *priv;
  gchar *object_path;
  SIPMediaStream *stream = NULL;

  g_assert (SIP_IS_MEDIA_SESSION (self));

  DEBUG ("enter");

  priv = SIP_MEDIA_SESSION_GET_PRIVATE (self);

  if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO ||
      media_type == TP_MEDIA_STREAM_TYPE_VIDEO) {

    object_path = g_strdup_printf ("%s/MediaStream%u",
                                   priv->object_path,
                                   priv->streams->len);

    stream = g_object_new (SIP_TYPE_MEDIA_STREAM,
			   "media-session", self,
			   "media-type", media_type,
			   "object-path", object_path,
			   "id", priv->streams->len,
                           "pending-send-flags", pending_send_flags,
			   NULL);

    g_free (object_path);
 
    g_signal_connect (stream, "close",
                      (GCallback) priv_stream_close_cb,
                      self);
    g_signal_connect (stream, "ready",
		      (GCallback) priv_stream_ready_cb,
		      self);
    g_signal_connect (stream, "supported-codecs",
		      (GCallback) priv_stream_supported_codecs_cb,
		      self);
    g_signal_connect (stream, "state-changed",
                      (GCallback) priv_stream_state_changed_cb,
                      priv->channel);
    g_signal_connect (stream, "direction-changed",
                      (GCallback) priv_stream_direction_changed_cb,
                      priv->channel);

    g_assert (priv->local_non_ready >= 0);
    ++priv->local_non_ready;

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
