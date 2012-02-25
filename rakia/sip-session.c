/*
 * rakia-sip-session.c - Source for RakiaSipSession
 * Copyright (C) 2005-2011 Collabora Ltd.
 *   @author Olivier Crete <olivier.crete@collabora.com>
 * Copyright (C) 2005-2010 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation
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

#include "rakia/sip-session.h"

#include <string.h>

#include <sofia-sip/sip_status.h>



#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"
#include "rakia/base-connection.h"
#include "rakia/event-target.h"
#include "rakia/sip-media.h"


/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define RAKIA_REINVITE_TIMEOUT 180

static void event_target_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(RakiaSipSession,
    rakia_sip_session,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (RAKIA_TYPE_EVENT_TARGET, event_target_init)
)



#ifdef ENABLE_DEBUG

/**
 * Sip session states:
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
static const char *const session_states[NUM_RAKIA_SIP_SESSION_STATES] =
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

#define SESSION_DEBUG(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "%s [%-17s]: " format, \
      G_STRFUNC, session_states[(session)->priv->state],##__VA_ARGS__)

#define SESSION_MESSAGE(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "%s [%-17s]: " format, \
      G_STRFUNC, session_states[(session)->priv->state],##__VA_ARGS__)

#else /* !ENABLE_DEBUG */

#define SESSION_DEBUG(session, format, ...) G_STMT_START { } G_STMT_END
#define SESSION_MESSAGE(session, format, ...) G_STMT_START { } G_STMT_END

#endif /* ENABLE_DEBUG */


/* properties */
enum
{
  PROP_REMOTE_PTIME = 1,
  PROP_REMOTE_MAX_PTIME,
  PROP_RTCP_ENABLED,
  PROP_HOLD_STATE,
  PROP_REMOTE_HELD,
  LAST_PROPERTY
};


/* signals */
enum
{
  SIG_ENDED,
  SIG_RINGING,
  SIG_QUEUED,
  SIG_IN_PROGRESS,
  SIG_INCOMING_CALL,
  SIG_MEDIA_ADDED,
  SIG_MEDIA_REMOVED,
  SIG_STATE_CHANGED,
  SIG_START_RECEIVING,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0 };


/* private structure */
struct _RakiaSipSessionPrivate
{
  nua_handle_t *nua_op;                   /* see gobj. prop. 'nua-handle' */
  RakiaSipSessionState state;           /* session state */

  gboolean immutable_streams;            /* immutable streams */

  GPtrArray *medias;

  gboolean incoming;                     /* Is this an incoming call ? (or outgoing */
  RakiaBaseConnection *conn;

  nua_saved_event_t saved_event[1];       /* Saved incoming request event */
  TpLocalHoldState hold_state;         /* local hold state aggregated from stream directions */
  gboolean hold_requested;                /* if the local hold has been requested by the user */
  gchar *remote_ptime;                    /* see gobj. prop. 'remote-ptime' */
  gchar *remote_max_ptime;                /* see gobj. prop. 'remote-max-ptime' */
  guint remote_media_count;              /* number of m= last seen in a remote offer */
  gboolean rtcp_enabled;                  /* see gobj. prop. 'rtcp-enabled' */
  gchar *local_sdp;                       /* local session as SDP string */
  su_home_t *home;                        /* Sofia memory home for remote SDP session structure */
  su_home_t *backup_home;                 /* Sofia memory home for previous generation remote SDP session*/
  sdp_session_t *remote_sdp;              /* last received remote session */
  sdp_session_t *backup_remote_sdp;       /* previous remote session */

  gboolean accepted;                      /*< session has been locally accepted for use */

  gboolean pending_offer;                 /*< local media have been changed, but a re-INVITE is pending */
  guint glare_timer_id;
  gboolean remote_held;
};


#define RAKIA_SIP_SESSION_GET_PRIVATE(session) ((session)->priv)



static void rakia_sip_session_dispose (GObject *object);
static void rakia_sip_session_finalize (GObject *object);

static TpMediaStreamType rakia_media_type (sdp_media_e sip_mtype);


static void priv_session_invite (RakiaSipSession *session, gboolean reinvite);
static gboolean priv_update_remote_media (RakiaSipSession *self,
    gboolean authoritative);
static void priv_request_response_step (RakiaSipSession *session);

static void
event_target_init(gpointer g_iface, gpointer iface_data)
{
}

static void
null_safe_unref (gpointer data)
{
  if (data)
    g_object_unref (data);
}

static void
rakia_sip_session_init (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_SIP_SESSION, RakiaSipSessionPrivate);

  self->priv = priv;

  priv->state = RAKIA_SIP_SESSION_STATE_CREATED;
  priv->rtcp_enabled = TRUE;

  /* allocate any data required by the object here */
  priv->medias = g_ptr_array_new_with_free_func (null_safe_unref);
}

