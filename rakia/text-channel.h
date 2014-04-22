/*
 * sip-text-channel.h - Header for RakiaTextChannel
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

#ifndef __RAKIA_TEXT_CHANNEL_H__
#define __RAKIA_TEXT_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/telepathy-glib.h>

#include <rakia/sofia-decls.h>

G_BEGIN_DECLS

typedef struct _RakiaTextChannel RakiaTextChannel;
typedef struct _RakiaTextChannelClass RakiaTextChannelClass;

struct _RakiaTextChannelClass {
    TpBaseChannelClass parent_class;
};

struct _RakiaTextChannel {
    TpBaseChannel parent;

    TpMessageMixin message_mixin;
};

GType rakia_text_channel_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_TEXT_CHANNEL \
  (rakia_text_channel_get_type())
#define RAKIA_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_TEXT_CHANNEL, RakiaTextChannel))
#define RAKIA_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_TEXT_CHANNEL, RakiaTextChannelClass))
#define RAKIA_IS_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_TEXT_CHANNEL))
#define RAKIA_IS_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_TEXT_CHANNEL))
#define RAKIA_TEXT_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_TEXT_CHANNEL, RakiaTextChannelClass))

void rakia_text_channel_receive (RakiaTextChannel  *obj,
                                 const sip_t       *sip,
                                 TpHandle           sender,
                                 const char        *text,
                                 gsize              len);

G_END_DECLS

#endif /* #ifndef __RAKIA_TEXT_CHANNEL_H__*/
