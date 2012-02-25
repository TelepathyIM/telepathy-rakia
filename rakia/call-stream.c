/*
 * call-stream.c - RakiaCallStream
 * Copyright (C) 2011 Collabora Ltd.
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

#include "rakia/call-stream.h"

#include "rakia/sip-media.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "debug.h"


static void rakia_call_stream_report_sending_failure (
    TpBaseMediaCallStream *self,
    TpStreamFlowState old_state,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message);
static void rakia_call_stream_report_receiving_failure (
    TpBaseMediaCallStream *self,
    TpStreamFlowState old_state,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message);
static gboolean rakia_call_stream_finish_initial_candidates (
    TpBaseMediaCallStream *stream, GError **error);
static GPtrArray *rakia_call_stream_add_local_candidates (
    TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error);
static gboolean rakia_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error);
static void rakia_call_stream_request_receiving (TpBaseMediaCallStream *stream,
    TpHandle contact, gboolean receive);


static void rakia_call_stream_constructed (GObject *object);
static void rakia_call_stream_dispose (GObject *object);
static void rakia_call_stream_finalize (GObject *object);


static void media_remote_candidates_updated_cb (RakiaSipMedia *media,
    RakiaCallStream *self);
static void media_direction_changed_cb (RakiaSipMedia *media,
    RakiaCallStream *self);
static void receiving_updated_cb (RakiaCallStream *self);


G_DEFINE_TYPE (RakiaCallStream, rakia_call_stream,
    TP_TYPE_BASE_MEDIA_CALL_STREAM)


/* properties */
enum
{
  PROP_CHANNEL = 1,
  PROP_SIP_MEDIA,
  PROP_CAN_REQUEST_RECEIVING,
  LAST_PROPERTY
};

/* private structure */
struct _RakiaCallStreamPrivate
{
  RakiaCallChannel *channel;

  RakiaSipMedia *media;

  TpCallStreamEndpoint *endpoint;

  guint last_endpoint_no;
};

static void
rakia_call_stream_init (RakiaCallStream *self)
{
  RakiaCallStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_STREAM, RakiaCallStreamPrivate);

  self->priv = priv;
}



static void
rakia_call_stream_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (object);
  RakiaCallStreamPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_CHANNEL:
      priv->channel = g_value_get_pointer (value);
      break;
    case PROP_SIP_MEDIA:
      priv->media = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
rakia_call_stream_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (object);
  RakiaCallStreamPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_CAN_REQUEST_RECEIVING:
      {
        gboolean mutable_contents;

        g_object_get (priv->channel,
            "mutable-contents", &mutable_contents,
            NULL);
        g_value_set_boolean (value, mutable_contents);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
rakia_call_stream_class_init (RakiaCallStreamClass *rakia_call_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_stream_class);
  TpBaseMediaCallStreamClass *bmcs_class =
      TP_BASE_MEDIA_CALL_STREAM_CLASS (rakia_call_stream_class);
  GParamSpec *param_spec;

  g_type_class_add_private (rakia_call_stream_class,
    sizeof (RakiaCallStreamPrivate));

  object_class->constructed = rakia_call_stream_constructed;
  object_class->set_property = rakia_call_stream_set_property;
  object_class->get_property = rakia_call_stream_get_property;
  object_class->dispose = rakia_call_stream_dispose;
  object_class->finalize = rakia_call_stream_finalize;

  bmcs_class->report_sending_failure = rakia_call_stream_report_sending_failure;
  bmcs_class->report_receiving_failure =
      rakia_call_stream_report_receiving_failure;
  bmcs_class->add_local_candidates = rakia_call_stream_add_local_candidates;
  bmcs_class->finish_initial_candidates =
      rakia_call_stream_finish_initial_candidates;
  bmcs_class->request_receiving = rakia_call_stream_request_receiving;
  bmcs_class->set_sending = rakia_call_stream_set_sending;

  g_object_class_override_property (object_class, PROP_CAN_REQUEST_RECEIVING,
      "can-request-receiving");

  param_spec = g_param_spec_pointer ("channel", "RakiaCallChannel object",
      "Call Channel",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL, param_spec);

  param_spec = g_param_spec_object ("sip-media", "RakiaSipMedia object",
      "SIP media object that is used for this SIP media channel object.",
      RAKIA_TYPE_SIP_MEDIA,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SIP_MEDIA, param_spec);
}