static void rakia_sip_session_get_property (GObject    *object,
					    guint       property_id,
					    GValue     *value,
					    GParamSpec *pspec)
{
  RakiaSipSession *session = RAKIA_SIP_SESSION (object);
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);

  switch (property_id)
    {
    case PROP_REMOTE_PTIME:
      g_value_set_string (value, priv->remote_ptime);
      break;
    case PROP_REMOTE_MAX_PTIME:
      g_value_set_string (value, priv->remote_max_ptime);
      break;
    case PROP_RTCP_ENABLED:
      g_value_set_boolean (value, priv->rtcp_enabled);
      break;
    case PROP_HOLD_STATE:
      g_value_set_uint (value, priv->hold_state);
      break;
    case PROP_REMOTE_HELD:
      g_value_set_boolean (value, priv->remote_held);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
rakia_sip_session_class_init (RakiaSipSessionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (RakiaSipSessionPrivate));

  object_class->get_property = rakia_sip_session_get_property;
  object_class->dispose = rakia_sip_session_dispose;
  object_class->finalize = rakia_sip_session_finalize;

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

  param_spec = g_param_spec_boolean ("rtcp-enabled", "RTCP enabled",
      "Is RTCP enabled session-wide",
      TRUE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RTCP_ENABLED, param_spec);


  param_spec = g_param_spec_uint ("hold-state", "Local Hold State",
      "Is the call held or not",
      0, NUM_TP_LOCAL_HOLD_STATES - 1,
      TP_LOCAL_HOLD_STATE_UNHELD,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_HOLD_STATE, param_spec);


  param_spec = g_param_spec_boolean ("remote-held", "Remote Held",
      "Are we remotely held",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REMOTE_HELD, param_spec);



  signals[SIG_ENDED] =
      g_signal_new ("ended",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 3, G_TYPE_BOOLEAN, G_TYPE_UINT, G_TYPE_STRING);

  signals[SIG_RINGING] =
      g_signal_new ("ringing",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 0);

  signals[SIG_QUEUED] =
      g_signal_new ("queued",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 0);

  signals[SIG_IN_PROGRESS] =
      g_signal_new ("in-progress",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 0);

  signals[SIG_INCOMING_CALL] =
      g_signal_new ("incoming-call",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 0);

  signals[SIG_MEDIA_ADDED] =
      g_signal_new ("media-added",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 1, RAKIA_TYPE_SIP_MEDIA);

  signals[SIG_MEDIA_REMOVED] =
      g_signal_new ("media-removed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 1, RAKIA_TYPE_SIP_MEDIA);

  signals[SIG_STATE_CHANGED] =
      g_signal_new ("state-changed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[SIG_START_RECEIVING] =
      g_signal_new ("start-receiving",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 0);
}


static void
rakia_sip_session_dispose (GObject *object)
{
  RakiaSipSession *self = RAKIA_SIP_SESSION (object);

  SESSION_DEBUG (self, "enter");

  if (self->priv->medias)
    {
      g_ptr_array_unref (self->priv->medias);
      self->priv->medias = NULL;
    }

  if (self->priv->glare_timer_id)
    {
      g_source_remove (self->priv->glare_timer_id);
      self->priv->glare_timer_id = 0;
    }

  tp_clear_object (&self->priv->conn);

  if (self->priv->remote_sdp != NULL)
    {
      self->priv->remote_sdp = NULL;
      g_assert (self->priv->home != NULL);
      su_home_unref (self->priv->home);
      self->priv->home = NULL;
    }

  if (self->priv->backup_remote_sdp != NULL)
    {
      self->priv->backup_remote_sdp = NULL;
      g_assert (self->priv->backup_home != NULL);
      su_home_unref (self->priv->backup_home);
      self->priv->backup_home = NULL;
    }

  if (G_OBJECT_CLASS (rakia_sip_session_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_sip_session_parent_class)->dispose (object);

  SESSION_DEBUG (self, "exit");
}

static void
rakia_sip_session_finalize (GObject *object)
{
  RakiaSipSession *self = RAKIA_SIP_SESSION (object);
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  g_free (priv->local_sdp);

  G_OBJECT_CLASS (rakia_sip_session_parent_class)->finalize (object);

  DEBUG("exit");
}

void
rakia_sip_session_change_state (RakiaSipSession *self,
                                RakiaSipSessionState new_state)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  guint old_state;

  if (priv->state == new_state)
    return;

  SESSION_DEBUG (self, "changing state to %s", session_states[new_state]);

  old_state = priv->state;
  priv->state = new_state;

  switch (new_state)
    {
    case RAKIA_SIP_SESSION_STATE_CREATED:
    case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
    case RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED:
    case RAKIA_SIP_SESSION_STATE_INVITE_SENT:
    case RAKIA_SIP_SESSION_STATE_REINVITE_SENT:
    case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
    case RAKIA_SIP_SESSION_STATE_REINVITE_PENDING:
    case RAKIA_SIP_SESSION_STATE_ACTIVE:
      break;
    case RAKIA_SIP_SESSION_STATE_ENDED:
      SESSION_DEBUG (self, "destroying the NUA handle %p", priv->nua_op);
      if (priv->nua_op != NULL)
        {
          nua_handle_destroy (priv->nua_op);
          priv->nua_op = NULL;
        }
      break;
    case NUM_RAKIA_SIP_SESSION_STATES:
      g_assert_not_reached();

      /* Don't add default because we want to be warned by the compiler
       * about unhandled states */
    }

  g_signal_emit (self, signals[SIG_STATE_CHANGED], 0, old_state, new_state);

  if (new_state == RAKIA_SIP_SESSION_STATE_ACTIVE && priv->pending_offer)
    priv_session_invite (self, TRUE);
}


static void
priv_zap_event (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  if (priv->saved_event[0])
    {
      nua_event_data_t const *ev_data = nua_event_data (priv->saved_event);
      g_assert (ev_data != NULL);
      WARNING ("zapping unhandled saved event '%s'", nua_event_name (ev_data->e_event));
      nua_destroy_event (priv->saved_event);
    }
}


static void
priv_save_event (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  priv_zap_event (self);

  rakia_base_connection_save_event (priv->conn, priv->saved_event);

#ifdef ENABLE_DEBUG
  {
    nua_event_data_t const *ev_data = nua_event_data (priv->saved_event);
    g_assert (ev_data != NULL);
    DEBUG("saved the last event: %s %hd %s", nua_event_name (ev_data->e_event), ev_data->e_status, ev_data->e_phrase);
  }
#endif
}

static void
rakia_sip_session_receive_reinvite (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  /* Check for permitted state transitions */
  switch (priv->state)
    {
    case RAKIA_SIP_SESSION_STATE_ACTIVE:
    case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
      break;
    case RAKIA_SIP_SESSION_STATE_REINVITE_PENDING:
      g_source_remove (priv->glare_timer_id);
      break;
    default:
      g_return_if_reached ();
    }

  priv_save_event (self);

  rakia_sip_session_change_state (self,
      RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED);
}


static gboolean
priv_nua_i_invite_cb (RakiaSipSession *self,
                      const RakiaNuaEvent  *ev,
                      tagi_t             tags[],
                      gpointer           foo)
{
  /* nua_i_invite delivered for a bound handle means a re-INVITE */

  rakia_sip_session_receive_reinvite (self);

  return TRUE;
}



static gboolean
priv_nua_i_bye_cb (RakiaSipSession *self,
                   const RakiaNuaEvent  *ev,
                   tagi_t             tags[],
                   gpointer           foo)
{
  g_signal_emit (self, signals[SIG_ENDED], 0, FALSE, 0, "");

  return TRUE;
}


static gboolean
priv_nua_i_cancel_cb (RakiaSipSession *self,
                      const RakiaNuaEvent  *ev,
                      tagi_t             tags[],
                      gpointer           foo)
{
  const sip_reason_t *reason;
  guint cause = 0;
  const gchar *message = NULL;
  gboolean self_actor = FALSE;

  /* FIXME: implement cancellation of an incoming re-INVITE, if ever
   * found in real usage and not caused by a request timeout */

  if (ev->sip != NULL)
    for (reason = ev->sip->sip_reason;
         reason != NULL;
         reason = reason->re_next)
      {
        const char *protocol = reason->re_protocol;
        if (protocol == NULL || strcmp (protocol, "SIP") != 0)
          continue;
        if (reason->re_cause != NULL)
          {
            cause = (guint) g_ascii_strtoull (reason->re_cause, NULL, 10);
            message = reason->re_text;
            break;
          }
      }

  switch (cause)
    {
    case 200:
    case 603:
      /* The user must have acted on another branch of the forked call */
      self_actor = TRUE;
      break;
    default:
      self_actor = FALSE;
    }


  if (message == NULL || !g_utf8_validate (message, -1, NULL))
    message = "";

  g_signal_emit (self, signals[SIG_ENDED], 0, self_actor, cause, message);


  return TRUE;
}

void
rakia_sip_session_ringing (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  g_return_if_fail (priv->state == RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED);

  nua_respond (priv->nua_op, SIP_180_RINGING, TAG_END());
}


void
rakia_sip_session_queued (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  g_return_if_fail (priv->state == RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED);

  nua_respond (priv->nua_op, SIP_182_QUEUED, TAG_END());
}

static void
rakia_sip_session_receive_invite (RakiaSipSession *self)
{
  g_return_if_fail (self->priv->nua_op != NULL);

  /* We'll do Ringing later instead */
  /* nua_respond (priv->nua_op, SIP_183_SESSION_PROGRESS, TAG_END()); */

  rakia_sip_session_change_state (self,
      RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED);
}


/*
 * Handles an incoming call, called shortly after the channel
 * has been created with initiator handle of the sender, when remote SDP
 * session data are reported by the NUA stack.
 */
static void
rakia_sip_session_handle_incoming_call (RakiaSipSession *self,
    nua_handle_t *nh,
    const sdp_session_t *sdp)
{
  g_assert (self->priv->incoming);

  rakia_sip_session_receive_invite (self);

  /* Tell the factory to emit NewChannel(s) */
  g_signal_emit (self, signals[SIG_INCOMING_CALL], 0);
}


static void
rakia_sip_session_peer_error (RakiaSipSession *self,
    guint status,
    const char* message)
{
  if (message == NULL || !g_utf8_validate (message, -1, NULL))
    message = "";

  g_signal_emit (self, signals[SIG_ENDED], 0, FALSE, status, message);
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


static gboolean
rakia_sip_session_supports_media_type (TpMediaStreamType media_type)
{
  switch (media_type)
    {
    case TP_MEDIA_STREAM_TYPE_AUDIO:
    case TP_MEDIA_STREAM_TYPE_VIDEO:
      return TRUE;
    default:
      return FALSE;
    }
}


static void
priv_session_rollback (RakiaSipSession *session)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);

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
      rakia_sip_session_terminate (session, 0, NULL);
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

  rakia_sip_session_change_state (session, RAKIA_SIP_SESSION_STATE_ACTIVE);
}


static void
priv_media_local_negotiation_complete_cb (RakiaSipMedia *media,
    gboolean success, RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "negotiation complete %d", success);

  if (!success)
    {
     /* This remote media description got no codec intersection. */
      switch (priv->state)
        {
        case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
        case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
          SESSION_DEBUG (self, "no codec intersection, closing the stream");
          rakia_sip_session_remove_media (self, media, 488,
              "No codec intersection");
          break;
        case RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED:
          /* In this case, we have the stream negotiated already,
           * and we don't want to close it just because the remote party
           * offers a different set of codecs.
           * Roll back the whole session to the previously negotiated state. */
          priv_session_rollback (self);
          return;
        case RAKIA_SIP_SESSION_STATE_ACTIVE:
          /* We've most likely rolled back from
           * RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED,
           * but we may receive more than one empty codec intersection
           * in the session, so we ignore the rest */
          return;
        default:
          g_assert_not_reached();
        }
    }

  priv_request_response_step (self);
}

