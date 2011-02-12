/*
 * rakia/connection-aliasing.h - Aliasing interface implementation for SIP
 * Copyright (C) 2008, 2011 Nokia Corporation
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
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

#ifndef __TPSIP_CONNECTION_ALIASING_H__
#define __TPSIP_CONNECTION_ALIASING_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _RakiaConnectionAliasing RakiaConnectionAliasing;

typedef struct _RakiaConnectionAliasingInterface
RakiaConnectionAliasingInterface;

/* TYPE MACROS */
#define TPSIP_TYPE_CONNECTION_ALIASING \
  (rakia_connection_aliasing_get_type ())
#define TPSIP_CONNECTION_ALIASING(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      TPSIP_TYPE_CONNECTION_ALIASING, RakiaConnectionAliasing))
#define TPSIP_IS_CONNECTION_ALIASING(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_CONNECTION_ALIASING))
#define TPSIP_CONNECTION_ALIASING_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE((obj), \
      TPSIP_TYPE_CONNECTION_ALIASING, RakiaConnectionAliasingInterface))

struct _RakiaConnectionAliasingInterface {
  GTypeInterface base_iface;
};

GType rakia_connection_aliasing_get_type (void) G_GNUC_CONST;

void rakia_connection_aliasing_init (gpointer instance);

void rakia_connection_aliasing_svc_iface_init (gpointer g_iface,
    gpointer iface_data);

G_END_DECLS

#endif /*__TPSIP_CONN_ALIASING_H__*/
