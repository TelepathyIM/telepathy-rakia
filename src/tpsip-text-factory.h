/*
 * text-factory.h - Text channel factory for SIP connection manager
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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

#ifndef __SIP_TEXT_FACTORY_H__
#define __SIP_TEXT_FACTORY_H__

#include <telepathy-glib/channel-factory-iface.h>

#include "sip-connection-sofia.h"
#include "sip-text-channel.h"

G_BEGIN_DECLS

typedef struct _SIPTextFactory SIPTextFactory;
typedef struct _SIPTextFactoryClass SIPTextFactoryClass;

struct _SIPTextFactoryClass {
  GObjectClass parent_class;
};

struct _SIPTextFactory {
  GObject parent;
};

GType sip_text_factory_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_TEXT_FACTORY \
  (sip_text_factory_get_type())
#define SIP_TEXT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_TEXT_FACTORY, SIPTextFactory))
#define SIP_TEXT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_TEXT_FACTORY, SIPTextFactoryClass))
#define SIP_IS_TEXT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_TEXT_FACTORY))
#define SIP_IS_TEXT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_TEXT_FACTORY))
#define SIP_TEXT_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_TEXT_FACTORY, SIPTextFactoryClass))

SIPTextChannel *sip_text_factory_lookup_channel (TpChannelFactoryIface *iface,
    guint handle);

SIPTextChannel *sip_text_factory_new_channel (TpChannelFactoryIface *iface,
    TpHandle handle, gpointer request);

G_END_DECLS

#endif