static gboolean
priv_has_all_media_ready (RakiaSipSession *self)
{
  guint i;

  for (i = 0; i < self->priv->medias->len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (self->priv->medias, i);;;;

      if (media == NULL)
        continue;
      if (!rakia_sip_media_is_ready (media))
        return FALSE;
    }

  return TRUE;
}

void
rakia_sip_session_media_changed (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "media changed");

  switch (priv->state)
    {
    case RAKIA_SIP_SESSION_STATE_CREATED:
      /* If all medias are ready, send an offer now */
      priv_request_response_step (self);
      break;
    case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
    case RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED:
      /* The changes to existing medias will be included in the
       * eventual answer (FIXME: implement postponed direction changes,
       * which are applied after the remote offer has been processed).
       * Check, however, if there are new medias not present in the
       * remote offer, that will need another offer-answer round */
      if (priv->remote_media_count < priv->medias->len)
        priv->pending_offer = TRUE;
      break;
    case RAKIA_SIP_SESSION_STATE_INVITE_SENT:
    case RAKIA_SIP_SESSION_STATE_REINVITE_SENT:
    case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
      /* Cannot send another offer right now */
      priv->pending_offer = TRUE;
      break;
    case RAKIA_SIP_SESSION_STATE_ACTIVE:
      /* Check if we are allowed to send re-INVITES */
      if (priv->immutable_streams) {
        SESSION_MESSAGE (self, "sending of a local media update disabled"
            " by parameter 'immutable-streams'");
        break;
      }
      /* Fall through to the next case */
    case RAKIA_SIP_SESSION_STATE_REINVITE_PENDING:
      if (priv_has_all_media_ready (self))
        priv_session_invite (self, TRUE);
      else
        priv->pending_offer = TRUE;
      break;
    case RAKIA_SIP_SESSION_STATE_ENDED:
      /* We've already ended the call, ignore any change request */
      break;
    default:
      g_assert_not_reached();
    }
}


