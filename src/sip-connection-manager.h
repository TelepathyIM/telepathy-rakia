/*
 * sip-connection-manager.h - Header for RakiaConnectionManager
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

#ifndef __RAKIA_CONNECTION_MANAGER_H__
#define __RAKIA_CONNECTION_MANAGER_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef struct _RakiaConnectionManager RakiaConnectionManager;
typedef struct _RakiaConnectionManagerClass RakiaConnectionManagerClass;
typedef struct _RakiaConnectionManagerPrivate RakiaConnectionManagerPrivate;

struct _RakiaConnectionManagerClass {
    TpBaseConnectionManagerClass parent_class;
};

struct _RakiaConnectionManager {
    TpBaseConnectionManager parent;
    RakiaConnectionManagerPrivate *priv;
};

GType rakia_connection_manager_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_CONNECTION_MANAGER \
  (rakia_connection_manager_get_type())
#define RAKIA_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_CONNECTION_MANAGER, RakiaConnectionManager))
#define RAKIA_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_CONNECTION_MANAGER, RakiaConnectionManagerClass))
#define RAKIA_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_CONNECTION_MANAGER))
#define RAKIA_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_CONNECTION_MANAGER))
#define RAKIA_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_CONNECTION_MANAGER, RakiaConnectionManagerClass))

extern const TpCMProtocolSpec rakia_protocols[];

G_END_DECLS

#endif /* #ifndef __RAKIA_CONNECTION_MANAGER_H__*/
