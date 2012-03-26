/*
 * rakia-sip-media.c - Source for RakiaSipMedia
 * Copyright (C) 2005-2012 Collabora Ltd.
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

#include "rakia/sip-media.h"

#include <stdlib.h>
#include <string.h>

#include <sofia-sip/sip_status.h>


#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"
#include "rakia/codec-param-formats.h"
#include "rakia/sip-session.h"


#ifdef ENABLE_DEBUG

#define MEDIA_DEBUG(media, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "media %s %p: " format, \
      priv_media_type_to_str ((media)->priv->media_type), (media),  \
      ##__VA_ARGS__)

#define MEDIA_MESSAGE(media, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "media %s %p: " format, \
      priv_media_type_to_str ((media)->priv->media_type), (media), \
      ##__VA_ARGS__)

#else

#define MEDIA_DEBUG(media, format, ...) G_STMT_START { } G_STMT_END
#define MEDIA_MESSAGE(media, format, ...) G_STMT_START { } G_STMT_END

#endif


/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define RAKIA_REINVITE_TIMEOUT 180

G_DEFINE_TYPE(RakiaSipMedia,
    rakia_sip_media,
    G_TYPE_OBJECT)

/* signals */
enum
{
  SIG_LOCAL_NEGOTIATION_COMPLETE,
  SIG_REMOTE_CODEC_OFFER_UPDATED,
  SIG_REMOTE_CANDIDATES_UPDATED,
  SIG_LOCAL_UPDATED,
  SIG_DIRECTION_CHANGED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0 };



/* private structure */
struct _RakiaSipMediaPrivate
{
  TpMediaStreamType media_type;

  RakiaSipSession *session;

  gchar *name;

  GPtrArray *local_codecs;
  GPtrArray *local_candidates;
  gboolean local_candidates_prepared;

  RakiaDirection direction;
  RakiaDirection requested_direction;

  gboolean hold_requested;
  gboolean created_locally;

  const sdp_media_t *remote_media; /* pointer to the SDP media structure
                                    *  owned by the session object */


  gboolean codec_intersect_pending;     /* codec intersection is pending */
  gboolean push_remote_codecs_pending;  /* SetRemoteCodecs emission is pending */
  gboolean push_candidates_on_new_codecs;

  GPtrArray *remote_codec_offer;
  GPtrArray *remote_candidates;

  gboolean can_receive;
};


#define RAKIA_SIP_MEDIA_GET_PRIVATE(media) ((media)->priv)



static void push_remote_candidates (RakiaSipMedia *media);

static void rakia_sip_media_dispose (GObject *object);
static void rakia_sip_media_finalize (GObject *object);


static void
rakia_sip_media_init (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaPrivate);

  self->priv = priv;
}

static void
rakia_sip_media_class_init (RakiaSipMediaClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RakiaSipMediaPrivate));

  object_class->dispose = rakia_sip_media_dispose;
  object_class->finalize = rakia_sip_media_finalize;

  signals[SIG_LOCAL_NEGOTIATION_COMPLETE] =
      g_signal_new ("local-negotiation-complete",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__BOOLEAN,
          G_TYPE_NONE, 1, G_TYPE_BOOLEAN);


  signals[SIG_REMOTE_CODEC_OFFER_UPDATED] =
      g_signal_new ("remote-codec-offer-updated",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__BOOLEAN,
          G_TYPE_NONE, 1, G_TYPE_BOOLEAN);


  signals[SIG_REMOTE_CANDIDATES_UPDATED] =
      g_signal_new ("remote-candidates-updated",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);


  signals[SIG_LOCAL_UPDATED] =
      g_signal_new ("local-updated",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);


  signals[SIG_DIRECTION_CHANGED] =
      g_signal_new ("direction-changed",
          G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__VOID,
          G_TYPE_NONE, 0);
}


