/*
 * call-channel.c - RakiaCallChannel
 * Copyright © 2011 Collabora Ltd.
 * @author Olivier Crete <olivier.crete@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#include "config.h"

#include "rakia/call-channel.h"

#include "rakia/call-content.h"
#include "rakia/sip-session.h"

#define DEBUG_FLAG RAKIA_DEBUG_CALL
#include "rakia/debug.h"

#include <telepathy-glib/exportable-channel.h>


G_DEFINE_TYPE (RakiaCallChannel, rakia_call_channel,
    TP_TYPE_BASE_MEDIA_CALL_CHANNEL)

static void rakia_call_channel_constructed (GObject *obj);
static void rakia_call_channel_set_property (GObject *object,
    guint property_id, const GValue *value, GParamSpec *pspec);
static void rakia_call_channel_get_property (GObject *object,
    guint property_id, GValue *value, GParamSpec *pspec);
static void rakia_call_channel_dispose (GObject *object);
static void rakia_call_channel_finalize (GObject *object);


static void rakia_call_channel_close (TpBaseChannel *base);

static TpBaseCallContent * rakia_call_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error);
static void rakia_call_channel_hangup (
    TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message);
static void rakia_call_channel_set_ringing (TpBaseCallChannel *base);
static void rakia_call_channel_set_queued (TpBaseCallChannel *base);

static void rakia_call_channel_accept (TpBaseMediaCallChannel *channel);
static void rakia_call_channel_hold_state_changed (TpBaseMediaCallChannel *self,
    TpLocalHoldState hold_state, TpLocalHoldStateReason hold_state_reason);

static gboolean rakia_call_channel_is_connected (TpBaseCallChannel *self);

static void ended_cb (RakiaSipSession *session, gboolean self_actor,
    guint status, const gchar *message, RakiaCallChannel *self);
static void ringing_cb (RakiaSipSession *session, RakiaCallChannel *self);
static void queued_cb (RakiaSipSession *session, RakiaCallChannel *self);
static void in_progress_cb (RakiaSipSession *session, RakiaCallChannel *self);
static void media_added_cb (RakiaSipSession *session, RakiaSipMedia *media,
    RakiaCallChannel *self);
static void media_removed_cb (RakiaSipSession *session, RakiaSipMedia *media,
    RakiaCallChannel *self);
static void state_changed_cb (RakiaSipSession *session,
    RakiaSipSessionState old_state, RakiaSipSessionState new_state,
    RakiaCallChannel *self);
static void remote_held_changed_cb (RakiaSipSession *session, GParamSpec *pspec,
    RakiaCallChannel *self);


static RakiaCallContent *rakia_call_channel_get_content_by_media (RakiaCallChannel *self,
    RakiaSipMedia *media);

static void new_content (RakiaCallChannel *self,
    const gchar *name,
    RakiaSipMedia *media,
    TpCallContentDisposition disposition);


/* properties */
enum
{
  PROP_SIP_SESSION = 1,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  LAST_PROPERTY
};


/* private structure */
struct _RakiaCallChannelPrivate
{
  RakiaSipSession *session;

  gchar *stun_server;
  guint stun_port;

  guint last_content_no;

};



static void
rakia_call_channel_init (RakiaCallChannel *self)
{
  RakiaCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_CHANNEL, RakiaCallChannelPrivate);

  self->priv = priv;
}


static gchar *
rakia_call_channel_get_object_path_suffix (TpBaseChannel *base)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);

  return g_strdup_printf ("CallChannel%p", self);
}



static void
rakia_call_channel_class_init (
    RakiaCallChannelClass *rakia_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_channel_class);
  TpBaseChannelClass *base_channel_class =
      TP_BASE_CHANNEL_CLASS (rakia_call_channel_class);
  TpBaseCallChannelClass *base_call_class =
      TP_BASE_CALL_CHANNEL_CLASS (rakia_call_channel_class);
  TpBaseMediaCallChannelClass *base_media_call_class =
      TP_BASE_MEDIA_CALL_CHANNEL_CLASS (rakia_call_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (rakia_call_channel_class,
      sizeof (RakiaCallChannelPrivate));

  object_class->constructed = rakia_call_channel_constructed;
  object_class->get_property = rakia_call_channel_get_property;
  object_class->set_property = rakia_call_channel_set_property;

  object_class->dispose = rakia_call_channel_dispose;
  object_class->finalize = rakia_call_channel_finalize;


  base_channel_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_channel_class->get_object_path_suffix =
      rakia_call_channel_get_object_path_suffix;
  base_channel_class->close = rakia_call_channel_close;

  base_call_class->add_content = rakia_call_channel_add_content;
  base_call_class->hangup = rakia_call_channel_hangup;
  base_call_class->set_ringing = rakia_call_channel_set_ringing;
  base_call_class->set_queued = rakia_call_channel_set_queued;
  base_call_class->is_connected = rakia_call_channel_is_connected;

  base_media_call_class->accept = rakia_call_channel_accept;
  base_media_call_class->hold_state_changed =
      rakia_call_channel_hold_state_changed;

  param_spec = g_param_spec_object ("sip-session", "RakiaSipSession object",
      "SIP session object that is used for this SIP media channel object.",
      RAKIA_TYPE_SIP_SESSION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SIP_SESSION, param_spec);

  param_spec = g_param_spec_string ("stun-server", "STUN server",
      "IP or address of STUN server.", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
      "UDP port of STUN server.", 0, G_MAXUINT16, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);
}


