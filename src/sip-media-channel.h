/*
 * sip-media-channel.h - Header for TpsipMediaChannel
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

#ifndef __TPSIP_MEDIA_CHANNEL_H__
#define __TPSIP_MEDIA_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/handle.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/properties-mixin.h>

#include "sip-sofia-decls.h"

#include <sofia-sip/sdp.h>


G_BEGIN_DECLS

typedef struct _TpsipMediaChannel TpsipMediaChannel;
typedef struct _TpsipMediaChannelClass TpsipMediaChannelClass;

struct _TpsipMediaChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
};

struct _TpsipMediaChannel {
    GObject parent;
    TpGroupMixin group;
    TpPropertiesMixin properties;
};

GType tpsip_media_channel_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_MEDIA_CHANNEL \
  (tpsip_media_channel_get_type())
#define TPSIP_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_MEDIA_CHANNEL, TpsipMediaChannel))
#define TPSIP_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_MEDIA_CHANNEL, TpsipMediaChannelClass))
#define TPSIP_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_MEDIA_CHANNEL))
#define TPSIP_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_MEDIA_CHANNEL))
#define TPSIP_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_MEDIA_CHANNEL, TpsipMediaChannelClass))


void tpsip_media_channel_close (TpsipMediaChannel *self);

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void tpsip_media_channel_terminated (TpsipMediaChannel *self);
void tpsip_media_channel_receive_invite   (TpsipMediaChannel *self,
                                         nua_handle_t *nh,
                                         TpHandle handle);
void tpsip_media_channel_receive_reinvite (TpsipMediaChannel *self);
gboolean tpsip_media_channel_set_remote_media (TpsipMediaChannel *chan,
                                             const sdp_session_t *r_sdp);
void tpsip_media_channel_ready (TpsipMediaChannel *self);
void tpsip_media_channel_call_status (TpsipMediaChannel *self,
                                    guint status,
                                    const char* message);
void tpsip_media_channel_peer_cancel (TpsipMediaChannel *self,
                                    guint cause,
                                    const char* text);

G_END_DECLS

#endif /* #ifndef __TPSIP_MEDIA_CHANNEL_H__*/