static void
rakia_sip_media_dispose (GObject *object)
{
  // RakiaSipMedia *self = RAKIA_SIP_MEDIA (object);

  DEBUG("enter");

  if (G_OBJECT_CLASS (rakia_sip_media_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_sip_media_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
rakia_sip_media_finalize (GObject *object)
{
  RakiaSipMedia *self = RAKIA_SIP_MEDIA (object);
  RakiaSipMediaPrivate *priv = self->priv;

  if (priv->local_candidates)
    g_ptr_array_unref (priv->local_candidates);
  if (priv->local_codecs)
    g_ptr_array_unref (priv->local_codecs);
  if (priv->remote_candidates)
    g_ptr_array_unref (priv->remote_candidates);
  if (priv->remote_codec_offer)
    g_ptr_array_unref (priv->remote_codec_offer);

  g_free (priv->name);

  G_OBJECT_CLASS (rakia_sip_media_parent_class)->finalize (object);

  DEBUG("exit");
}


TpMediaStreamType
rakia_sip_media_get_media_type (RakiaSipMedia *self)
{
  return self->priv->media_type;
}

static const char *
priv_media_type_to_str(TpMediaStreamType media_type)
{
  switch (media_type)
    {
    case TP_MEDIA_STREAM_TYPE_AUDIO:
      return "audio";
    case TP_MEDIA_STREAM_TYPE_VIDEO:
      return "video";
    default:
      g_assert_not_reached ();
      return "-";
    }
}

const gchar *
sip_media_get_media_type_str (RakiaSipMedia *self)
{
  g_return_val_if_fail (RAKIA_IS_SIP_MEDIA (self), "");

  return priv_media_type_to_str (self->priv->media_type);
}


static void
priv_append_rtpmaps (TpMediaStreamType media_type,
    const GPtrArray *codecs, GString *mline, GString *alines)
{
  guint i;

  for (i = 0; i < codecs->len; i++)
    {
      RakiaSipCodec *codec = g_ptr_array_index (codecs, i);

      /* Add rtpmap entry to the a= lines */
      g_string_append_printf (alines,
                              "a=rtpmap:%u %s/%u",
                              codec->id,
                              codec->encoding_name,
                              codec->clock_rate);
      if (codec->channels > 1)
        g_string_append_printf (alines, "/%u", codec->channels);
      g_string_append (alines, "\r\n");

      /* Marshal parameters into the fmtp attribute */
      if (codec->params != NULL)
        {
          GString *fmtp_value;
          g_string_append_printf (alines, "a=fmtp:%u ", codec->id);
          fmtp_value = g_string_new (NULL);
          rakia_codec_param_format (media_type, codec, fmtp_value);
          g_string_append (alines, fmtp_value->str);
          g_string_free (fmtp_value, TRUE);
          g_string_append (alines, "\r\n");
        }

      /* Add PT id to the m= line */
      g_string_append_printf (mline, " %u", codec->id);
    }
}

static void
priv_get_preferred_local_candidates (RakiaSipMedia *media,
    RakiaSipCandidate **rtp_cand, RakiaSipCandidate **rtcp_cand)
{
  RakiaSipMediaPrivate *priv = media->priv;
  guint i;

  g_assert (priv->local_candidates);
  g_assert (priv->local_candidates->len > 0);

  *rtp_cand = NULL;

  for (i = 0; i < priv->local_candidates->len; i++)
    {
      RakiaSipCandidate *tmpcand =
          g_ptr_array_index (priv->local_candidates, i);

      if (tmpcand->component != 1)
        continue;

      if (*rtp_cand == NULL)
        *rtp_cand = tmpcand;
      else if ((*rtp_cand)->priority > tmpcand->priority)
        *rtp_cand = tmpcand;
    }

  g_assert (*rtp_cand != NULL);

  if (rtcp_cand == NULL)
    return;

  *rtcp_cand = NULL;

  for (i = 0; i < priv->local_candidates->len; i++)
    {
      RakiaSipCandidate *tmpcand =
          g_ptr_array_index (priv->local_candidates, i);

      if (tmpcand->component != 2)
        continue;

      if (((*rtp_cand)->foundation == NULL && tmpcand->foundation == NULL) ||
          ((*rtp_cand)->foundation != NULL && tmpcand->foundation != NULL &&
              !strcmp ((*rtp_cand)->foundation, tmpcand->foundation)))
        {
          if (*rtcp_cand == NULL)
            *rtcp_cand = g_ptr_array_index (priv->local_candidates, i);
          else if ((*rtcp_cand)->priority > tmpcand->priority)
            *rtcp_cand = tmpcand;
        }
    }
}


static void
rakia_sip_media_set_direction (RakiaSipMedia *media,
    RakiaDirection direction)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  priv->direction = direction;

  g_signal_emit (media, signals[SIG_DIRECTION_CHANGED], 0);
}


static RakiaDirection
priv_get_sdp_direction (RakiaSipMedia *media, gboolean authoritative)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);
  RakiaDirection direction = priv->requested_direction;

  DEBUG ("req: %s auth: %d remote: %p %s hold: %d",
      rakia_direction_to_string (direction),
      authoritative,
      priv->remote_media,
      rakia_direction_to_string (rakia_sip_media_get_remote_direction (media)),
      priv->hold_requested);

  if (!authoritative && priv->remote_media)
    direction &= rakia_sip_media_get_remote_direction (media);

  /* Don't allow send, only receive if a hold is requested */
  if (priv->hold_requested)
    direction &= RAKIA_DIRECTION_SEND;

  if (!authoritative)
    rakia_sip_media_set_direction (media, direction);

  return direction;
}

