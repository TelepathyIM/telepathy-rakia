/*
 * rakia/text-manager.h - Text channel manager for SIP
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007-2011 Nokia Corporation
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

#ifndef __RAKIA_TEXT_MANAGER_H__
#define __RAKIA_TEXT_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _RakiaTextManager RakiaTextManager;
typedef struct _RakiaTextManagerClass RakiaTextManagerClass;

struct _RakiaTextManagerClass {
  GObjectClass parent_class;
};

struct _RakiaTextManager {
  GObject parent;
};

GType rakia_text_manager_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_TEXT_MANAGER \
  (rakia_text_manager_get_type())
#define RAKIA_TEXT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_TEXT_MANAGER, RakiaTextManager))
#define RAKIA_TEXT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_TEXT_MANAGER, RakiaTextManagerClass))
#define RAKIA_IS_TEXT_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_TEXT_MANAGER))
#define RAKIA_IS_TEXT_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_TEXT_MANAGER))
#define RAKIA_TEXT_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_TEXT_MANAGER, RakiaTextManagerClass))

G_END_DECLS

#endif
