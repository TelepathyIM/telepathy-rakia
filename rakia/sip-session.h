/*
 * rakia-sip-session.h - Header for RakiaSipSession
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

#ifndef __RAKIA_SIP_SESSION_H__
#define __RAKIA_SIP_SESSION_H__

#include <glib-object.h>
#include <telepathy-glib/handle.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS

typedef enum {
    RAKIA_SIP_SESSION_STATE_CREATED = 0,
    RAKIA_SIP_SESSION_STATE_INVITE_SENT,
    RAKIA_SIP_SESSION_STATE_INVITE_RECEIVED,
    RAKIA_SIP_SESSION_STATE_RESPONSE_RECEIVED,
    RAKIA_SIP_SESSION_STATE_ACTIVE,
    RAKIA_SIP_SESSION_STATE_REINVITE_SENT,
    RAKIA_SIP_SESSION_STATE_REINVITE_RECEIVED,
    RAKIA_SIP_SESSION_STATE_REINVITE_PENDING,
    RAKIA_SIP_SESSION_STATE_ENDED,

    NUM_RAKIA_SIP_SESSION_STATES
} RakiaSipSessionState;


typedef enum {
  RAKIA_SIP_MEDIA_TYPE_AUDIO,
  RAKIA_SIP_MEDIA_TYPE_VIDEO
} RakiaSipMediaType;

typedef struct _RakiaSipSession RakiaSipSession;
typedef struct _RakiaSipSessionClass RakiaSipSessionClass;
typedef struct _RakiaSipSessionPrivate RakiaSipSessionPrivate;

struct _RakiaSipSessionClass {
    GObjectClass parent_class;
};

struct _RakiaSipSession {
    GObject parent;
    RakiaSipSessionPrivate *priv;
};

GType rakia_sip_session_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_SIP_SESSION \
  (rakia_sip_session_get_type())
#define RAKIA_SIP_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_SIP_SESSION, RakiaSipSession))
#define RAKIA_SIP_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_SIP_SESSION, RakiaSipSessionClass))
#define RAKIA_IS_SIP_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_SIP_SESSION))
#define RAKIA_IS_SIP_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_SIP_SESSION))
#define RAKIA_SIP_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_SIP_SESSION, RakiaSipSessionClass))

void rakia_sip_session_terminate (RakiaSipSession *session);
RakiaSipSessionState rakia_sip_session_get_state (RakiaSipSession *session);
void rakia_sip_session_change_state (RakiaSipSession *session,
    RakiaSipSessionState new_state);
gboolean rakia_sip_session_set_remote_sip (RakiaSipSession *chan,
    const sdp_session_t* r_sdp);
#if 0
RakiaSipMedia* rakia_sip_session_add_media (RakiaSipSession *self,
    guint media_type,
    TpMediaMediaDirection direction,
    gboolean created_locally);
#endif

void rakia_sip_session_receive_invite (RakiaSipSession *self);
void rakia_sip_session_receive_reinvite (RakiaSipSession *self);
void rakia_sip_session_accept (RakiaSipSession *self);
void rakia_sip_session_respond (RakiaSipSession *self,
    gint status,
    const char *message);
gboolean rakia_sip_session_is_accepted (RakiaSipSession *self);
void rakia_sip_session_resolve_glare (RakiaSipSession *self);

TpLocalHoldState rakia_sip_session_get_hold_state (RakiaSipSession *session);
void rakia_sip_session_request_hold (RakiaSipSession *session,
    gboolean hold);

gboolean rakia_sip_session_has_sip (RakiaSipSession *self,
    RakiaSipMediaType media_type);

gint rakia_sip_session_rate_native_transport (RakiaSipSession *session,
    const GValue *transport);

gboolean rakia_sdp_rtcp_bandwidth_throttled (const sdp_bandwidth_t *b);

gchar * rakia_sdp_get_string_attribute (const sdp_attribute_t *attrs,
    const char *name);

G_END_DECLS

#endif /* #ifndef __RAKIA_SIP_SESSION_H__*/