/**
 * Produces the SDP description of the media based on Farsight state and
 * current object state.
 *
 * @param media The media object
 * @param signal_update If true, emit the signal "local-media-updated".
 */
void
rakia_sip_media_generate_sdp (RakiaSipMedia *media, GString *out,
    gboolean authoritative)
{
  RakiaSipMediaPrivate *priv = media->priv;
  GString *alines;
  const gchar *dirline;
  RakiaSipCandidate *rtp_cand, *rtcp_cand;

  priv_get_preferred_local_candidates (media, &rtp_cand, &rtcp_cand);

  g_return_if_fail (rtp_cand != NULL);

  g_string_append (out, "m=");
  g_string_append_printf (out,
                          "%s %u RTP/AVP",
                          priv_media_type_to_str (priv->media_type),
                          rtp_cand->port);

  switch (priv_get_sdp_direction (media, authoritative))
    {
    case RAKIA_DIRECTION_BIDIRECTIONAL:
      dirline = "";
      break;
    case RAKIA_DIRECTION_SEND:
      dirline = "a=sendonly\r\n";
      break;
    case RAKIA_DIRECTION_RECEIVE:
      dirline = "a=recvonly\r\n";
      break;
    case RAKIA_DIRECTION_NONE:
      dirline = "a=inactive\r\n";
      break;
    default:
      g_assert_not_reached();
    }

  alines = g_string_new (dirline);

  if (rtcp_cand != NULL)
    {
      /* Add RTCP attribute as per RFC 3605 */
      if (strcmp (rtcp_cand->ip, rtp_cand->ip) != 0)
        {
          g_string_append_printf (alines,
                                  "a=rtcp:%u IN %s %s\r\n",
                                  rtcp_cand->port,
                                  (strchr (rtcp_cand->ip, ':') == NULL)
                                        ? "IP4" : "IP6",
                                  rtcp_cand->ip);
        }
      else if (rtcp_cand->port != rtp_cand->port + 1)
        {
          g_string_append_printf (alines,
                                  "a=rtcp:%u\r\n",
                                  rtcp_cand->port);
        }
    }

  priv_append_rtpmaps (priv->media_type,
      priv->local_codecs,
      out, alines);

  g_string_append_printf (out, "\r\nc=IN %s %s\r\n",
                          (strchr (rtp_cand->ip, ':') == NULL)? "IP4" : "IP6",
                          rtp_cand->ip);

  g_string_append (out, alines->str);

  g_string_free (alines, TRUE);
}


