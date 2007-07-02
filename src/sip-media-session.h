/*
 * sip-media-session.h - Header for SIPMediaSession
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
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

#ifndef __SIP_MEDIA_SESSION_H__
#define __SIP_MEDIA_SESSION_H__

#include <glib-object.h>
#include <telepathy-glib/handle.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef enum {
    SIP_MEDIA_SESSION_STATE_CREATED = 0,
    SIP_MEDIA_SESSION_STATE_INVITE_SENT,
    SIP_MEDIA_SESSION_STATE_INVITE_RECEIVED,
    SIP_MEDIA_SESSION_STATE_ACTIVE,
    SIP_MEDIA_SESSION_STATE_REINVITE_SENT,
    SIP_MEDIA_SESSION_STATE_REINVITE_RECEIVED,
    SIP_MEDIA_SESSION_STATE_ENDED
} SIPMediaSessionState;

typedef struct _SIPMediaSession SIPMediaSession;
typedef struct _SIPMediaSessionClass SIPMediaSessionClass;

struct _SIPMediaSessionClass {
    GObjectClass parent_class;
};

struct _SIPMediaSession {
    GObject parent;
};

GType sip_media_session_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_MEDIA_SESSION \
  (sip_media_session_get_type())
#define SIP_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_MEDIA_SESSION, SIPMediaSession))
#define SIP_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_MEDIA_SESSION, SIPMediaSessionClass))
#define SIP_IS_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_MEDIA_SESSION))
#define SIP_IS_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_MEDIA_SESSION))
#define SIP_MEDIA_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_MEDIA_SESSION, SIPMediaSessionClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

/* Telepathy type helpers */
#define SIP_TP_STREAM_LIST_TYPE (sip_tp_stream_list_type ())
GType sip_tp_stream_list_type (void) G_GNUC_CONST;

TpHandle sip_media_session_get_peer (SIPMediaSession *session);
void sip_media_session_terminate (SIPMediaSession *session);
gboolean sip_media_session_set_remote_media (SIPMediaSession *chan,
                                            const sdp_session_t* r_sdp);
gboolean sip_media_session_request_streams (SIPMediaSession *session,
					    const GArray *media_types,
					    GPtrArray **ret,
					    GError **error);
gboolean sip_media_session_list_streams (SIPMediaSession *session,
					 GPtrArray **ret);
gboolean sip_media_session_request_stream_direction (SIPMediaSession *session,
                                                     guint stream_id,
                                                     guint direction,
                                                     GError **error);
void sip_media_session_receive_invite (SIPMediaSession *self);
void sip_media_session_receive_reinvite (SIPMediaSession *self);
void sip_media_session_accept (SIPMediaSession *self);
void sip_media_session_reject (SIPMediaSession *self,
                               gint status,
                               const char *message);
gboolean sip_media_session_start_telephony_event (SIPMediaSession *self,
                                                  guint stream_id,
                                                  guchar event,
                                                  GError **error);
gboolean sip_media_session_stop_telephony_event  (SIPMediaSession *self,
                                                  guint stream_id,
                                                  GError **error);

#ifdef ENABLE_DEBUG

#define SESSION_DEBUG(s, ...)    sip_media_session_debug (s, __VA_ARGS__)

void sip_media_session_debug (SIPMediaSession *session,
			      const gchar *format, ...);

#else

#define SESSION_DEBUG(s, ...)

#endif

G_END_DECLS

#endif /* #ifndef __SIP_MEDIA_SESSION_H__*/