static void
rakia_call_channel_constructed (GObject *obj)
{
  TpBaseChannel *bc = TP_BASE_CHANNEL (obj);
  TpBaseCallChannel *bcc = TP_BASE_CALL_CHANNEL (obj);
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (obj);
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_call_channel_parent_class);
  TpHandle actor;
  TpCallStateChangeReason reason;

  g_signal_connect_object (self->priv->session, "ended",
      G_CALLBACK (ended_cb), self, 0);
  g_signal_connect_object (self->priv->session, "ringing",
      G_CALLBACK (ringing_cb), self, 0);
  g_signal_connect_object (self->priv->session, "queued",
      G_CALLBACK (queued_cb), self, 0);
  g_signal_connect_object (self->priv->session, "in-progress",
      G_CALLBACK (in_progress_cb), self, 0);
  g_signal_connect_object (self->priv->session, "media-added",
      G_CALLBACK (media_added_cb), self, 0);
  g_signal_connect_object (self->priv->session, "media-removed",
      G_CALLBACK (media_removed_cb), self, 0);
  g_signal_connect_object (self->priv->session, "state-changed",
      G_CALLBACK (state_changed_cb), self, 0);
  g_signal_connect_object (self->priv->session, "notify::remote-held",
      G_CALLBACK (remote_held_changed_cb), self, 0);

  if (tp_base_channel_is_requested (bc))
    {
      const gchar *initial_audio_name;
      const gchar *initial_video_name;

      if (tp_base_call_channel_has_initial_audio (bcc, &initial_audio_name))
        rakia_sip_session_add_media (self->priv->session,
            TP_MEDIA_STREAM_TYPE_AUDIO, initial_audio_name,
            RAKIA_DIRECTION_BIDIRECTIONAL, TRUE);

      if (tp_base_call_channel_has_initial_video (bcc, &initial_video_name))
        rakia_sip_session_add_media (self->priv->session,
            TP_MEDIA_STREAM_TYPE_VIDEO, initial_video_name,
            RAKIA_DIRECTION_BIDIRECTIONAL, TRUE);

      actor = tp_base_channel_get_self_handle (bc);
      reason = TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED;
    }
  else
    {
      guint i;
      GPtrArray *medias = rakia_sip_session_get_medias (self->priv->session);

      for (i = 0; i < medias->len; i++)
        {
          RakiaSipMedia *media = g_ptr_array_index (medias, i);
          gchar *name;

          if (media)
            {
              name = g_strdup_printf ("initial_%s_%u",
                  sip_media_get_media_type_str (media), i + 1);
              new_content (self, name, media,
                  TP_CALL_CONTENT_DISPOSITION_INITIAL);
              g_free (name);
            }
        }


      actor = tp_base_channel_get_target_handle (bc);
      reason = TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE;
    }

  tp_base_call_channel_update_member_flags (bcc,
      tp_base_channel_get_target_handle (bc), 0,
      actor, reason, "", "Call Created");

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);
}

static void
rakia_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);
  RakiaCallChannelPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_SIP_SESSION:
      g_value_set_object (value, priv->session);
      break;
    case PROP_STUN_SERVER:
      g_value_set_string (value, priv->stun_server);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, priv->stun_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}



static void
rakia_call_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);
  RakiaCallChannelPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_SIP_SESSION:
      priv->session = g_value_dup_object (value);
      break;
    case PROP_STUN_SERVER:
      priv->stun_server = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      priv->stun_port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
rakia_call_channel_dispose (GObject *object)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);
  RakiaCallChannelPrivate *priv = self->priv;

  DEBUG ("disposing");

  tp_clear_object (&priv->session);

  if (G_OBJECT_CLASS (rakia_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_call_channel_parent_class)->dispose (object);
}

static void
rakia_call_channel_finalize (GObject *object)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);
  RakiaCallChannelPrivate *priv = self->priv;

  g_free (priv->stun_server);

  G_OBJECT_CLASS (rakia_call_channel_parent_class)->finalize (object);
}