RakiaSipCodec*
rakia_sip_codec_new (guint id, const gchar *encoding_name,
    guint clock_rate, guint channels)
{
  RakiaSipCodec *codec = g_slice_new (RakiaSipCodec);

  codec->id = id;
  codec->encoding_name = g_strdup (encoding_name);
  codec->clock_rate = clock_rate;
  codec->channels = channels;
  codec->params = NULL;

  return codec;
}

static void
rakia_sip_codec_param_free (RakiaSipCodecParam *param)
{
  g_free (param->name);
  g_free (param->value);
  g_slice_free (RakiaSipCodecParam, param);
}

void
rakia_sip_codec_add_param (RakiaSipCodec *codec, const gchar *name,
    const gchar *value)
{
  RakiaSipCodecParam *param;

  if (codec->params == NULL)
    codec->params = g_ptr_array_new_with_free_func (
        (GDestroyNotify) rakia_sip_codec_param_free);

  param = g_slice_new (RakiaSipCodecParam);
  param->name = g_strdup (name);
  param->value = g_strdup (value);
  g_ptr_array_add (codec->params, param);
}

void
rakia_sip_codec_free (RakiaSipCodec *codec)
{
  g_free (codec->encoding_name);
  if (codec->params)
    g_ptr_array_unref (codec->params);
  g_slice_free (RakiaSipCodec, codec);
}


RakiaSipCandidate*
rakia_sip_candidate_new (guint component, const gchar *ip, guint port,
    const gchar *foundation, guint priority)
{
  RakiaSipCandidate *candidate = g_slice_new (RakiaSipCandidate);

  candidate->component = component;
  candidate->ip = g_strdup (ip);
  candidate->port = port;
  candidate->foundation = g_strdup (foundation);
  candidate->priority = priority;

  return candidate;
}


void
rakia_sip_candidate_free (RakiaSipCandidate *candidate)
{
  g_free (candidate->ip);
  g_free (candidate->foundation);
  g_slice_free (RakiaSipCandidate, candidate);
}



RakiaDirection
rakia_direction_from_remote_media (const sdp_media_t *media)
{
  sdp_mode_t mode = media->m_mode;
  return ((mode & sdp_recvonly)? RAKIA_DIRECTION_SEND : 0)
       | ((mode & sdp_sendonly)? RAKIA_DIRECTION_RECEIVE : 0);
}


static gboolean
rakia_sdp_codecs_differ (const sdp_rtpmap_t *m1, const sdp_rtpmap_t *m2)
{
  while (m1 != NULL && m2 != NULL)
    {
      if (sdp_rtpmap_cmp (m1, m2) != 0)
        return TRUE;
      m1 = m1->rm_next;
      m2 = m2->rm_next;
    }
  return m1 != NULL || m2 != NULL;
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


RakiaDirection
rakia_sip_media_get_remote_direction (RakiaSipMedia *media)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  if (priv->remote_media == NULL)
    return RAKIA_DIRECTION_NONE;

  return rakia_direction_from_remote_media (priv->remote_media);
}


static void
priv_update_sending (RakiaSipMedia *media, RakiaDirection send_direction)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);
  RakiaDirection recv_direction;

  /* Only keep the receiving bit from the current direction */
  recv_direction = priv->direction & RAKIA_DIRECTION_RECEIVE;

  /* And only the sending bit from the new direction */
  send_direction &= RAKIA_DIRECTION_SEND;

  rakia_sip_media_set_direction (media, send_direction | recv_direction);
}


void
rakia_sip_media_set_requested_direction (RakiaSipMedia *media,
    RakiaDirection direction)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  if (priv->requested_direction == direction)
    return;

  priv->requested_direction = direction;

  if (priv->requested_direction == priv->direction)
    return;

  rakia_sip_media_local_updated (media);
}

