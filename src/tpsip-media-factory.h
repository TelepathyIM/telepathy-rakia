/*
 * media-factory.h - Media channel factory for SIP connection manager
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

#ifndef __SIP_MEDIA_FACTORY_H__
#define __SIP_MEDIA_FACTORY_H__

#include <telepathy-glib/channel-factory-iface.h>

#include "sip-media-channel.h"

G_BEGIN_DECLS

typedef struct _SIPMediaFactory SIPMediaFactory;
typedef struct _SIPMediaFactoryClass SIPMediaFactoryClass;

struct _SIPMediaFactoryClass {
  GObjectClass parent_class;
};

struct _SIPMediaFactory {
  GObject parent;
};

GType sip_media_factory_get_type(void);

/* TYPE MACROS */
#define SIP_TYPE_MEDIA_FACTORY \
  (sip_media_factory_get_type())
#define SIP_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SIP_TYPE_MEDIA_FACTORY, SIPMediaFactory))
#define SIP_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SIP_TYPE_MEDIA_FACTORY, SIPMediaFactoryClass))
#define SIP_IS_MEDIA_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SIP_TYPE_MEDIA_FACTORY))
#define SIP_IS_MEDIA_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SIP_TYPE_MEDIA_FACTORY))
#define SIP_MEDIA_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SIP_TYPE_MEDIA_FACTORY, SIPMediaFactoryClass))

/***********************************************************************
 * Functions for managing media session IDs (sids) 
 ***********************************************************************/

SIPMediaChannel *sip_media_factory_new_channel (SIPMediaFactory *fac,
                                                gpointer request,
                                                TpHandleType handle_type,
                                                TpHandle handle,
                                                GError **error);

G_END_DECLS

#endif
