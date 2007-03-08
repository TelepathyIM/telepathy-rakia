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

#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/properties-mixin.h>

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


gboolean sip_media_channel_add_member (TpSvcChannelInterfaceGroup *,
    TpHandle, const gchar *, GError **);

void sip_media_channel_close (SIPMediaChannel *self);

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void sip_media_channel_respond_to_invite (SIPMediaChannel *self, 
					  TpHandle handle,
					  const char *subject,
					  const char *remoteurl);
int sip_media_channel_set_remote_info (SIPMediaChannel *chan, const char* r_sdp);
void sip_media_channel_stream_state (SIPMediaChannel *chan,
                                     guint stream_id,
                                     guint state);
void sip_media_channel_peer_error (SIPMediaChannel *self,
                                   guint status,
                                   const char* message);

G_END_DECLS

#endif /* #ifndef __SIP_MEDIA_CHANNEL_H__*/
