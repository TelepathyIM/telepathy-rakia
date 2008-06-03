/*
 * sip-media-session.h - Header for TpsipMediaSession
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

#ifndef __TPSIP_MEDIA_SESSION_H__
#define __TPSIP_MEDIA_SESSION_H__

#include <glib-object.h>
#include <telepathy-glib/handle.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef enum {
    SIP_MEDIA_SESSION_STATE_CREATED = 0,
    SIP_MEDIA_SESSION_STATE_INVITE_SENT,
    SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED,
    SIP_MEDIA_SESSION_STATE_RESPONSE_RECEIVED,
    SIP_MEDIA_SESSION_STATE_ACTIVE,
    SIP_MEDIA_SESSION_STATE_REINVITE_SENT,
    SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED,
    SIP_MEDIA_SESSION_STATE_ENDED
} SIPMediaSessionState;

typedef struct _TpsipMediaSession TpsipMediaSession;
typedef struct _TpsipMediaSessionClass TpsipMediaSessionClass;

struct _TpsipMediaSessionClass {
    GObjectClass parent_class;
};

struct _TpsipMediaSession {
    GObject parent;
};

GType tpsip_media_session_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_MEDIA_SESSION \
  (tpsip_media_session_get_type())
#define TPSIP_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_MEDIA_SESSION, TpsipMediaSession))
#define TPSIP_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_MEDIA_SESSION, TpsipMediaSessionClass))
#define TPSIP_IS_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_MEDIA_SESSION))
#define TPSIP_IS_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_MEDIA_SESSION))
#define TPSIP_MEDIA_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_MEDIA_SESSION, TpsipMediaSessionClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

TpHandle tpsip_media_session_get_peer (TpsipMediaSession *session);
void tpsip_media_session_terminate (TpsipMediaSession *session);
TpsipMediaSessionState tpsip_media_session_get_state (TpsipMediaSession *session);
void tpsip_media_session_change_state (TpsipMediaSession *session,
                                     TpsipMediaSessionState new_state);
gboolean tpsip_media_session_set_remote_media (TpsipMediaSession *chan,
                                            const sdp_session_t* r_sdp);
gboolean tpsip_media_session_request_streams (TpsipMediaSession *session,
					    const GArray *media_types,
					    GPtrArray *ret,
					    GError **error);
gboolean tpsip_media_session_remove_streams (TpsipMediaSession *session,
                                           const GArray *stream_ids,
                                           GError **error);
void tpsip_media_session_list_streams (TpsipMediaSession *session,
                                     GPtrArray *ret);
gboolean tpsip_media_session_request_stream_direction (TpsipMediaSession *session,
                                                     guint stream_id,
                                                     guint direction,
                                                     GError **error);
void tpsip_media_session_receive_invite (TpsipMediaSession *self);
void tpsip_media_session_receive_reinvite (TpsipMediaSession *self);
void tpsip_media_session_accept (TpsipMediaSession *self);
void tpsip_media_session_reject (TpsipMediaSession *self,
                                 gint status,
                                 const char *message);
gboolean tpsip_media_session_is_accepted (TpsipMediaSession *self);

TpLocalHoldState tpsip_media_session_get_hold_state (TpsipMediaSession *session);
void tpsip_media_session_request_hold (TpsipMediaSession *session,
                                       gboolean hold);

gboolean tpsip_media_session_start_telephony_event (TpsipMediaSession *self,
                                                    guint stream_id,
                                                    guchar event,
                                                    GError **error);
gboolean tpsip_media_session_stop_telephony_event  (TpsipMediaSession *self,
                                                    guint stream_id,
                                                    GError **error);

gint tpsip_media_session_rate_native_transport (TpsipMediaSession *session,
                                              const GValue *transport);

gboolean tpsip_sdp_rtcp_bandwidth_throttled (const sdp_bandwidth_t *b);

#ifdef ENABLE_DEBUG

#define SESSION_DEBUG(s, ...)    tpsip_media_session_debug (s, __VA_ARGS__)

void tpsip_media_session_debug (TpsipMediaSession *session,
			      const gchar *format, ...);

#else

#define SESSION_DEBUG(s, ...)

#endif

G_END_DECLS

#endif /* #ifndef __TPSIP_MEDIA_SESSION_H__*/
