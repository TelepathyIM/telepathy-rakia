/*
 * sip-connection-helpers.h - Helper routines used by RakiaConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
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

#ifndef __RAKIA_CONNECTION_HELPERS_H__
#define __RAKIA_CONNECTION_HELPERS_H__

#include <glib.h>

#include "sip-connection.h"
#include <rakia/sofia-decls.h>

G_BEGIN_DECLS

/***********************************************************************
 * Functions for accessing Sofia-SIP interface handles
 ***********************************************************************/

nua_handle_t *rakia_conn_create_register_handle (RakiaConnection *conn,
    TpHandle contact);
nua_handle_t *rakia_conn_create_request_handle (RakiaConnection *conn,
    TpHandle contact);

/***********************************************************************
 * Functions for managing NUA outbound/keepalive parameters and STUN settings
 ***********************************************************************/

const url_t * rakia_conn_get_local_url (RakiaConnection *conn);
void rakia_conn_update_proxy_and_transport (RakiaConnection *conn);
void rakia_conn_update_nua_outbound (RakiaConnection *conn);
void rakia_conn_update_nua_keepalive_interval (RakiaConnection *conn);
void rakia_conn_update_nua_contact_features (RakiaConnection *conn);
void rakia_conn_update_stun_server (RakiaConnection *conn);
void rakia_conn_resolv_stun_server (RakiaConnection *conn, const gchar *stun_host);
void rakia_conn_discover_stun_server (RakiaConnection *conn);

/***********************************************************************
 * Heartbeat management for keepalives
 ***********************************************************************/

void rakia_conn_heartbeat_init (RakiaConnection *self);
void rakia_conn_heartbeat_shutdown (RakiaConnection *self);

G_END_DECLS

#endif /* #ifndef __RAKIA_CONNECTION_HELPERS_H__*/