RakiaSipMedia*
rakia_sip_session_add_media (RakiaSipSession *self,
    TpMediaStreamType media_type,
    const gchar *name,
    RakiaDirection direction,
    gboolean created_locally)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  RakiaSipMedia *media = NULL;

  SESSION_DEBUG (self, "enter");

  if (rakia_sip_session_supports_media_type (media_type)) {
    media = rakia_sip_media_new (self, media_type, name, direction,
        created_locally, priv->hold_requested);

    g_signal_connect_object (media, "local-negotiation-complete",
        G_CALLBACK (priv_media_local_negotiation_complete_cb), self, 0);
    g_signal_connect_object (media, "local-updated",
        G_CALLBACK (rakia_sip_session_media_changed), self,
        G_CONNECT_SWAPPED);

    g_signal_emit (self, signals[SIG_MEDIA_ADDED], 0, media);
  }

  /* note: we add an entry even for unsupported media types */
  g_ptr_array_add (priv->medias, media);

  SESSION_DEBUG (self, "exit");

  return media;
}


static void
priv_update_remote_hold (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  RakiaSipMedia *media;
  gboolean has_medias = FALSE;
  gboolean remote_held = TRUE;
  guint i;

  /* The call is remotely unheld if there's at least one sending media */
  for (i = 0; i < priv->medias->len; i++)
    {
      media = g_ptr_array_index(priv->medias, i);
      if (media != NULL)
        {
          if (rakia_sip_media_get_direction (media) & RAKIA_DIRECTION_SEND ||
              !(rakia_sip_media_get_requested_direction (media) &
                  RAKIA_DIRECTION_SEND))
            remote_held = FALSE;

          has_medias = TRUE;
        }
    }

  if (!has_medias)
    return;

  SESSION_DEBUG (self, "is remotely %s", remote_held? "held" : "unheld");

  priv->remote_held = remote_held;
  g_object_notify (G_OBJECT (self), "remote-held");
}


