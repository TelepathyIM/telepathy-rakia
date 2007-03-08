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

G_BEGIN_DECLS

typedef enum {
    JS_STATE_PENDING_CREATED = 0,
    JS_STATE_PENDING_INITIATED,
    JS_STATE_ACTIVE,
    JS_STATE_ENDED
} JingleSessionState;

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

TpHandle sip_media_session_get_peer (SIPMediaSession *session);
void sip_media_session_terminate (SIPMediaSession *session);
int sip_media_session_set_remote_info (SIPMediaSession *chan, const char* r_sdp);
void sip_media_session_stream_state (SIPMediaSession *sess,
                                     guint stream_id,
                                     guint state);
gboolean sip_media_session_request_streams (SIPMediaSession *session,
					    const GArray *media_types,
					    GPtrArray **ret,
					    GError **error);
gboolean sip_media_session_list_streams (SIPMediaSession *session,
					 GPtrArray **ret);
void sip_media_session_accept (SIPMediaSession *self, gboolean accept);

typedef enum {
    DEBUG_MSG_INFO = 0,
    DEBUG_MSG_DUMP,
    DEBUG_MSG_WARNING,
    DEBUG_MSG_ERROR,
    DEBUG_MSG_EVENT
} DebugMessageType;

#ifndef _GMS_DEBUG_LEVEL
#define _GMS_DEBUG_LEVEL 2
#endif

#if _GMS_DEBUG_LEVEL

#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD_ON    "\x1b[1m"
#define ANSI_BOLD_OFF   "\x1b[22m"
#define ANSI_INVERSE_ON "\x1b[7m"

#define ANSI_BG_RED     "\x1b[41m"
#define ANSI_BG_GREEN   "\x1b[42m"
#define ANSI_BG_YELLOW  "\x1b[43m"
#define ANSI_BG_BLUE    "\x1b[44m"
#define ANSI_BG_MAGENTA "\x1b[45m"
#define ANSI_BG_CYAN    "\x1b[46m"
#define ANSI_BG_WHITE   "\x1b[47m"

#define ANSI_FG_BLACK   "\x1b[30m"
#define ANSI_FG_RED     "\x1b[31m"
#define ANSI_FG_GREEN   "\x1b[32m"
#define ANSI_FG_YELLOW  "\x1b[33m"
#define ANSI_FG_BLUE    "\x1b[34m"
#define ANSI_FG_MAGENTA "\x1b[35m"
#define ANSI_FG_CYAN    "\x1b[36m"
#define ANSI_FG_WHITE   "\x1b[37m"

#define GMS_DEBUG_INFO(s, ...)    sip_media_session_debug (s, DEBUG_MSG_INFO, __VA_ARGS__)
#if _GMS_DEBUG_LEVEL > 1
#define GMS_DEBUG_DUMP(s, ...)    sip_media_session_debug (s, DEBUG_MSG_DUMP, __VA_ARGS__)
#else
#define GMS_DEBUG_DUMP(s, ...)
#endif
#define GMS_DEBUG_WARNING(s, ...) sip_media_session_debug (s, DEBUG_MSG_WARNING, __VA_ARGS__)
#define GMS_DEBUG_ERROR(s, ...)   sip_media_session_debug (s, DEBUG_MSG_ERROR, __VA_ARGS__)
#define GMS_DEBUG_EVENT(s, ...)   sip_media_session_debug (s, DEBUG_MSG_EVENT, __VA_ARGS__)

void sip_media_session_debug (SIPMediaSession *session,
			      DebugMessageType type,
			      const gchar *format, ...);

#else

#define GMS_DEBUG_INFO(s, ...)
#define GMS_DEBUG_DUMP(s, ...)
#define GMS_DEBUG_WARNING(s, ...)
#define GMS_DEBUG_ERROR(s, ...)
#define GMS_DEBUG_EVENT(s, ...)

#endif

G_END_DECLS

#endif /* #ifndef __SIP_MEDIA_SESSION_H__*/
