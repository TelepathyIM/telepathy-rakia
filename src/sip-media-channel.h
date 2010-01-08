/*
 * sip-media-channel.h - Header for TpsipMediaChannel
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

#ifndef __TPSIP_MEDIA_CHANNEL_H__
#define __TPSIP_MEDIA_CHANNEL_H__

#include <glib-object.h>
#include <sofia-sip/sdp.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/properties-mixin.h>

#include <tpsip/sofia-decls.h>


G_BEGIN_DECLS

typedef struct _TpsipMediaChannel TpsipMediaChannel;
typedef struct _TpsipMediaChannelClass TpsipMediaChannelClass;

struct _TpsipMediaChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
    TpDBusPropertiesMixinClass dbus_props_class;
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

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void tpsip_media_channel_close (TpsipMediaChannel *self);

gboolean _tpsip_media_channel_add_member (GObject *iface,
                                          TpHandle handle,
                                          const gchar *message,
                                          GError **error);

void tpsip_media_channel_create_initial_streams (TpsipMediaChannel *self);

void tpsip_media_channel_receive_invite (TpsipMediaChannel *self,
                                         nua_handle_t *nh);

guint
tpsip_media_channel_change_call_state (TpsipMediaChannel *self,
                                       TpHandle peer,
                                       guint flags_add,
                                       guint flags_remove);

G_END_DECLS

#endif /* #ifndef __TPSIP_MEDIA_CHANNEL_H__*/
