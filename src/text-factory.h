/*
 * text-factory.h - Text channel factory for SIP connection manager
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007-2008 Nokia Corporation
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

#ifndef __TPSIP_TEXT_FACTORY_H__
#define __TPSIP_TEXT_FACTORY_H__

#include <telepathy-glib/channel-factory-iface.h>

#include "sip-connection-sofia.h"
#include "sip-text-channel.h"

G_BEGIN_DECLS

typedef struct _TpsipTextFactory TpsipTextFactory;
typedef struct _TpsipTextFactoryClass TpsipTextFactoryClass;

struct _TpsipTextFactoryClass {
  GObjectClass parent_class;
};

struct _TpsipTextFactory {
  GObject parent;
};

GType tpsip_text_factory_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_TEXT_FACTORY \
  (tpsip_text_factory_get_type())
#define TPSIP_TEXT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_TEXT_FACTORY, TpsipTextFactory))
#define TPSIP_TEXT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_TEXT_FACTORY, TpsipTextFactoryClass))
#define TPSIP_IS_TEXT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_TEXT_FACTORY))
#define TPSIP_IS_TEXT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_TEXT_FACTORY))
#define TPSIP_TEXT_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_TEXT_FACTORY, TpsipTextFactoryClass))

TpsipTextChannel *tpsip_text_factory_lookup_channel (TpChannelFactoryIface *iface,
    guint handle);

TpsipTextChannel *tpsip_text_factory_new_channel (TpChannelFactoryIface *iface,
    TpHandle handle, TpHandle initiator, gpointer request);

G_END_DECLS

#endif
