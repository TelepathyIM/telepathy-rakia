/*
 * call-content.c - RakiaCallContent
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

#include "rakia/call-content.h"

#include "rakia/call-stream.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "debug.h"


static void rakia_call_content_constructed (GObject *object);
static void rakia_call_content_dispose (GObject *object);
static void rakia_call_content_deinit (TpBaseCallContent *base);

static void media_remote_codecs_updated_cb (RakiaSipMedia *media,
    gboolean is_offer, RakiaCallContent *self);
static void local_media_description_updated (RakiaCallContent *self,
    TpHandle contact, GHashTable *properties, gpointer user_data);

static void md_offer_cb (GObject *obj, GAsyncResult *res, gpointer user_data);


G_DEFINE_TYPE (RakiaCallContent, rakia_call_content,
    TP_TYPE_BASE_MEDIA_CALL_CONTENT);


/* properties */
enum
{
  PROP_CHANNEL = 1,
  PROP_SIP_MEDIA,
  LAST_PROPERTY
};


/* private structure */
struct _RakiaCallContentPrivate
{
  RakiaCallChannel *channel;

  RakiaSipMedia *media;

  RakiaCallStream *stream;

  guint codec_offer_id;
};

static void
rakia_call_content_init (RakiaCallContent *self)
{
  RakiaCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_CONTENT, RakiaCallContentPrivate);

  self->priv = priv;
}

static void
rakia_call_content_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (object);
  RakiaCallContentPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_CHANNEL:
      priv->channel = g_value_dup_object (value);
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
rakia_call_content_class_init (
    RakiaCallContentClass *rakia_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_content_class);
  TpBaseCallContentClass *bcc_class =
      TP_BASE_CALL_CONTENT_CLASS (rakia_call_content_class);
  GParamSpec *param_spec;

  g_type_class_add_private (rakia_call_content_class,
      sizeof (RakiaCallContentPrivate));

  object_class->constructed = rakia_call_content_constructed;
  object_class->dispose = rakia_call_content_dispose;
  object_class->set_property = rakia_call_content_set_property;

  bcc_class->deinit = rakia_call_content_deinit;

  param_spec = g_param_spec_object ("channel", "RakiaCallChannel object",
      "Call Channel.",
      RAKIA_TYPE_CALL_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CHANNEL, param_spec);

  param_spec = g_param_spec_object ("sip-media", "RakiaSipMedia object",
      "SIP media object that is used for this SIP media channel object.",
      RAKIA_TYPE_SIP_MEDIA,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SIP_MEDIA, param_spec);
}

static void
rakia_call_content_constructed (GObject *object)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (object);
  RakiaCallContentPrivate *priv = self->priv;
  TpBaseChannel *bchan = TP_BASE_CHANNEL (priv->channel);
  TpBaseCallContent *bcc = TP_BASE_CALL_CONTENT (self);
  TpBaseMediaCallContent *bmcc = TP_BASE_MEDIA_CALL_CONTENT (self);
  TpHandle creator;
  gchar *object_path;

  g_signal_connect_object (priv->media, "remote-codec-offer-updated",
      G_CALLBACK (media_remote_codecs_updated_cb), self, 0);

  g_signal_connect (self, "local-media-description-updated",
      G_CALLBACK (local_media_description_updated), NULL);

  g_object_get (object, "creator", &creator, NULL);

  if (creator == tp_base_channel_get_self_handle (bchan))
    {
      TpCallContentMediaDescription *md;
      TpDBusDaemon *bus = tp_base_connection_get_dbus_daemon (
          tp_base_call_content_get_connection (bcc));

      object_path = g_strdup_printf ("%s/InitialOffer",
          tp_base_call_content_get_object_path (bcc));

      md = tp_call_content_media_description_new (bus, object_path,
          tp_base_channel_get_target_handle (bchan), FALSE, TRUE);
      g_free (object_path);

      tp_base_media_call_content_offer_media_description_async (bmcc,
          md, md_offer_cb, GUINT_TO_POINTER (TRUE));
    }
  else
    {
      media_remote_codecs_updated_cb (priv->media, TRUE, self);
    }

  G_OBJECT_CLASS (rakia_call_content_parent_class)->constructed (object);
}