static void
rakia_call_stream_constructed (GObject *object)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (object);
  RakiaCallStreamPrivate *priv = self->priv;
  TpBaseMediaCallStream *bmcs = TP_BASE_MEDIA_CALL_STREAM (object);
  GPtrArray *stun_array;
  GPtrArray *relay_array;
  gchar *stun_server = NULL;
  guint stun_port = 0;

  g_signal_connect_object (priv->media, "remote-candidates-updated",
      G_CALLBACK (media_remote_candidates_updated_cb), self, 0);
  g_signal_connect_object (priv->media, "direction-changed",
      G_CALLBACK (media_direction_changed_cb), self, 0);

  if (!rakia_sip_media_is_created_locally (priv->media))
    media_remote_candidates_updated_cb (priv->media, self);


  stun_array = g_ptr_array_new_with_free_func (
      (GDestroyNotify) g_value_array_free);

  g_object_get (priv->channel, "stun-server", &stun_server,
      "stun-port", &stun_port, NULL);
  if (stun_server && stun_port)
    {
      g_ptr_array_add (stun_array, tp_value_array_build (2,
              G_TYPE_STRING, stun_server,
              G_TYPE_UINT, stun_port,
              G_TYPE_INVALID));
    }
  g_free (stun_server);
  tp_base_media_call_stream_set_stun_servers (bmcs, stun_array);
  g_ptr_array_unref (stun_array);


  relay_array = g_ptr_array_new ();
  tp_base_media_call_stream_set_relay_info (bmcs, relay_array);
  g_ptr_array_unref (relay_array);

  g_signal_connect (self, "notify::receiving-state",
      G_CALLBACK (receiving_updated_cb), NULL);
  receiving_updated_cb (self);

  G_OBJECT_CLASS (rakia_call_stream_parent_class)->constructed (object);
}



void
rakia_call_stream_dispose (GObject *object)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (object);
  RakiaCallStreamPrivate *priv = self->priv;

  tp_clear_object (&priv->endpoint);
  tp_clear_object (&priv->media);

  G_OBJECT_CLASS (rakia_call_stream_parent_class)->dispose (object);
}

void
rakia_call_stream_finalize (GObject *object)
{
  G_OBJECT_CLASS (rakia_call_stream_parent_class)->finalize (object);
}

static void
rakia_call_stream_report_sending_failure (TpBaseMediaCallStream *self,
    TpStreamFlowState old_state,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message)
{
}

static void
rakia_call_stream_report_receiving_failure (TpBaseMediaCallStream *self,
    TpStreamFlowState old_state,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message)
{
}

static GPtrArray *
rakia_call_stream_add_local_candidates (TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error)
{
  GPtrArray *accepted_candidates = g_ptr_array_sized_new (candidates->len);
  guint i;

  for (i = 0; i < candidates->len; i ++)
    {
      GValueArray *candidate = g_ptr_array_index (candidates, i);
      guint component;
      const gchar *ip;
      guint port;
      GHashTable *info;
      GInetAddress *inetaddr;
      gboolean valid;
      TpMediaStreamBaseProto proto;

      tp_value_array_unpack (candidate, 4, &component, &ip, &port, &info);

      if (component != 1 && component != 2)
        continue;

      if (port > 65535)
        continue;

      proto = tp_asv_get_uint32 (info, "protocol", &valid);
      if (valid && proto != TP_MEDIA_STREAM_BASE_PROTO_UDP)
        continue;

      inetaddr = g_inet_address_new_from_string (ip);
      if (inetaddr == NULL)
        continue;
      g_object_unref (inetaddr);

      g_ptr_array_add (accepted_candidates, candidate);
    }

  if (accepted_candidates->len == 0)
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "No valid candidate passed");
      g_ptr_array_unref (accepted_candidates);
      return NULL;
    }

  return accepted_candidates;
}

