/*
 * sip-media-channel.h - Header for SIPMediaChannel
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

#ifndef __SIP_MEDIA_CHANNEL_H__
#define __SIP_MEDIA_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/handle.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/properties-mixin.h>

#include "sip-sofia-decls.h"

#include <sofia-sip/sdp.h>


G_BEGIN_DECLS

typedef struct _SIPMediaChannel SIPMediaChannel;
typedef struct _SIPMediaChannelClass SIPMediaChannelClass;

struct _SIPMediaChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
};

struct _SIPMediaChannel {
    GObject parent;
    TpGroupMixin group;
    TpPropertiesMixin properties;
};

GType sip_media_channel_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_MEDIA_CHANNEL \
  (sip_media_channel_get_type())
#define SIP_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_MEDIA_CHANNEL, SIPMediaChannel))
#define SIP_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_MEDIA_CHANNEL, SIPMediaChannelClass))
#define SIP_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_MEDIA_CHANNEL))
#define SIP_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_MEDIA_CHANNEL))
#define SIP_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_MEDIA_CHANNEL, SIPMediaChannelClass))


void sip_media_channel_close (SIPMediaChannel *self);

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void sip_media_channel_terminated (SIPMediaChannel *self);
void sip_media_channel_receive_invite   (SIPMediaChannel *self,
                                         nua_handle_t *nh,
                                         TpHandle handle);
void sip_media_channel_receive_reinvite (SIPMediaChannel *self);
gboolean sip_media_channel_set_remote_media (SIPMediaChannel *chan,
                                             const sdp_session_t *r_sdp);
void sip_media_channel_ready (SIPMediaChannel *self);
void sip_media_channel_peer_error (SIPMediaChannel *self,
                                   guint status,
                                   const char* message);
void sip_media_channel_peer_cancel (SIPMediaChannel *self,
                                    guint cause,
                                    const char* text);

G_END_DECLS

#endif /* #ifndef __SIP_MEDIA_CHANNEL_H__*/