static gboolean
priv_update_remote_media (RakiaSipSession *self, gboolean authoritative)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  const sdp_session_t *sdp = priv->remote_sdp;
  const sdp_media_t *sdp_media;
  gboolean has_supported_media = FALSE;
  guint i;

  g_return_val_if_fail (sdp != NULL, FALSE);

  /* Update the session-wide parameters
   * before updating medias */

  priv->remote_ptime     = rakia_sdp_get_string_attribute (
      sdp->sdp_attributes, "ptime");
  priv->remote_max_ptime = rakia_sdp_get_string_attribute (
      sdp->sdp_attributes, "maxptime");

  priv->rtcp_enabled = !rakia_sdp_rtcp_bandwidth_throttled (
      sdp->sdp_bandwidths);


  /* A remote media requesting to enable sending would need local approval.
   * Also, if there have been any local media updates pending a re-INVITE,
   * keep or bump the pending remote send flag on the medias: it will
   * be resolved in the next re-INVITE transaction */
#if 0
  pending_send_mask = TP_MEDIA_STREAM_PENDING_LOCAL_SEND;
  if (priv->pending_offer)
    pending_send_mask |= TP_MEDIA_STREAM_PENDING_REMOTE_SEND;
#endif

  sdp_media = sdp->sdp_media;

  /* note: for each session, we maintain an ordered list of
   *       medias (SDP m-lines) which are matched 1:1 to
   *       the medias of the remote SDP */

  for (i = 0; sdp_media != NULL; sdp_media = sdp_media->m_next, i++)
    {
      RakiaSipMedia *media = NULL;
      TpMediaStreamType media_type;

      media_type = rakia_media_type (sdp_media->m_type);

      if (i >= priv->medias->len)
        media = rakia_sip_session_add_media (
                        self,
                        media_type,
                        NULL,
                        /* Don't start sending unless requested by the user */
                        rakia_direction_from_remote_media (sdp_media),
                        FALSE);
      else
        media = g_ptr_array_index(priv->medias, i);

      /* note: it is ok for the media to be NULL (unsupported media type) */
      if (media == NULL)
        continue;

      SESSION_DEBUG (self, "setting remote SDP for media %u", i);

      if (sdp_media->m_rejected)
        {
          SESSION_DEBUG (self, "the media has been rejected, closing");
        }
      else if (rakia_sip_media_get_media_type (media) != media_type)
        {
          /* XXX: close this media and create a new one in its place? */
          WARNING ("The peer has changed the media type, don't know what to do");
        }
      else if (rakia_sip_media_set_remote_media (media, sdp_media,
              authoritative))
        {
          has_supported_media = TRUE;
          continue;
        }

      /* There have been problems with the media update, kill the media */
      rakia_sip_session_remove_media (self, media, 488,
          "Can not process this media type");
    }
  g_assert(sdp_media == NULL);
  g_assert(i <= priv->medias->len);
  g_assert(!authoritative || i == priv->remote_media_count);

  if (i < priv->medias->len && !priv->pending_offer)
    {
      /*
       * It's not defined what we should do if there are previously offered
       * medias not accounted in the remote SDP, in violation of RFC 3264.
       * Closing them off serves resource preservation and gives better
       * clue to the client as to the real state of the session.
       * Note that this situation is masked if any local media updates
       * have been requested and are pending until the present remote session
       * answer is received and applied. In such a case, we'll issue a new offer
       * at the closest available time, with the "overhanging" media entries
       * intact.
       */
      do
        {
          RakiaSipMedia *media;
          media = g_ptr_array_index(priv->medias, i);
          if (media != NULL)
            {
              SESSION_MESSAGE (self, "removing a mismatched media %u", i);
              rakia_sip_session_remove_media (self, media,
                  488, "Media type mismatch");
            }
        }
      while (++i < priv->medias->len);
    }

  if (has_supported_media)
    priv_update_remote_hold (self);

  DEBUG("exit");

  return has_supported_media;
}



static GString *
priv_session_generate_sdp (RakiaSipSession *session,
                           gboolean authoritative)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  GString *user_sdp;
  guint len;
  guint i;

  g_return_val_if_fail (priv_has_all_media_ready (session), NULL);

  user_sdp = g_string_new ("v=0\r\n");

  len = priv->medias->len;
  if (!authoritative && len > priv->remote_media_count)
    {
      len = priv->remote_media_count;
      SESSION_DEBUG (session, "clamped response to %u medias seen in the offer", len);
    }

  for (i = 0; i < len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (priv->medias, i);
      if (media)
        rakia_sip_media_generate_sdp (media, user_sdp, authoritative);
      else
        g_string_append (user_sdp, "m=audio 0 RTP/AVP 0\r\n");
    }

  return user_sdp;
}

static void
priv_session_invite (RakiaSipSession *session, gboolean reinvite)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  GString *user_sdp;

  DEBUG("enter");

  g_return_if_fail (priv->nua_op != NULL);

  user_sdp = priv_session_generate_sdp (session, TRUE);

  g_return_if_fail (user_sdp != NULL);

  if (!reinvite
      || priv->state == RAKIA_SIP_SESSION_STATE_REINVITE_PENDING
      || tp_strdiff (priv->local_sdp, user_sdp->str))
    {
      g_free (priv->local_sdp);
      priv->local_sdp = g_string_free (user_sdp, FALSE);

      /* We need to be prepared to receive media right after the
       * offer is sent, so we must set the streams to playing */
      g_signal_emit (session, signals[SIG_START_RECEIVING], 0);

      nua_invite (priv->nua_op,
                  SOATAG_USER_SDP_STR(priv->local_sdp),
                  SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
                  SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
                  SOATAG_ORDERED_USER(1),
                  NUTAG_AUTOANSWER(0),
                  TAG_IF(reinvite,
                         NUTAG_INVITE_TIMER (RAKIA_REINVITE_TIMEOUT)),
                  TAG_END());
      priv->pending_offer = FALSE;

      rakia_sip_session_change_state (
                session,
                reinvite? RAKIA_SIP_SESSION_STATE_REINVITE_SENT
                        : RAKIA_SIP_SESSION_STATE_INVITE_SENT);
    }
  else
    {
      SESSION_DEBUG (session, "SDP unchanged, not sending a re-INVITE");
      g_string_free (user_sdp, TRUE);
    }
}

