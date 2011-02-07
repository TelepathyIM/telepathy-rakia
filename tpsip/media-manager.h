/*
 * tpsip/media-manager.h - Media channel manager for SIP
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

#ifndef __TPSIP_MEDIA_MANAGER_H__
#define __TPSIP_MEDIA_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _TpsipMediaManager TpsipMediaManager;
typedef struct _TpsipMediaManagerClass TpsipMediaManagerClass;

struct _TpsipMediaManagerClass {
  GObjectClass parent_class;
};

struct _TpsipMediaManager {
  GObject parent;
};

GType tpsip_media_manager_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_MEDIA_MANAGER \
  (tpsip_media_manager_get_type())
#define TPSIP_MEDIA_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_MEDIA_MANAGER, TpsipMediaManager))
#define TPSIP_MEDIA_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_MEDIA_MANAGER, TpsipMediaManagerClass))
#define TPSIP_IS_MEDIA_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_MEDIA_MANAGER))
#define TPSIP_IS_MEDIA_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_MEDIA_MANAGER))
#define TPSIP_MEDIA_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_MEDIA_MANAGER, TpsipMediaManagerClass))

G_END_DECLS

#endif
