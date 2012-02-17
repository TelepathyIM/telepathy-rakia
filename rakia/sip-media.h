/*
 * rakia-sip-media.h - Header for RakiaSipMedia
 * Copyright (C) 2005-2012 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
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

#ifndef __RAKIA_SIP_MEDIA_H__
#define __RAKIA_SIP_MEDIA_H__

#include <glib-object.h>
#include <sofia-sip/sdp.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS



typedef struct _RakiaSipMedia RakiaSipMedia;
typedef struct _RakiaSipMediaClass RakiaSipMediaClass;
typedef struct _RakiaSipMediaPrivate RakiaSipMediaPrivate;

typedef struct _RakiaSipSession RakiaSipSession;

struct _RakiaSipMediaClass {
    GObjectClass parent_class;
};

struct _RakiaSipMedia {
    GObject parent;
    RakiaSipMediaPrivate *priv;
};

typedef enum {
    RAKIA_DIRECTION_NONE = 0,
    RAKIA_DIRECTION_SEND = 1,
    RAKIA_DIRECTION_RECEIVE = 2,
    RAKIA_DIRECTION_BIDIRECTIONAL = 3,
} RakiaDirection;

typedef struct _RakiaSipCodecParam {
  gchar *name;
  gchar *value;
} RakiaSipCodecParam;

typedef struct _RakiaSipCodec {
  guint id;
  gchar *encoding_name;
  guint clock_rate;
  guint channels;
  GPtrArray *params;
} RakiaSipCodec;


typedef struct _RakiaSipCandidate {
  guint component;
  gchar *ip;
  guint port;
  gchar *foundation;
  guint priority;
} RakiaSipCandidate;

GType rakia_sip_media_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_SIP_MEDIA \
  (rakia_sip_media_get_type())
#define RAKIA_SIP_MEDIA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_SIP_MEDIA, RakiaSipMedia))
#define RAKIA_SIP_MEDIA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaClass))
#define RAKIA_IS_SIP_MEDIA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_SIP_MEDIA))
#define RAKIA_IS_SIP_MEDIA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_SIP_MEDIA))
#define RAKIA_SIP_MEDIA_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaClass))

/* For RakiaSipSession */

gchar * rakia_sdp_get_string_attribute (const sdp_attribute_t *attrs,
                                        const char *name);

gboolean rakia_sip_media_set_remote_media (RakiaSipMedia *media,
    const sdp_media_t *new_media, gboolean authoritative);

void rakia_sip_media_generate_sdp (RakiaSipMedia *media, GString *out,
    gboolean authoritative);

gboolean rakia_sip_media_is_ready (RakiaSipMedia *self);

gboolean rakia_sip_media_is_codec_intersect_pending (RakiaSipMedia *self);

RakiaDirection rakia_direction_from_remote_media (const sdp_media_t *media);

void rakia_sip_media_local_updated (RakiaSipMedia *self); /* ?? */

void rakia_sip_media_set_hold_requested (RakiaSipMedia *media,
    gboolean hold_requested);
gboolean rakia_sip_media_is_held (RakiaSipMedia *media);

RakiaSipMedia *rakia_sip_media_new (RakiaSipSession *session,
    TpMediaStreamType media_type,
    const gchar *name,
    RakiaDirection requested_direction,
    gboolean created_locally,
    gboolean hold_requested);


const gchar * sip_media_get_media_type_str (RakiaSipMedia *self);

/* Functions for both */

TpMediaStreamType rakia_sip_media_get_media_type (RakiaSipMedia *self);

/* Functions for the upper layers */


RakiaSipCodec* rakia_sip_codec_new (guint id, const gchar *encoding_name,
    guint clock_rate, guint channels);
void rakia_sip_codec_add_param (RakiaSipCodec *codec, const gchar *name,
    const gchar *value);
void rakia_sip_codec_free (RakiaSipCodec *codec);

RakiaSipCandidate* rakia_sip_candidate_new (guint component,
    const gchar *ip, guint port,
    const gchar *foundation, guint priority);
void rakia_sip_candidate_free (RakiaSipCandidate *candidate);

void rakia_sip_media_take_local_codecs (RakiaSipMedia *self,
    GPtrArray *local_codecs);
void rakia_sip_media_take_local_candidate (RakiaSipMedia *self,
    RakiaSipCandidate *candidate);
gboolean rakia_sip_media_local_candidates_prepared (RakiaSipMedia *self);

GPtrArray *rakia_sip_media_get_remote_codecs (RakiaSipMedia *self);
GPtrArray *rakia_sip_media_get_remote_candidates (RakiaSipMedia *self);

const gchar *rakia_sip_media_get_name (RakiaSipMedia *media);

RakiaSipSession *rakia_sip_media_get_session (RakiaSipMedia *media);

void rakia_sip_media_codecs_rejected (RakiaSipMedia *media);

gboolean rakia_sip_media_is_created_locally (RakiaSipMedia *self);

void rakia_sip_media_set_requested_direction (RakiaSipMedia *media,
    RakiaDirection direction);

RakiaDirection rakia_sip_media_get_direction (RakiaSipMedia *media);

RakiaDirection rakia_sip_media_get_remote_direction (RakiaSipMedia *media);

RakiaDirection rakia_sip_media_get_requested_direction (
    RakiaSipMedia *self);


G_END_DECLS

#endif /* #ifndef __RAKIA_SIP_MEDIA_H__*/