static void
priv_session_respond (RakiaSipSession *session)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  msg_t *msg;

  g_return_if_fail (priv->nua_op != NULL);

  {
    GString *user_sdp = priv_session_generate_sdp (session, FALSE);

    g_free (priv->local_sdp);
    priv->local_sdp = g_string_free (user_sdp, FALSE);
  }

  /* We need to be prepared to receive media right after the
   * answer is sent, so we must set the streams to playing */
  g_signal_emit (session, signals[SIG_START_RECEIVING], 0);

  msg = (priv->saved_event[0])
            ? nua_saved_event_request (priv->saved_event) : NULL;

  nua_respond (priv->nua_op, SIP_200_OK,
               TAG_IF(msg, NUTAG_WITH(msg)),
               SOATAG_USER_SDP_STR(priv->local_sdp),
               SOATAG_RTP_SORT(SOA_RTP_SORT_REMOTE),
               SOATAG_RTP_SELECT(SOA_RTP_SELECT_ALL),
               NUTAG_AUTOANSWER(0),
               TAG_END());

  if (priv->saved_event[0])
    nua_destroy_event (priv->saved_event);

  rakia_sip_session_change_state (session, RAKIA_SIP_SESSION_STATE_ACTIVE);
}



static gboolean
priv_is_codec_intersect_pending (RakiaSipSession *session)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->medias->len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (priv->medias, i);
      if (media != NULL
          && rakia_sip_media_is_codec_intersect_pending (media))
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
priv_request_response_step (RakiaSipSession *session)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);

  if (!priv_has_all_media_ready (session))
    {
      SESSION_DEBUG (session, "there are local streams not ready, postponed");
      return;
    }

  switch (priv->state)
    {
    case RAKIA_SIP_SESSION_STATE_CREATED:
      priv_session_invite (session, FALSE);
      break;
    case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        rakia_sip_session_change_state (session,
                                        RAKIA_SIP_SESSION_STATE_ACTIVE);
      break;
    case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
      /* TODO: if the call has not yet been accepted locally
       * and the remote endpoint supports 100rel, send them
       * an early session answer in a reliable 183 response */
      if (priv->accepted
          && !priv_is_codec_intersect_pending (session))
        priv_session_respond (session);
      break;
    case RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED:
      if (!priv_is_codec_intersect_pending (session))
        priv_session_respond (session);
      break;
    case RAKIA_SIP_SESSION_STATE_ACTIVE:
    case RAKIA_SIP_SESSION_STATE_REINVITE_PENDING:
      if (priv->pending_offer)
        priv_session_invite (session, TRUE);
      break;
    default:
      SESSION_DEBUG (session, "no action taken in the current state");
    }
}


static gboolean
rakia_sip_session_set_remote_media (RakiaSipSession *self,
                                    const sdp_session_t* sdp)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  gboolean authoritative;

  SESSION_DEBUG (self, "enter");

  if (priv->state == RAKIA_SIP_SESSION_STATE_INVITE_SENT
      || priv->state == RAKIA_SIP_SESSION_STATE_REINVITE_SENT)
    {
      rakia_sip_session_change_state (
                self,
                RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED);
    }
  else
    {
      /* Remember the m= line count in the remote offer,
       * to match it with exactly this number of answer lines */
      sdp_media_t *media;
      guint count = 0;

      for (media = sdp->sdp_media; media != NULL; media = media->m_next)
        ++count;

      priv->remote_media_count = count;
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
   * The medias still need the old media descriptions */
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

  authoritative = (priv->state == RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED
                || priv->state == RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED);
  if (!priv_update_remote_media (self, authoritative))
    return FALSE;

finally:
  /* Make sure to always transition states and send out the response,
   * even if no stream-engine roundtrips were initiated */
  priv_request_response_step (self);
  return TRUE;
}




static gboolean
priv_glare_retry (gpointer session)
{
  RakiaSipSession *self = session;
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "glare resolution interval is over");

  if (priv->state == RAKIA_SIP_SESSION_STATE_REINVITE_PENDING)
    priv_session_invite (self, TRUE);

  /* Reap the timer */
  priv->glare_timer_id = 0;
  return FALSE;
}

static void
rakia_sip_session_resolve_glare (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  guint interval;

  if (priv->state != RAKIA_SIP_SESSION_STATE_REINVITE_SENT)
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
  else if (priv->incoming)
    interval = g_random_int_range (0, 200) * 10;
  else
    interval = g_random_int_range (210, 400) * 10;

  if (priv->glare_timer_id != 0)
    g_source_remove (priv->glare_timer_id);

  priv->glare_timer_id = g_timeout_add (interval, priv_glare_retry, self);

  SESSION_DEBUG (self, "glare resolution interval %u msec", interval);

  rakia_sip_session_change_state (
        self, RAKIA_SIP_SESSION_STATE_REINVITE_PENDING);
}




