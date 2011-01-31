/*
 * conn-aliasing.h - Aliasing interface implementation for TpsipConnection
 * Copyright (C) 2008 Nokia Corporation
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

#ifndef __TPSIP_CONN_ALIASING_H__
#define __TPSIP_CONN_ALIASING_H__

#include <tpsip/base-connection.h>

G_BEGIN_DECLS

void tpsip_conn_aliasing_init (TpsipBaseConnection *conn);
void tpsip_conn_aliasing_iface_init (gpointer g_iface, gpointer iface_data);

G_END_DECLS

#endif /*__TPSIP_CONN_ALIASING_H__*/