static void push_remote_codecs (RakiaSipMedia *media)
{
  RakiaSipMediaPrivate *priv;
  GPtrArray *codecs;
  const sdp_media_t *sdpmedia;
  const sdp_rtpmap_t *rtpmap;
  gchar *ptime = NULL;
  gchar *max_ptime = NULL;

  DEBUG ("enter");

  priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  sdpmedia = priv->remote_media;
  if (sdpmedia == NULL)
    {
      MEDIA_DEBUG (media, "remote media description is not received yet");
      return;
    }


  ptime = rakia_sdp_get_string_attribute (sdpmedia->m_attributes, "ptime");
  if (ptime == NULL)
    {
      g_object_get (priv->session,
          "remote-ptime", &ptime,
          NULL);
    }
  max_ptime = rakia_sdp_get_string_attribute (sdpmedia->m_attributes, "maxptime");
  if (max_ptime == NULL)
    {
      g_object_get (priv->session,
          "remote-max-ptime", &max_ptime,
          NULL);
    }


  codecs = g_ptr_array_new_with_free_func (
      (GDestroyNotify) rakia_sip_codec_free);

  rtpmap = sdpmedia->m_rtpmaps;
  while (rtpmap)
    {
      RakiaSipCodec *codec;


      codec = rakia_sip_codec_new (rtpmap->rm_pt, rtpmap->rm_encoding,
          rtpmap->rm_rate,
          rtpmap->rm_params ? atoi(rtpmap->rm_params) : 0);


      rakia_codec_param_parse (priv->media_type, codec,
          rtpmap->rm_fmtp);

      if (ptime != NULL)
        rakia_sip_codec_add_param (codec, "ptime", ptime);
      if (max_ptime != NULL)
        rakia_sip_codec_add_param (codec, "maxptime", max_ptime);

      g_ptr_array_add (codecs, codec);

      rtpmap = rtpmap->rm_next;
    }

  g_free (ptime);
  g_free (max_ptime);

  if (priv->remote_codec_offer)
    g_ptr_array_unref (priv->remote_codec_offer);

  priv->remote_codec_offer = codecs;

  g_signal_emit (media, signals[SIG_REMOTE_CODEC_OFFER_UPDATED], 0,
      priv->codec_intersect_pending);

  MEDIA_DEBUG(media, "emitting %d remote codecs to the handler",
      codecs->len);
}


static void push_remote_candidates (RakiaSipMedia *media)
{
  RakiaSipMediaPrivate *priv;
  RakiaSipCandidate *rtp_cand;
  const sdp_media_t *sdp_media;
  const sdp_connection_t *sdp_conn;
  GPtrArray *candidates;
  guint port;

  DEBUG("enter");

  priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  sdp_media = priv->remote_media;
  if (sdp_media == NULL)
    {
      MEDIA_DEBUG (media, "remote media description is not received yet");
      return;
    }


  /* use the address from SDP c-line as the only remote candidate */

  sdp_conn = sdp_media_connections (sdp_media);
  g_return_if_fail (sdp_conn != NULL);

  port = (guint) sdp_media->m_port;


  rtp_cand = rakia_sip_candidate_new (1, sdp_conn->c_address, port, NULL, 0);
  candidates = g_ptr_array_new_with_free_func (
      (GDestroyNotify) rakia_sip_candidate_free);

  g_ptr_array_add (candidates, rtp_cand);


  MEDIA_DEBUG (media, "remote RTP address=<%s>, port=<%u>", sdp_conn->c_address, port);

  if (!rakia_sdp_rtcp_bandwidth_throttled (sdp_media->m_bandwidths))
    {
      gboolean session_rtcp_enabled = TRUE;

      g_object_get (priv->session,
                    "rtcp-enabled", &session_rtcp_enabled,
                    NULL);

      if (session_rtcp_enabled)
        {
          const sdp_attribute_t *rtcp_attr;
          const char *rtcp_address;
          guint rtcp_port;
          RakiaSipCandidate *rtcp_cand;

          /* Get the port and optional address for RTCP accordingly to RFC 3605 */
          rtcp_address = sdp_conn->c_address;
          rtcp_attr = sdp_attribute_find (sdp_media->m_attributes, "rtcp");
          if (rtcp_attr == NULL || rtcp_attr->a_value == NULL)
            {
              rtcp_port = port + 1;
            }
          else
            {
              const char *rest;
              rtcp_port = (guint) g_ascii_strtoull (rtcp_attr->a_value,
                                                    (gchar **) &rest,
                                                    10);
              if (rtcp_port != 0
                  && (strncmp (rest, " IN IP4 ", 8) == 0
                      || strncmp (rest, " IN IP6 ", 8) == 0))
                rtcp_address = rest + 8;
            }


          rtcp_cand = rakia_sip_candidate_new (2, rtcp_address, rtcp_port,
              NULL, 0);
          g_ptr_array_add (candidates, rtcp_cand);
        }
    }

  if (priv->remote_candidates != NULL)
    g_ptr_array_unref (priv->remote_candidates);
  priv->remote_candidates = candidates;

  g_signal_emit (media, signals[SIG_REMOTE_CANDIDATES_UPDATED], 0);
}