static gboolean
priv_nua_i_state_cb (RakiaSipSession *self,
                     const RakiaNuaEvent  *ev,
                     tagi_t             tags[],
                     gpointer           foo)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  const sdp_session_t *r_sdp = NULL;
  int offer_recv = 0;
  int answer_recv = 0;
  int ss_state = nua_callstate_init;
  gint status = ev->status;

  tl_gets(tags,
          NUTAG_CALLSTATE_REF(ss_state),
          NUTAG_OFFER_RECV_REF(offer_recv),
          NUTAG_ANSWER_RECV_REF(answer_recv),
          SOATAG_REMOTE_SDP_REF(r_sdp),
          TAG_END());

  DEBUG("call with handle %p is %s", ev->nua_handle,
      nua_callstate_name (ss_state));

  if (r_sdp)
    {
      g_return_val_if_fail (answer_recv || offer_recv, FALSE);
      if (!rakia_sip_session_set_remote_media (self, r_sdp))
        {
          rakia_sip_session_terminate (self, 0, NULL);
          return TRUE;
        }
    }


  if (ss_state == nua_callstate_received &&
      priv->state == RAKIA_SIP_SESSION_STATE_CREATED)
    {
      /* Let's announce the new call now that the initial streams have
       * been created
       */
      rakia_sip_session_handle_incoming_call (self, ev->nua_handle, r_sdp);
    }

  switch ((enum nua_callstate)ss_state)
    {
    case nua_callstate_proceeding:
      switch (status)
        {
          case 180:
            g_signal_emit (self, signals[SIG_RINGING], 0);
            break;
          case 182:
            g_signal_emit (self, signals[SIG_QUEUED], 0);
            break;
          case 183:
            g_signal_emit (self, signals[SIG_IN_PROGRESS], 0);
            break;
        }
      break;

    case nua_callstate_completing:
      /* In auto-ack mode, we don't need to call nua_ack(), see NUTAG_AUTOACK() */
      break;

    case nua_callstate_ready:

      /* FIXME: Clear any pre-establishment call states */
      /* This are queued/ringing/in-progress */

      if (status < 300)
        {
          rakia_sip_session_accept (self);
        }
      else if (status == 491)
        rakia_sip_session_resolve_glare (self);
      else
        {
          /* Was something wrong with our re-INVITE? We can't cope anyway. */
          MESSAGE ("can't handle non-fatal response %d %s", status, ev->text);
          rakia_sip_session_terminate (self, 480, "Re-invite rejected");
        }
      break;

    case nua_callstate_terminated:
      /* In cases of self-inflicted termination,
       * we should have already gone through the moves */
      if (priv->state == RAKIA_SIP_SESSION_STATE_ENDED)
        break;

      if (status >= 300)
        {
          rakia_sip_session_peer_error (self, status, ev->text);
        }

      rakia_sip_session_change_state (self,
          RAKIA_SIP_SESSION_STATE_ENDED);
      break;

    default:
      break;
  }

  return TRUE;
}


static void
rakia_sip_session_attach_to_nua_handle (RakiaSipSession *self,
    nua_handle_t *nh, RakiaBaseConnection *conn)
{
  rakia_event_target_attach (nh, (GObject *) self);

  /* have the connection handle authentication, before all other
   * response callbacks */
  rakia_base_connection_add_auth_handler (conn, RAKIA_EVENT_TARGET (self));

  g_signal_connect (self,
                    "nua-event::nua_i_invite",
                    G_CALLBACK (priv_nua_i_invite_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_bye",
                    G_CALLBACK (priv_nua_i_bye_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_cancel",
                    G_CALLBACK (priv_nua_i_cancel_cb),
                    NULL);
  g_signal_connect (self,
                    "nua-event::nua_i_state",
                    G_CALLBACK (priv_nua_i_state_cb),
                    NULL);

}


RakiaSipSession *
rakia_sip_session_new (nua_handle_t *nh, RakiaBaseConnection *conn,
    gboolean incoming, gboolean immutable_streams)
{
  RakiaSipSession *self = g_object_new (RAKIA_TYPE_SIP_SESSION, NULL);

  self->priv->incoming = incoming;
  self->priv->nua_op = nh;
  self->priv->conn = g_object_ref (conn);
  self->priv->immutable_streams = immutable_streams;
  nua_handle_ref (self->priv->nua_op);
  rakia_sip_session_attach_to_nua_handle (self, nh, conn);

  return self;
}


/**
 * Converts a sofia-sip media type enum to Telepathy media type.
 * See <sofia-sip/sdp.h> and <telepathy-constants.h>.
 *
 * @return G_MAXUINT if the media type cannot be mapped
 */
static TpMediaStreamType
rakia_media_type (sdp_media_e sip_mtype)
{
  switch (sip_mtype)
    {
    case sdp_media_audio:
      return TP_MEDIA_STREAM_TYPE_AUDIO;
    case sdp_media_video:
      return TP_MEDIA_STREAM_TYPE_VIDEO;
    default:
      /* some invalid value */
      return G_MAXINT;
    }
}

gboolean
rakia_sip_session_remove_media (RakiaSipSession *self, RakiaSipMedia *media,
    guint status, const gchar *reason)
{
  guint i;
  gboolean has_removed_media = FALSE;
  gboolean has_media = TRUE;

  g_return_val_if_fail (RAKIA_IS_SIP_MEDIA (media), FALSE);

  for (i = 0; i < self->priv->medias->len; i++)
    {
      if (media == g_ptr_array_index (self->priv->medias, i))
        {
          g_object_ref (media);
          g_ptr_array_index (self->priv->medias, i) = NULL;
          g_signal_emit (self, signals[SIG_MEDIA_REMOVED], 0, media);
          g_object_unref (media);
          has_removed_media = TRUE;
        }
      else if (g_ptr_array_index (self->priv->medias, i) != NULL)
        has_media = TRUE;
    }

  if (!has_media)
    rakia_sip_session_terminate (self, status, reason);

  return has_removed_media;
}


gboolean
rakia_sip_session_has_media (RakiaSipSession *self,
    TpMediaStreamType media_type)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  guint i;

  for (i = 0; i < priv->medias->len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (priv->medias, i);
      if (media == NULL)
        continue;
      if (rakia_sip_media_get_media_type (media) == media_type)
        return TRUE;
    }

  return FALSE;
}


void
rakia_sip_session_respond (RakiaSipSession *self,
                           gint status,
                           const char *message)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  SESSION_DEBUG (self, "responding: %03d %s", status, message ? message : "");

  if (message != NULL && !message[0])
    message = NULL;

  if (priv->nua_op)
    nua_respond (priv->nua_op, status, message, TAG_END());
}