static void
rakia_call_content_dispose (GObject *object)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (object);
  RakiaCallContentPrivate *priv = self->priv;

  tp_clear_object (&priv->media);

  if (G_OBJECT_CLASS (rakia_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_call_content_parent_class)->dispose (object);
}

static void
rakia_call_content_deinit (TpBaseCallContent *base)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (base);
  RakiaCallContentPrivate *priv = self->priv;
  RakiaSipSession *session;

  session = rakia_sip_media_get_session (priv->media);

  /* If the media was removed, it means it's by user request, so we must
   * do a re-invite
   */
  if (rakia_sip_session_remove_media (session, priv->media, 0, NULL))
    rakia_sip_session_media_changed (session);


  tp_clear_object (&priv->stream);
  tp_clear_object (&priv->channel);

  TP_BASE_CALL_CONTENT_CLASS (
    rakia_call_content_parent_class)->deinit (base);
}

RakiaCallContent *
rakia_call_content_new (RakiaCallChannel *channel,
    RakiaSipMedia *media,
    const gchar *object_path,
    TpBaseConnection *connection,
    const gchar *name,
    TpMediaStreamType media_type,
    TpHandle creator,
    TpCallContentDisposition disposition)
{
  RakiaCallContent *content;

  content = g_object_new (RAKIA_TYPE_CALL_CONTENT,
      "channel", channel,
      "sip-media", media,
      "object-path", object_path,
      "connection", connection,
      "name", name,
      "media-type", media_type,
      "creator", creator,
      "disposition", disposition,
      "packetization", TP_CALL_CONTENT_PACKETIZATION_TYPE_RTP,
      NULL);

  return content;
}

RakiaSipMedia *
rakia_call_content_get_media (RakiaCallContent *self)
{
  return self->priv->media;
}

static void
set_telepathy_codecs (RakiaCallContent *self, GHashTable *md_properties)
{
  RakiaCallContentPrivate *priv = self->priv;
  guint i;
  GPtrArray *tpcodecs = tp_asv_get_boxed (md_properties,
      TP_PROP_CALL_CONTENT_MEDIA_DESCRIPTION_CODECS,
      TP_ARRAY_TYPE_CODEC_LIST);
  GPtrArray *sipcodecs = g_ptr_array_new_with_free_func (
      (GDestroyNotify) rakia_sip_codec_free);

  for (i = 0; i < tpcodecs->len; i++)
    {
      GValueArray *tpcodec = g_ptr_array_index (tpcodecs, i);
      guint id;
      const gchar *name;
      guint clock_rate;
      guint channels;
      gboolean updated;
      GHashTable *extra_params;
      RakiaSipCodec *sipcodec;
      GHashTableIter iter;
      gpointer key, value;

      tp_value_array_unpack (tpcodec, 6, &id, &name, &clock_rate,
          &channels, &updated, &extra_params);

      sipcodec = rakia_sip_codec_new (id, name, clock_rate, channels);

      g_hash_table_iter_init (&iter, extra_params);

      while (g_hash_table_iter_next (&iter, &key, &value))
        rakia_sip_codec_add_param (sipcodec, key, value);

      g_ptr_array_add (sipcodecs, sipcodec);
    }

  rakia_sip_media_take_local_codecs (priv->media, sipcodecs);
}

static void
md_offer_cb (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (obj);
  RakiaCallContentPrivate *priv = self->priv;
  TpBaseMediaCallContent *bmcc = TP_BASE_MEDIA_CALL_CONTENT (self);
  GError *error = NULL;
  gboolean is_initial_offer = GPOINTER_TO_UINT (user_data);

  if (tp_base_media_call_content_offer_media_description_finish (bmcc,
          res, &error))
    {
      GHashTable *local_md =
          tp_base_media_call_content_get_local_media_description (bmcc,
              tp_base_channel_get_target_handle (TP_BASE_CHANNEL (priv->channel)));

      set_telepathy_codecs (self, local_md);
    }
  else
    {
      g_assert (!is_initial_offer);

      rakia_sip_media_codecs_rejected (priv->media);

      DEBUG ("Codecs rejected: %s", error->message);

      /* FIXME: We need to allow for partial failures */
      g_clear_error (&error);
    }
}

