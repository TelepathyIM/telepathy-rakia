/*
 * sip-connection-manager.h - Header for TpsipConnectionManager
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
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

#ifndef __TPSIP_CONNECTION_MANAGER_H__
#define __TPSIP_CONNECTION_MANAGER_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection-manager.h>

G_BEGIN_DECLS

typedef struct _TpsipConnectionManager TpsipConnectionManager;
typedef struct _TpsipConnectionManagerClass TpsipConnectionManagerClass;
typedef struct _TpsipConnectionManagerPrivate TpsipConnectionManagerPrivate;

struct _TpsipConnectionManagerClass {
    TpBaseConnectionManagerClass parent_class;
};

struct _TpsipConnectionManager {
    TpBaseConnectionManager parent;
    TpsipConnectionManagerPrivate *priv;
};

GType tpsip_connection_manager_get_type(void);

/* TYPE MACROS */
#define TPSIP_TYPE_CONNECTION_MANAGER \
  (tpsip_connection_manager_get_type())
#define TPSIP_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TPSIP_TYPE_CONNECTION_MANAGER, TpsipConnectionManager))
#define TPSIP_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TPSIP_TYPE_CONNECTION_MANAGER, TpsipConnectionManagerClass))
#define TPSIP_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_CONNECTION_MANAGER))
#define TPSIP_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_CONNECTION_MANAGER))
#define TPSIP_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TPSIP_TYPE_CONNECTION_MANAGER, TpsipConnectionManagerClass))

extern const TpCMProtocolSpec tpsip_protocols[];

G_END_DECLS

#endif /* #ifndef __TPSIP_CONNECTION_MANAGER_H__*/
