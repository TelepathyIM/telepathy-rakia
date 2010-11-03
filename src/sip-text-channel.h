/*
 * sip-text-channel.h - Header for TpsipTextChannel
 * Copyright (C) 2005-2008 Collabora Ltd.
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

#ifndef __TPSIP_TEXT_CHANNEL_H__
#define __TPSIP_TEXT_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/message-mixin.h>

#include <tpsip/sofia-decls.h>

G_BEGIN_DECLS

typedef struct _TpsipTextChannel TpsipTextChannel;
typedef struct _TpsipTextChannelClass TpsipTextChannelClass;

struct _TpsipTextChannelClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _TpsipTextChannel {
    GObject parent;

    TpMessageMixin message_mixin;
};

GType tpsip_text_channel_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_TEXT_CHANNEL \
  (tpsip_text_channel_get_type())
#define TPSIP_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_TEXT_CHANNEL, TpsipTextChannel))
#define TPSIP_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_TEXT_CHANNEL, TpsipTextChannelClass))
#define TPSIP_IS_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_TEXT_CHANNEL))
#define TPSIP_IS_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_TEXT_CHANNEL))
#define TPSIP_TEXT_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_TEXT_CHANNEL, TpsipTextChannelClass))

void tpsip_text_channel_receive (TpsipTextChannel  *obj,
                                 const sip_t       *sip,
                                 TpHandle           sender,
                                 const char        *text,
                                 gsize              len);

G_END_DECLS

#endif /* #ifndef __TPSIP_TEXT_CHANNEL_H__*/
