/*
 * sip-text-channel.h - Header for SIPTextChannel
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

#ifndef __SIP_TEXT_CHANNEL_H__
#define __SIP_TEXT_CHANNEL_H__

#include <glib-object.h>

typedef struct _SipHandleStorage SipHandleStorage;

G_BEGIN_DECLS

typedef struct _SIPTextChannel SIPTextChannel;
typedef struct _SIPTextChannelClass SIPTextChannelClass;

struct _SIPTextChannelClass {
    GObjectClass parent_class;
};

struct _SIPTextChannel {
    GObject parent;
};

GType sip_text_channel_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_TEXT_CHANNEL \
  (sip_text_channel_get_type())
#define SIP_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_TEXT_CHANNEL, SIPTextChannel))
#define SIP_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_TEXT_CHANNEL, SIPTextChannelClass))
#define SIP_IS_TEXT_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_TEXT_CHANNEL))
#define SIP_IS_TEXT_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_TEXT_CHANNEL))
#define SIP_TEXT_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_TEXT_CHANNEL, SIPTextChannelClass))


void sip_text_channel_emit_message_status(SIPTextChannel *obj,
                                          nua_handle_t *nh,
                                          int status);

void sip_text_channel_receive (SIPTextChannel *obj,
                               TpHandle        sender,
                               const char     *message);


G_END_DECLS

#endif /* #ifndef __SIP_TEXT_CHANNEL_H__*/
