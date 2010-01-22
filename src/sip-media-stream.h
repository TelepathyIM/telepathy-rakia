/*
 * sip-media-stream.h - Header for TpsipMediaStream
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#ifndef __TPSIP_MEDIA_STREAM_H__
#define __TPSIP_MEDIA_STREAM_H__

#include <glib-object.h>
#include <telepathy-glib/enums.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef struct _TpsipMediaStream TpsipMediaStream;
typedef struct _TpsipMediaStreamClass TpsipMediaStreamClass;

struct _TpsipMediaStreamClass {
    GObjectClass parent_class;
};

struct _TpsipMediaStream {
    GObject parent;
};

GType tpsip_media_stream_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_MEDIA_STREAM \
  (tpsip_media_stream_get_type())
#define TPSIP_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_MEDIA_STREAM, TpsipMediaStream))
#define TPSIP_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_MEDIA_STREAM, TpsipMediaStreamClass))
#define TPSIP_IS_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_MEDIA_STREAM))
#define TPSIP_IS_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_MEDIA_STREAM))
#define TPSIP_MEDIA_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_MEDIA_STREAM, TpsipMediaStreamClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void tpsip_media_stream_close (TpsipMediaStream *self);
guint tpsip_media_stream_get_id (TpsipMediaStream *self);
guint tpsip_media_stream_get_media_type (TpsipMediaStream *self);
const char *tpsip_media_stream_local_sdp (TpsipMediaStream *self);
gboolean tpsip_media_stream_set_remote_media (TpsipMediaStream *self,
                                              const sdp_media_t *media,
                                              guint direction_up_mask,
                                              guint pending_send_mask);
void tpsip_media_stream_set_playing (TpsipMediaStream *self, gboolean playing);
void tpsip_media_stream_set_sending (TpsipMediaStream *self, gboolean sending);
void tpsip_media_stream_set_direction (TpsipMediaStream *stream,
                                       TpMediaStreamDirection direction,
                                       guint pending_send_mask);
void tpsip_media_stream_apply_pending_direction (TpsipMediaStream *stream,
                                                 guint pending_send_mask);
TpMediaStreamDirection tpsip_media_stream_get_requested_direction (TpsipMediaStream *self);
gboolean tpsip_media_stream_is_local_ready (TpsipMediaStream *self);
gboolean tpsip_media_stream_is_codec_intersect_pending (TpsipMediaStream *self);
void tpsip_media_stream_start_telephony_event (TpsipMediaStream *self, guchar event);
void tpsip_media_stream_stop_telephony_event  (TpsipMediaStream *self);
gboolean tpsip_media_stream_request_hold_state (TpsipMediaStream *self,
                                                gboolean hold);

guint tpsip_tp_media_type (sdp_media_e sip_mtype);
TpMediaStreamDirection tpsip_media_stream_direction_from_remote_media (
                          const sdp_media_t *media);

G_END_DECLS

#endif /* #ifndef __TPSIP_MEDIA_STREAM_H__*/