/*
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
rakia_sip_media_set_remote_media (RakiaSipMedia *media,
    const sdp_media_t *new_media,
    gboolean authoritative)
{
  RakiaSipMediaPrivate *priv;
  sdp_connection_t *sdp_conn;
  const sdp_media_t *old_media;
  gboolean transport_changed = TRUE;
  gboolean codecs_changed = TRUE;
  guint new_direction;
  RakiaDirection direction_up_mask;

  DEBUG ("enter");

  priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  /* Do sanity checks */

  g_return_val_if_fail (new_media != NULL, FALSE);

  if (new_media->m_rejected || new_media->m_port == 0)
    {
      MEDIA_DEBUG (media, "the media is rejected remotely");
      return FALSE;
    }

  if (new_media->m_proto != sdp_proto_rtp)
    {
      MEDIA_MESSAGE (media, "the remote protocol is not RTP/AVP");
      return FALSE;
    }

  sdp_conn = sdp_media_connections (new_media);
  if (sdp_conn == NULL)
    {
      MEDIA_MESSAGE (media, "no valid remote connections");
      return FALSE;
    }

  if (new_media->m_rtpmaps == NULL)
    {
      MEDIA_MESSAGE (media, "no remote codecs");
      return FALSE;
    }

  /* Note: always update the pointer to the current media structure
   * because of memory management done in the session object */
  old_media = priv->remote_media;
  priv->remote_media = new_media;

  /* Check if there was any media update at all */

  new_direction = rakia_direction_from_remote_media (new_media);


  /*
   * Do not allow:
   * 1) an answer to bump up directions beyond what's been offered;
   * 2) an offer to remove the local hold.
   */
  if (authoritative)
    direction_up_mask = priv->hold_requested ?
        RAKIA_DIRECTION_SEND : RAKIA_DIRECTION_BIDIRECTIONAL;
  else
    direction_up_mask = 0;

  /* Make sure the peer can only enable sending or receiving direction
   * if it's allowed to */
  new_direction &= priv->requested_direction | direction_up_mask;


  if (sdp_media_cmp (old_media, new_media) == 0)
    {
      MEDIA_DEBUG (media, "no media changes detected for the media");
      goto done;
    }

  if (old_media != NULL)
    {
      /* Check if the transport candidate needs to be changed */
      if (!sdp_connection_cmp (sdp_media_connections (old_media), sdp_conn))
        transport_changed = FALSE;

      /* Check if the codec list needs to be updated */
      codecs_changed = rakia_sdp_codecs_differ (old_media->m_rtpmaps,
                                                new_media->m_rtpmaps);

      /* Disable sending at this point if it will be disabled
       * accordingly to the new direction */
      priv_update_sending (media, new_direction & RAKIA_DIRECTION_SEND);
    }

  /* First add the new candidate, then update the codec set.
   * The offerer isn't supposed to send us anything from the new transport
   * until we accept; if it's the answer, both orderings have problems. */

  if (transport_changed)
    {
      /* Make sure we stop sending before we use the new set of codecs
       * intended for the new connection
       * This only applies if we were already sending to somewhere else before.
       */
      if (codecs_changed && old_media)
        {
          priv->push_candidates_on_new_codecs = TRUE;
          if (priv->remote_candidates != NULL)
            {
              g_ptr_array_unref (priv->remote_candidates);
              priv->remote_candidates = NULL;
              g_signal_emit (media, signals[SIG_REMOTE_CANDIDATES_UPDATED], 0);
            }
        }
      else
        {
          push_remote_candidates (media);
        }
    }

  if (codecs_changed)
    {
      if (authoritative)
        priv->codec_intersect_pending = TRUE;

      if (priv->remote_codec_offer == NULL)
        push_remote_codecs (media);
      else
        priv->push_remote_codecs_pending = TRUE;
    }

  /* TODO: this will go to session change commit code */

 done:

  /* Set the final direction */
  rakia_sip_media_set_direction (media, new_direction);

  return TRUE;
}