static void
rakia_call_channel_close (TpBaseChannel *base)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);
  RakiaCallChannelPrivate *priv = self->priv;

  if (priv->session)
    rakia_sip_session_terminate (priv->session, 480, "Terminated");

  DEBUG ("Closed: %s", tp_base_channel_get_object_path (base));

  TP_BASE_CHANNEL_CLASS (rakia_call_channel_parent_class)->close (base);
}

static TpBaseCallContent *
rakia_call_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    TpMediaStreamDirection initial_direction,
    GError **error)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);
  RakiaCallChannelPrivate *priv = self->priv;
  RakiaCallContent *content;
  RakiaSipMedia *media;

  media = rakia_sip_session_add_media (priv->session,
      type, name, initial_direction, TRUE);

  content = rakia_call_channel_get_content_by_media (self, media);

  return TP_BASE_CALL_CONTENT (content);

}
static void
rakia_call_channel_hangup (
    TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);
  RakiaCallChannelPrivate *priv = self->priv;

  /* FIXME: We need to convert the dbus reason into a SIP status code */

  rakia_sip_session_terminate (priv->session, 480, "Terminated");
}

static void
rakia_call_channel_set_ringing (TpBaseCallChannel *base)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);

  rakia_sip_session_ringing (self->priv->session);
}

static void
rakia_call_channel_set_queued (TpBaseCallChannel *base)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);

  rakia_sip_session_queued (self->priv->session);

}

static void
rakia_call_channel_accept (TpBaseMediaCallChannel *channel)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (channel);

  rakia_sip_session_accept (self->priv->session);
}

static void
rakia_call_channel_hold_state_changed (TpBaseMediaCallChannel *bmcc,
    TpLocalHoldState hold_state,
    TpLocalHoldStateReason hold_state_reason)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (bmcc);

  switch (hold_state)
    {
    case TP_LOCAL_HOLD_STATE_PENDING_HOLD:
    case TP_LOCAL_HOLD_STATE_HELD:
    case TP_LOCAL_HOLD_STATE_PENDING_UNHOLD:
      rakia_sip_session_set_hold_requested (self->priv->session, TRUE);
      break;
    case TP_LOCAL_HOLD_STATE_UNHELD:
      rakia_sip_session_set_hold_requested (self->priv->session, FALSE);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
ended_cb (RakiaSipSession *session, gboolean self_actor, guint status,
    const gchar *message, RakiaCallChannel *self)
{
  TpHandle actor;
  TpCallStateChangeReason reason = TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE;
  const gchar *detailed_reason = "";

  if (self_actor)
    actor = tp_base_channel_get_self_handle (TP_BASE_CHANNEL (self));
  else
    actor = tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self));

  switch (status)
    {
    default:
      reason = TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE;
      break;
    }

  tp_base_call_channel_set_state (TP_BASE_CALL_CHANNEL (self),
      TP_CALL_STATE_ENDED, actor, reason, detailed_reason, message);
}

static void
ringing_cb (RakiaSipSession *session, RakiaCallChannel *self)
{

  tp_base_call_channel_update_member_flags (TP_BASE_CALL_CHANNEL (self),
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self)),
      TP_CALL_MEMBER_FLAG_RINGING,
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self)),
      TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE,
      "", "Remote side has started ringing");
}

static void
queued_cb (RakiaSipSession *session, RakiaCallChannel *self)
{
}

static void
in_progress_cb (RakiaSipSession *session, RakiaCallChannel *self)
{
}

static RakiaCallContent *
rakia_call_channel_get_content_by_media (RakiaCallChannel *self,
    RakiaSipMedia *media)
{
  GList *e;

  for (e = tp_base_call_channel_get_contents (TP_BASE_CALL_CHANNEL (self));
       e != NULL;
       e = e->next)
    {
      RakiaCallContent *content = e->data;
      if (rakia_call_content_get_media (content) == media)
        return content;
    }

  return NULL;
}

static void
new_content (RakiaCallChannel *self,
    const gchar *name,
    RakiaSipMedia *media,
    TpCallContentDisposition disposition)
{
  TpBaseChannel *bchan = TP_BASE_CHANNEL (self);
  RakiaCallContent *content;
  TpMediaStreamType media_type;
  TpHandle creator;
  gchar *object_path;
  gchar *free_name = NULL;
  const gchar *media_type_name;

  switch (rakia_sip_media_get_media_type (media))
    {
    case TP_MEDIA_STREAM_TYPE_AUDIO:
      media_type = TP_MEDIA_STREAM_TYPE_AUDIO;
      media_type_name = "Audio";
      break;
    case TP_MEDIA_STREAM_TYPE_VIDEO:
      media_type = TP_MEDIA_STREAM_TYPE_VIDEO;
      media_type_name = "Video";
      break;
    default:
      g_assert_not_reached ();
    }

  if (rakia_sip_media_is_created_locally (media))
    creator = tp_base_channel_get_self_handle (bchan);
  else
    creator = tp_base_channel_get_target_handle (bchan);

  object_path = g_strdup_printf ("%s/Content%u",
      tp_base_channel_get_object_path (bchan), ++self->priv->last_content_no);

  if (name == NULL)
    name = free_name = g_strdup_printf ("%s %u", media_type_name,
        self->priv->last_content_no);

  content = rakia_call_content_new (self, media, object_path,
      tp_base_channel_get_connection (bchan), name,
      media_type, creator, disposition);

  g_free (free_name);
  g_free (object_path);

  tp_base_call_channel_add_content (TP_BASE_CALL_CHANNEL (self),
      TP_BASE_CALL_CONTENT (content));

  rakia_call_content_add_stream (content);
}