static void
media_remote_codecs_updated_cb (RakiaSipMedia *media, gboolean is_offer,
    RakiaCallContent *self)
{
  TpBaseCallContent *bcc = TP_BASE_CALL_CONTENT (self);
  TpBaseMediaCallContent *bmcc = TP_BASE_MEDIA_CALL_CONTENT (self);
  RakiaCallContentPrivate *priv = self->priv;
  GPtrArray *remote_codecs =
      rakia_sip_media_get_remote_codec_offer (priv->media);
  TpCallContentMediaDescription *md;
  TpDBusDaemon *bus = tp_base_connection_get_dbus_daemon (
      tp_base_call_content_get_connection (bcc));
  gchar *object_path;
  guint i, j;

  if (remote_codecs == NULL)
    return;

  object_path = g_strdup_printf ("%s/Offer%u",
      tp_base_call_content_get_object_path (bcc), ++priv->codec_offer_id);

  md = tp_call_content_media_description_new (bus, object_path,
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (priv->channel)),
      TRUE, is_offer);

  g_free (object_path);

  for (i = 0; i < remote_codecs->len; i++)
    {
      RakiaSipCodec *codec = g_ptr_array_index (remote_codecs, i);
      GHashTable *parameters = g_hash_table_new (g_str_hash, g_str_equal);

      /* No need to copy the values as .._append_codec() will */
      if (codec->params)
        for (j = 0; j < codec->params->len; j++)
          {
            RakiaSipCodecParam *param = g_ptr_array_index (codec->params, j);

            g_hash_table_insert (parameters, param->name, param->value);
          }

      tp_call_content_media_description_append_codec (md, codec->id,
          codec->encoding_name, codec->clock_rate, codec->channels, TRUE,
          parameters);

      g_hash_table_unref (parameters);
    }

  tp_base_media_call_content_offer_media_description_async (bmcc,
      md, md_offer_cb, GUINT_TO_POINTER (FALSE));

  g_object_unref (md);
}

void
rakia_call_content_add_stream (RakiaCallContent *self)
{
  RakiaCallContentPrivate *priv = self->priv;
  TpBaseCallContent *bcc = TP_BASE_CALL_CONTENT (self);
  TpHandle creator;
  gchar *object_path;
  TpSendingState local_sending_state;
  TpSendingState remote_sending_state;

  g_object_get (self, "creator", &creator, NULL);

  if (rakia_sip_media_get_requested_direction (priv->media) &
      RAKIA_DIRECTION_SEND)
    {
      if (!tp_base_call_channel_is_accepted (
              TP_BASE_CALL_CHANNEL (priv->channel)) &&
          !tp_base_channel_is_requested (TP_BASE_CHANNEL (priv->channel)))
        local_sending_state = TP_SENDING_STATE_PENDING_SEND;
      else
        local_sending_state = TP_SENDING_STATE_SENDING;
    }
  else
    {
      local_sending_state = TP_SENDING_STATE_NONE;
    }


  if (rakia_sip_media_get_requested_direction (priv->media) &
      RAKIA_DIRECTION_RECEIVE)
    remote_sending_state = TP_SENDING_STATE_PENDING_SEND;
  else
    remote_sending_state = TP_SENDING_STATE_NONE;

  object_path = g_strdup_printf ("%s/Stream",
      tp_base_call_content_get_object_path (bcc));
  priv->stream = rakia_call_stream_new (priv->channel, priv->media,
      object_path, TP_STREAM_TRANSPORT_TYPE_RAW_UDP,
      tp_base_call_content_get_connection (bcc),
      local_sending_state);
  g_free (object_path);

  tp_base_call_stream_update_remote_sending_state (
      TP_BASE_CALL_STREAM (priv->stream),
      tp_base_channel_get_target_handle (TP_BASE_CHANNEL (priv->channel)),
      remote_sending_state, creator,
      TP_CALL_STATE_CHANGE_REASON_PROGRESS_MADE, "", "");

  tp_base_call_content_add_stream (bcc, TP_BASE_CALL_STREAM (priv->stream));

  tp_base_media_call_stream_update_receiving_state (
      TP_BASE_MEDIA_CALL_STREAM (priv->stream));
}

static void
local_media_description_updated (RakiaCallContent *self, TpHandle contact,
    GHashTable *properties, gpointer user_data)
{
  set_telepathy_codecs (self, properties);
}
