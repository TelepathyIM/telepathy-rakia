/*
 * sip-media-channel.h - Header for RakiaMediaChannel
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
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

#ifndef __RAKIA_MEDIA_CHANNEL_H__
#define __RAKIA_MEDIA_CHANNEL_H__

#include <glib-object.h>
#include <sofia-sip/sdp.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/properties-mixin.h>

#include <rakia/sofia-decls.h>


G_BEGIN_DECLS

typedef struct _RakiaMediaChannel RakiaMediaChannel;
typedef struct _RakiaMediaChannelClass RakiaMediaChannelClass;

struct _RakiaMediaChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
    TpPropertiesMixinClass properties_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _RakiaMediaChannel {
    GObject parent;
    TpGroupMixin group;
    TpPropertiesMixin properties;
};

GType rakia_media_channel_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_MEDIA_CHANNEL \
  (rakia_media_channel_get_type())
#define RAKIA_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_MEDIA_CHANNEL, RakiaMediaChannel))
#define RAKIA_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_MEDIA_CHANNEL, RakiaMediaChannelClass))
#define RAKIA_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_MEDIA_CHANNEL))
#define RAKIA_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_MEDIA_CHANNEL))
#define RAKIA_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_MEDIA_CHANNEL, RakiaMediaChannelClass))

/***********************************************************************
 * Additional declarations (not based on generated templates)
 ***********************************************************************/

void rakia_media_channel_close (RakiaMediaChannel *self);

gboolean _rakia_media_channel_add_member (GObject *iface,
                                          TpHandle handle,
                                          const gchar *message,
                                          GError **error);

void rakia_media_channel_create_initial_streams (RakiaMediaChannel *self);

void rakia_media_channel_attach_to_nua_handle (RakiaMediaChannel *self,
                                               nua_handle_t *nh);

guint
rakia_media_channel_change_call_state (RakiaMediaChannel *self,
                                       TpHandle peer,
                                       guint flags_add,
                                       guint flags_remove);

G_END_DECLS

#endif /* #ifndef __RAKIA_MEDIA_CHANNEL_H__*/