static void
media_added_cb (RakiaSipSession *session, RakiaSipMedia *media,
    RakiaCallChannel *self)
{
  TpCallContentDisposition disposition;
  const gchar *name;

  DEBUG ("Adding media");

  /* Ignore new medias that we have created ourselves */
  g_assert (rakia_call_channel_get_content_by_media (self, media) == NULL);

  switch (rakia_sip_session_get_state (session))
    {
    case RAKIA_SIP_SESSION_STATE_CREATED:
    case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
      disposition = TP_CALL_CONTENT_DISPOSITION_INITIAL;
      break;
    default:
      disposition = TP_CALL_CONTENT_DISPOSITION_NONE;
    }

  name = rakia_sip_media_get_name (media);

  new_content (self, name, media, disposition);
}

static void
media_removed_cb (RakiaSipSession *session, RakiaSipMedia *media,
    RakiaCallChannel *self)
{
  RakiaCallContent *content;

  content = rakia_call_channel_get_content_by_media (self, media);
  if (content == NULL)
    return;

  tp_base_call_channel_remove_content (TP_BASE_CALL_CHANNEL (self),
      TP_BASE_CALL_CONTENT (content),
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (self)),
      TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "Removed by remote side");
}

static void
state_changed_cb (RakiaSipSession *session, RakiaSipSessionState old_state,
    RakiaSipSessionState new_state, RakiaCallChannel *self)
{
  switch (new_state)
    {
    case RAKIA_SIP_SESSION_STATE_INVITE_SENT:
      /* Do nothing.. we don't have a TP state for this */
      break;

    case RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED:
      /* This will never be received here because this is what
       * triggers RakiaMediaManager to create a Channel in the first place
       */
      break;

    case RAKIA_SIP_SESSION_STATE_ACTIVE:

      if (tp_base_channel_is_requested (TP_BASE_CHANNEL (self)))
        tp_base_call_channel_remote_accept (TP_BASE_CALL_CHANNEL (self));
      break;

    case RAKIA_SIP_SESSION_STATE_ENDED:
      /* the ended callback is used to get more information */
      break;
    default:
      break;
    }
}

static void
remote_held_changed_cb (RakiaSipSession *session, GParamSpec *pspec,
    RakiaCallChannel *self)
{
  TpBaseChannel *bchan = TP_BASE_CHANNEL (self);
  TpBaseCallChannel *bcc = TP_BASE_CALL_CHANNEL (self);
  gboolean remote_held;
  GHashTable *members;
  TpCallMemberFlags member_flags;
  TpHandle remote_contact = tp_base_channel_get_target_handle (bchan);

  g_object_get (session, "remote-held", &remote_held, NULL);

  members = tp_base_call_channel_get_call_members (bcc);

  member_flags = GPOINTER_TO_UINT (g_hash_table_lookup (members,
          GUINT_TO_POINTER (remote_contact)));

  if (!!(member_flags & TP_CALL_MEMBER_FLAG_HELD) == remote_held)
    return;

  if (remote_held)
    member_flags |= TP_CALL_MEMBER_FLAG_HELD;
  else
    member_flags &= ~TP_CALL_MEMBER_FLAG_HELD;

  tp_base_call_channel_update_member_flags (bcc, remote_contact, member_flags,
      remote_contact, TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "",
      remote_held ? "Held by remote side" : "Unheld by remote side");
}

static gboolean
rakia_call_channel_is_connected (TpBaseCallChannel *self)
{
  /* We don't support ICE, so we don'T have the concept of connected-ness
   * yet.
   */
  return TRUE;
}

void
rakia_call_channel_hangup_error (RakiaCallChannel *self,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message)
{
  TpHandle self_handle = tp_base_channel_get_self_handle (
      TP_BASE_CHANNEL (self));

  rakia_call_channel_hangup (TP_BASE_CALL_CHANNEL (self),
      reason, dbus_reason, message);

  tp_base_call_channel_set_state (TP_BASE_CALL_CHANNEL (self),
      TP_CALL_STATE_ENDED, self_handle, reason, dbus_reason, message);
}
