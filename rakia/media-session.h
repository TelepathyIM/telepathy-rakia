/*
 * sip-media-session.h - Header for RakiaMediaSession
 * Copyright (C) 2005 Collabora Ltd.
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

#ifndef __RAKIA_MEDIA_SESSION_H__
#define __RAKIA_MEDIA_SESSION_H__

#include <rakia/media-stream.h>

#include <glib-object.h>
#include <telepathy-glib/handle.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef enum {
    RAKIA_MEDIA_SESSION_STATE_CREATED = 0,
    RAKIA_MEDIA_SESSION_STATE_INVITE_SENT,
    RAKIA_MEDIA_SESSION_STATE_INVITE_RECEIVED,
    RAKIA_MEDIA_SESSION_STATE_RESPONSE_RECEIVED,
    RAKIA_MEDIA_SESSION_STATE_ACTIVE,
    RAKIA_MEDIA_SESSION_STATE_REINVITE_SENT,
    RAKIA_MEDIA_SESSION_STATE_REINVITE_RECEIVED,
    RAKIA_MEDIA_SESSION_STATE_REINVITE_PENDING,
    RAKIA_MEDIA_SESSION_STATE_ENDED,

    NUM_RAKIA_MEDIA_SESSION_STATES
} RakiaMediaSessionState;

typedef struct _RakiaMediaSession RakiaMediaSession;
typedef struct _RakiaMediaSessionClass RakiaMediaSessionClass;
typedef struct _RakiaMediaSessionPrivate RakiaMediaSessionPrivate;

struct _RakiaMediaSessionClass {
    GObjectClass parent_class;
};

struct _RakiaMediaSession {
    GObject parent;
    RakiaMediaSessionPrivate *priv;
};

GType rakia_media_session_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_MEDIA_SESSION \
  (rakia_media_session_get_type())
#define RAKIA_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_MEDIA_SESSION, RakiaMediaSession))
#define RAKIA_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_MEDIA_SESSION, RakiaMediaSessionClass))
#define RAKIA_IS_MEDIA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_MEDIA_SESSION))
#define RAKIA_IS_MEDIA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_MEDIA_SESSION))
#define RAKIA_MEDIA_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_MEDIA_SESSION, RakiaMediaSessionClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

TpHandle rakia_media_session_get_peer (RakiaMediaSession *session);
void rakia_media_session_terminate (RakiaMediaSession *session);
RakiaMediaSessionState rakia_media_session_get_state (RakiaMediaSession *session);
void rakia_media_session_change_state (RakiaMediaSession *session,
                                     RakiaMediaSessionState new_state);
gboolean rakia_media_session_set_remote_media (RakiaMediaSession *chan,
                                            const sdp_session_t* r_sdp);
RakiaMediaStream* rakia_media_session_add_stream (RakiaMediaSession *self,
                                                  guint media_type,
                                                  TpMediaStreamDirection direction,
                                                  gboolean created_locally);
gboolean rakia_media_session_request_streams (RakiaMediaSession *session,
					    const GArray *media_types,
					    GPtrArray *ret,
					    GError **error);
gboolean rakia_media_session_remove_streams (RakiaMediaSession *session,
                                           const GArray *stream_ids,
                                           GError **error);
void rakia_media_session_list_streams (RakiaMediaSession *session,
                                     GPtrArray *ret);
gboolean rakia_media_session_request_stream_direction (RakiaMediaSession *session,
                                                     guint stream_id,
                                                     guint direction,
                                                     GError **error);
void rakia_media_session_receive_invite (RakiaMediaSession *self);
void rakia_media_session_receive_reinvite (RakiaMediaSession *self);
void rakia_media_session_accept (RakiaMediaSession *self);
void rakia_media_session_respond (RakiaMediaSession *self,
                                  gint status,
                                  const char *message);
gboolean rakia_media_session_is_accepted (RakiaMediaSession *self);
void rakia_media_session_resolve_glare (RakiaMediaSession *self);

TpLocalHoldState rakia_media_session_get_hold_state (RakiaMediaSession *session);
void rakia_media_session_request_hold (RakiaMediaSession *session,
                                       gboolean hold);

gboolean rakia_media_session_start_telephony_event (RakiaMediaSession *self,
                                                    guint stream_id,
                                                    guchar event,
                                                    GError **error);
gboolean rakia_media_session_stop_telephony_event  (RakiaMediaSession *self,
                                                    guint stream_id,
                                                    GError **error);

gint rakia_media_session_rate_native_transport (RakiaMediaSession *session,
                                              const GValue *transport);

gboolean rakia_sdp_rtcp_bandwidth_throttled (const sdp_bandwidth_t *b);

gchar * rakia_sdp_get_string_attribute (const sdp_attribute_t *attrs,
                                        const char *name);

#endif /* #ifndef __RAKIA_MEDIA_SESSION_H__*/
