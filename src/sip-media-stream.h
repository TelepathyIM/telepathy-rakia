/*
 * sip-media-stream.h - Header for SIPMediaStream
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
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

#ifndef __SIP_MEDIA_STREAM_H__
#define __SIP_MEDIA_STREAM_H__

#include <glib-object.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef struct _SIPMediaStream SIPMediaStream;
typedef struct _SIPMediaStreamClass SIPMediaStreamClass;

struct _SIPMediaStreamClass {
    GObjectClass parent_class;
};

struct _SIPMediaStream {
    GObject parent;
};

GType sip_media_stream_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_MEDIA_STREAM \
  (sip_media_stream_get_type())
#define SIP_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_MEDIA_STREAM, SIPMediaStream))
#define SIP_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_MEDIA_STREAM, SIPMediaStreamClass))
#define SIP_IS_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_MEDIA_STREAM))
#define SIP_IS_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_MEDIA_STREAM))
#define SIP_MEDIA_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_MEDIA_STREAM, SIPMediaStreamClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

const char *sip_media_stream_local_sdp (SIPMediaStream *self);
gboolean sip_media_stream_set_remote_info (SIPMediaStream *self,
                                           const sdp_media_t *media);
void sip_media_stream_set_playing (SIPMediaStream *self, gboolean playing);
gboolean sip_media_stream_is_ready (SIPMediaStream *self);
void sip_media_stream_start_telephony_event (SIPMediaStream *self, guint event);
void sip_media_stream_stop_telephony_event  (SIPMediaStream *self);

#define TP_TYPE_TRANSPORT_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_DOUBLE, \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_STRING, \
      G_TYPE_INVALID))
#define TP_TYPE_TRANSPORT_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_TRANSPORT_STRUCT))
#define TP_TYPE_CANDIDATE_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_STRING, \
      TP_TYPE_TRANSPORT_LIST, \
      G_TYPE_INVALID))
#define TP_TYPE_CANDIDATE_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_CANDIDATE_STRUCT))

#define TP_TYPE_CODEC_STRUCT (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, \
      G_TYPE_STRING, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      G_TYPE_UINT, \
      DBUS_TYPE_G_STRING_STRING_HASHTABLE, \
      G_TYPE_INVALID))
#define TP_TYPE_CODEC_LIST (dbus_g_type_get_collection ("GPtrArray", \
      TP_TYPE_CODEC_STRUCT))

G_END_DECLS

#endif /* #ifndef __SIP_MEDIA_STREAM_H__*/
