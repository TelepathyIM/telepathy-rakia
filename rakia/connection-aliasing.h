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

#ifndef __RAKIA_CONNECTION_ALIASING_H__
#define __RAKIA_CONNECTION_ALIASING_H__

#include <glib-object.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef struct _RakiaConnectionAliasing RakiaConnectionAliasing;

typedef struct _RakiaConnectionAliasingInterface
RakiaConnectionAliasingInterface;

/* TYPE MACROS */
#define RAKIA_TYPE_CONNECTION_ALIASING \
  (rakia_connection_aliasing_get_type ())
#define RAKIA_CONNECTION_ALIASING(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      RAKIA_TYPE_CONNECTION_ALIASING, RakiaConnectionAliasing))
#define RAKIA_IS_CONNECTION_ALIASING(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_CONNECTION_ALIASING))
#define RAKIA_CONNECTION_ALIASING_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE((obj), \
      RAKIA_TYPE_CONNECTION_ALIASING, RakiaConnectionAliasingInterface))

struct _RakiaConnectionAliasingInterface {
  GTypeInterface base_iface;
};

GType rakia_connection_aliasing_get_type (void) G_GNUC_CONST;

void rakia_connection_aliasing_svc_iface_init (gpointer g_iface,
    gpointer iface_data);

gboolean rakia_conn_aliasing_fill_contact_attributes (TpBaseConnection *base,
    const gchar *dbus_interface,
    TpHandle handle,
    GVariantDict *attributes);

G_END_DECLS

#endif /*__RAKIA_CONN_ALIASING_H__*/