static gboolean
rakia_call_stream_finish_initial_candidates (TpBaseMediaCallStream *stream,
    GError **error)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (stream);
  RakiaCallStreamPrivate *priv = self->priv;
  GPtrArray *candidates = tp_base_media_call_stream_get_local_candidates (
      stream);
  guint i;

  for (i = 0; i < candidates->len; i++)
    {
      GValueArray *candidate = g_ptr_array_index (candidates, i);
      guint component;
      gchar *ip;
      guint port;
      GHashTable *info;
      const gchar *foundation;
      guint priority;
      gboolean valid;

      tp_value_array_unpack (candidate, 4, &component, &ip, &port, &info);

      foundation = tp_asv_get_string (info, "foundation");
      if (!foundation)
        foundation = "";

      priority = tp_asv_get_uint32 (info, "priority", &valid);
      if (!valid)
        priority = 0;

      rakia_sip_media_take_local_candidate (priv->media,
          rakia_sip_candidate_new (component, ip, port, foundation, priority));
    }

  if (!rakia_sip_media_local_candidates_prepared (priv->media))
    {
      g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "You need to set a candidate on component 1 first.");
      return FALSE;
    }

  return TRUE;
}

static void rakia_call_stream_request_receiving (
    TpBaseMediaCallStream *stream,
    TpHandle contact,
    gboolean receive)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (stream);
  TpBaseCallStream *bcs = TP_BASE_CALL_STREAM (stream);
  RakiaCallStreamPrivate *priv = self->priv;
  RakiaDirection current_requested_direction =
      rakia_sip_media_get_requested_direction (priv->media);
  RakiaDirection current_direction =
      rakia_sip_media_get_direction (priv->media);
  TpBaseChannel *bchan = TP_BASE_CHANNEL (priv->channel);

  if ((!!(current_requested_direction & RAKIA_DIRECTION_RECEIVE)) == receive)
    return;

  if (receive)
    {
      rakia_sip_media_set_requested_direction (priv->media,
          current_requested_direction | RAKIA_DIRECTION_RECEIVE);

      if (current_direction & RAKIA_DIRECTION_RECEIVE)
           tp_base_call_stream_update_remote_sending_state (bcs,
               tp_base_channel_get_target_handle (bchan),
               TP_SENDING_STATE_SENDING,
               tp_base_channel_get_self_handle (bchan),
               TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED, "",
               "User requested to start receiving");
    }
  else
    {
      rakia_sip_media_set_requested_direction (priv->media,
          current_requested_direction & ~RAKIA_DIRECTION_RECEIVE);

      if (!(current_direction & RAKIA_DIRECTION_RECEIVE))
        tp_base_call_stream_update_remote_sending_state (bcs,
            tp_base_channel_get_target_handle (bchan),
            TP_SENDING_STATE_NONE,
            tp_base_channel_get_self_handle (bchan),
            TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED, "",
            "User requested to stop receiving");
    }
}

static gboolean
rakia_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (stream);
  RakiaCallStreamPrivate *priv = self->priv;
  RakiaDirection current_direction =
      rakia_sip_media_get_requested_direction (priv->media);

  if (!!(current_direction & RAKIA_DIRECTION_SEND) == sending)
    return TRUE;


  if (sending)
    rakia_sip_media_set_requested_direction (priv->media,
        current_direction | RAKIA_DIRECTION_SEND);
  else
    rakia_sip_media_set_requested_direction (priv->media,
        current_direction & ~RAKIA_DIRECTION_SEND);

  return TRUE;
}

static void
media_remote_candidates_updated_cb (RakiaSipMedia *media, RakiaCallStream *self)
{
  RakiaCallStreamPrivate *priv = self->priv;
  TpBaseCallStream *bcs = TP_BASE_CALL_STREAM (self);
  TpBaseMediaCallStream *bmcs = TP_BASE_MEDIA_CALL_STREAM (self);
  GPtrArray *candidates = rakia_sip_media_get_remote_candidates (media);
  TpDBusDaemon *bus = tp_base_connection_get_dbus_daemon (
      tp_base_call_stream_get_connection (bcs));
  gchar *object_path;
  guint i;

  if (priv->endpoint)
    {
      tp_base_media_call_stream_remove_endpoint (bmcs, priv->endpoint);
      tp_clear_object (&priv->endpoint);
    }

  if (candidates == NULL)
    return;

  object_path = g_strdup_printf ("%s/Endpoint%u",
      tp_base_call_stream_get_object_path (bcs),
      ++priv->last_endpoint_no);
  priv->endpoint = tp_call_stream_endpoint_new (bus, object_path,
      TP_STREAM_TRANSPORT_TYPE_RAW_UDP, FALSE);
  g_free (object_path);

  for (i = 0; i < candidates->len; i++)
    {
      RakiaSipCandidate *candidate = g_ptr_array_index (candidates, i);
      GHashTable *info = tp_asv_new (
          "priority", G_TYPE_UINT, candidate->priority,
          "protocol", G_TYPE_UINT, TP_MEDIA_STREAM_BASE_PROTO_UDP,
          NULL);

      tp_call_stream_endpoint_add_new_candidate (priv->endpoint,
          candidate->component, candidate->ip, candidate->port, info);
      g_hash_table_unref (info);
    }

  tp_base_media_call_stream_add_endpoint (bmcs, priv->endpoint);
}