RakiaDirection
rakia_sip_media_get_requested_direction (RakiaSipMedia *self)
{
  return self->priv->requested_direction;
}


gboolean
rakia_sip_media_is_codec_intersect_pending (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (self);

  return priv->codec_intersect_pending;
}

gboolean
rakia_sip_media_is_ready (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (self);

  MEDIA_DEBUG (self, "is_ready, requested_recv: %d can_recv: %d "
      "local_cand_prep: %d local_codecs: %p local_inter_pending: %d",
      priv->requested_direction & RAKIA_DIRECTION_RECEIVE,
      priv->can_receive,
      self->priv->local_candidates_prepared,
      self->priv->local_codecs,
      priv->codec_intersect_pending);

  if (priv->requested_direction & RAKIA_DIRECTION_RECEIVE &&
      !priv->can_receive &&
      !priv->hold_requested)
    return FALSE;

  return (self->priv->local_candidates_prepared &&
      self->priv->local_codecs &&
      !priv->codec_intersect_pending);
}

void
rakia_sip_media_take_local_codecs (RakiaSipMedia *self, GPtrArray *local_codecs)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (self);

  if (priv->local_codecs)
    g_ptr_array_unref (priv->local_codecs);
  priv->local_codecs = local_codecs;

  if (priv->push_remote_codecs_pending)
    {
      priv->push_remote_codecs_pending = FALSE;
      push_remote_codecs (self);
    }
  else
    {

      if (priv->push_candidates_on_new_codecs)
        {
          /* Push the new candidates now that we have new codecs */
          priv->push_candidates_on_new_codecs = FALSE;
          push_remote_candidates (self);
        }

      if (priv->codec_intersect_pending)
        {

          priv->codec_intersect_pending = FALSE;
          if (rakia_sip_media_is_ready (self))
            {
              g_signal_emit (self, signals[SIG_LOCAL_NEGOTIATION_COMPLETE], 0,
                  TRUE);
              g_ptr_array_unref (priv->remote_codec_offer);
              priv->remote_codec_offer = NULL;
            }
        }
      else
        {
          rakia_sip_media_local_updated (self);
        }
    }
}


void
rakia_sip_media_take_local_candidate (RakiaSipMedia *self,
    RakiaSipCandidate *candidate)
{
  g_return_if_fail (!self->priv->local_candidates_prepared);

  if (self->priv->local_candidates == NULL)
    self->priv->local_candidates = g_ptr_array_new_with_free_func (
        (GDestroyNotify) rakia_sip_candidate_free);

  g_ptr_array_add (self->priv->local_candidates, candidate);
}

gboolean
rakia_sip_media_local_candidates_prepared (RakiaSipMedia *self)
{
  RakiaSipCandidate *rtp_cand = NULL;

  if (self->priv->local_candidates == NULL)
    return FALSE;

  if (self->priv->local_candidates_prepared)
    return FALSE;

  priv_get_preferred_local_candidates (self, &rtp_cand, NULL);

  if (!rtp_cand)
    return FALSE;

  self->priv->local_candidates_prepared = TRUE;

  if (rakia_sip_media_is_ready (self))
    {
      g_signal_emit (self, signals[SIG_LOCAL_NEGOTIATION_COMPLETE], 0,
          TRUE);
    }

  return TRUE;
}

GPtrArray *
rakia_sip_media_get_remote_codec_offer (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (self);

  return priv->remote_codec_offer;
}