void
rakia_sip_session_accept (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  if (priv->accepted)
    return;

  SESSION_DEBUG (self, "accepting the session");

  priv->accepted = TRUE;

  /* Will change session state to active when streams are ready */
  priv_request_response_step (self);
}

gboolean
rakia_sip_session_is_accepted (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);
  return priv->accepted;
}


void
rakia_sip_session_terminate (RakiaSipSession *session, guint status,
    const gchar *reason)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);

  DEBUG ("enter");

  if (priv->state == RAKIA_SIP_SESSION_STATE_ENDED)
    return;

  if (status == 0)
    {
      status = SIP_480_TEMPORARILY_UNAVAILABLE;
      reason = "Terminated";
    }

  if (priv->nua_op != NULL)
    {
      /* XXX: should the stack do pretty much the same
       * (except freeing the saved event) upon nua_handle_destroy()? */
      switch (priv->state)
        {
        case RAKIA_SIP_SESSION_STATE_ACTIVE:
        case RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED:
        case RAKIA_SIP_SESSION_STATE_REINVITE_SENT:
        case RAKIA_SIP_SESSION_STATE_REINVITE_PENDING:
          SESSION_DEBUG (session, "sending BYE");
          nua_bye (priv->nua_op, TAG_END());
          break;
        case RAKIA_SIP_SESSION_STATE_INVITE_SENT:
          SESSION_DEBUG (session, "sending CANCEL");
          nua_cancel (priv->nua_op, TAG_END());
          break;
        case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
          SESSION_DEBUG (session, "sending the %d response to an incoming INVITE", status);
          nua_respond (priv->nua_op, status, reason, TAG_END());
          break;
        case RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED:
          if (priv->saved_event[0])
            {
              SESSION_DEBUG (session, "sending the %d response to an incoming re-INVITE", status);
              nua_respond (priv->nua_op, status, reason,
                           NUTAG_WITH(nua_saved_event_request (priv->saved_event)),
                           TAG_END());
              nua_destroy_event (priv->saved_event);
            }
          SESSION_DEBUG (session, "sending BYE to terminate the call itself");
          nua_bye (priv->nua_op, TAG_END());
          break;
        default:
          /* let the Sofia stack decide what do to */;
        }
    }

  rakia_sip_session_change_state (session, RAKIA_SIP_SESSION_STATE_ENDED);
}

gboolean
rakia_sip_session_pending_offer (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (self);

  return priv->pending_offer;
}

RakiaSipSessionState
rakia_sip_session_get_state (RakiaSipSession *session)
{
  return session->priv->state;
}

gboolean
rakia_sip_session_is_held (RakiaSipSession *session)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  guint i;

  for (i = 0; i < priv->medias->len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (priv->medias, i);

      if (!media)
        continue;

      if (!rakia_sip_media_is_held (media))
        return FALSE;
    }

  return TRUE;
}

void
rakia_sip_session_set_hold_requested (RakiaSipSession *session,
    gboolean hold_requested)
{
  RakiaSipSessionPrivate *priv = RAKIA_SIP_SESSION_GET_PRIVATE (session);
  guint i;

  if (session->priv->hold_requested == hold_requested)
    return;

  SESSION_DEBUG (session, "set hold: %d", hold_requested);

  session->priv->hold_requested = hold_requested;

  for (i = 0; i < priv->medias->len; i++)
    {
      RakiaSipMedia *media = g_ptr_array_index (priv->medias, i);

      if (media == NULL)
        continue;

      rakia_sip_media_set_hold_requested (media, hold_requested);
    }

  rakia_sip_session_media_changed (session);

}

GPtrArray *
rakia_sip_session_get_medias (RakiaSipSession *self)
{
  return self->priv->medias;
}