RakiaCallStream *
rakia_call_stream_new (RakiaCallChannel *channel,
    RakiaSipMedia *media,
    const gchar *object_path,
    TpStreamTransportType transport,
    TpBaseConnection *connection,
    TpSendingState local_sending_state)
{
  return g_object_new (RAKIA_TYPE_CALL_STREAM,
      "channel", channel,
      "sip-media", media,
      "object-path", object_path,
      "transport", transport,
      "connection", connection,
      "local-sending-state", local_sending_state,
      NULL);
}

static void
media_direction_changed_cb (RakiaSipMedia *media, RakiaCallStream *self)
{
  TpBaseCallStream *bcs = TP_BASE_CALL_STREAM (self);
  TpBaseMediaCallStream *bmcs = TP_BASE_MEDIA_CALL_STREAM (self);
  RakiaCallStreamPrivate *priv = self->priv;
  TpHandle contact = tp_base_channel_get_target_handle (
      TP_BASE_CHANNEL (priv->channel));
  TpHandle self_handle = tp_base_channel_get_self_handle (
      TP_BASE_CHANNEL (priv->channel));
  RakiaDirection direction = rakia_sip_media_get_direction (media);
  RakiaDirection remote_direction =
      rakia_sip_media_get_remote_direction (media);
  RakiaDirection requested_direction =
      rakia_sip_media_get_requested_direction (media);

  g_debug ("req: %d remote: %d dir: %d",
      requested_direction, remote_direction, direction);

  if (direction & requested_direction & RAKIA_DIRECTION_SEND)
    {
      tp_base_call_stream_update_local_sending_state (bcs,
          TP_SENDING_STATE_SENDING, self_handle,
          TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED, "", "User requested");
      tp_base_media_call_stream_set_local_sending (bmcs, TRUE);
    }
  else if (remote_direction & RAKIA_DIRECTION_SEND)
    {
      if (tp_base_call_stream_get_local_sending_state (bcs) !=
          TP_SENDING_STATE_SENDING)
        tp_base_call_stream_update_local_sending_state (bcs,
            TP_SENDING_STATE_PENDING_SEND, contact,
            TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "",
            "Remote requested that we start sending");
    }
  else
    {
      tp_base_call_stream_update_local_sending_state (bcs,
          TP_SENDING_STATE_NONE, self_handle,
          TP_CALL_STATE_CHANGE_REASON_USER_REQUESTED, "", "User requested");
    }


  if ((direction & RAKIA_DIRECTION_RECEIVE) &&
      (requested_direction & RAKIA_DIRECTION_RECEIVE))
    tp_base_call_stream_update_remote_sending_state (bcs, contact,
        TP_SENDING_STATE_SENDING, 0,
        TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
  else if (!(direction & RAKIA_DIRECTION_RECEIVE) &&
      !(requested_direction & RAKIA_DIRECTION_RECEIVE))
    tp_base_call_stream_update_remote_sending_state (bcs, contact,
        TP_SENDING_STATE_NONE, 0,
        TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");
}

static void
receiving_updated_cb (RakiaCallStream *self)
{
  RakiaCallStreamPrivate *priv = self->priv;
  TpBaseMediaCallStream *bmcs = TP_BASE_MEDIA_CALL_STREAM (self);

  rakia_sip_media_set_can_receive (priv->media,
      tp_base_media_call_stream_get_receiving_state (bmcs) ==
      TP_STREAM_FLOW_STATE_STARTED);
}