GPtrArray *
rakia_sip_media_get_remote_candidates (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (self);

  return priv->remote_candidates;
}

gboolean
rakia_sip_media_is_created_locally (RakiaSipMedia *self)
{
  return self->priv->created_locally;
}

void
rakia_sip_media_local_updated (RakiaSipMedia *self)
{
  g_signal_emit (self, signals[SIG_LOCAL_UPDATED], 0);
}

RakiaSipMedia *
rakia_sip_media_new (RakiaSipSession *session,
    TpMediaStreamType media_type,
    const gchar *name,
    RakiaDirection requested_direction,
    gboolean created_locally,
    gboolean hold_requested)
{
  RakiaSipMedia *self;

  g_return_val_if_fail (media_type == TP_MEDIA_STREAM_TYPE_VIDEO ||
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO, NULL);
  g_return_val_if_fail (requested_direction <= RAKIA_DIRECTION_BIDIRECTIONAL,
      NULL);

  self = g_object_new (RAKIA_TYPE_SIP_MEDIA, NULL);

  self->priv->session = session;
  self->priv->media_type = media_type;
  self->priv->name = g_strdup (name);
  self->priv->requested_direction = requested_direction;
  self->priv->created_locally = created_locally;
  self->priv->hold_requested = hold_requested;

  return self;
}

const gchar *
rakia_sip_media_get_name (RakiaSipMedia *media)
{
  return media->priv->name;
}

RakiaSipSession *
rakia_sip_media_get_session (RakiaSipMedia *media)
{
  return media->priv->session;
}

void
rakia_sip_media_codecs_rejected (RakiaSipMedia *media)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  if (priv->push_remote_codecs_pending)
    {
      priv->push_remote_codecs_pending = FALSE;
      push_remote_codecs (media);
    }
  else
    {
      priv->codec_intersect_pending = FALSE;
      g_signal_emit (media, signals[SIG_LOCAL_NEGOTIATION_COMPLETE], 0, FALSE);
      g_ptr_array_unref (priv->remote_codec_offer);
      priv->remote_codec_offer = NULL;
    }
}

RakiaDirection
rakia_sip_media_get_direction (RakiaSipMedia *media)
{
  return media->priv->direction;
}


void
rakia_sip_media_set_hold_requested (RakiaSipMedia *media,
    gboolean hold_requested)
{
  if (media->priv->hold_requested == hold_requested)
    return;

  media->priv->hold_requested = hold_requested;
}

gboolean
rakia_sip_media_get_hold_requested (RakiaSipMedia *media)
{
  return media->priv->hold_requested;
}


gboolean
rakia_sip_media_is_held (RakiaSipMedia *media)
{
  return !(media->priv->direction & RAKIA_DIRECTION_SEND);
}

void
rakia_sip_media_set_can_receive (RakiaSipMedia *media, gboolean can_receive)
{
  RakiaSipMediaPrivate *priv = RAKIA_SIP_MEDIA_GET_PRIVATE (media);

  if (priv->can_receive == can_receive)
    return;

  priv->can_receive = can_receive;

  if (rakia_sip_media_is_ready (media))
    {
      g_signal_emit (media, signals[SIG_LOCAL_NEGOTIATION_COMPLETE], 0, TRUE);
      if (priv->remote_codec_offer)
        {
          g_ptr_array_unref (priv->remote_codec_offer);
          priv->remote_codec_offer = NULL;
        }
    }
}

gboolean
rakia_sip_media_has_remote_media (RakiaSipMedia *media)
{
  return (media->priv->remote_media != NULL);
}

const gchar *
rakia_direction_to_string (RakiaDirection direction)
{
  switch (direction)
    {
    case RAKIA_DIRECTION_NONE:
      return "none";
    case RAKIA_DIRECTION_SEND:
      return "send";
    case RAKIA_DIRECTION_RECEIVE:
      return "recv";
    case RAKIA_DIRECTION_BIDIRECTIONAL:
      return "bidi";
    default:
      g_warning ("Invalid direction %d", direction);
      return "broken";
    }
}
