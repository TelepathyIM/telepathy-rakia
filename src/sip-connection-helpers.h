/*
 * sip-connection-helpers.h - Helper routines used by TpsipConnection
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

#ifndef __TPSIP_CONNECTION_HELPERS_H__
#define __TPSIP_CONNECTION_HELPERS_H__

#include <glib.h>

#include "sip-connection.h"
#include <tpsip/sofia-decls.h>

G_BEGIN_DECLS

/***********************************************************************
 * Functions for accessing Sofia-SIP interface handles
 ***********************************************************************/

nua_handle_t *tpsip_conn_create_register_handle (TpsipConnection *conn,
    TpHandle contact);
nua_handle_t *tpsip_conn_create_request_handle (TpsipConnection *conn,
    TpHandle contact);

/***********************************************************************
 * Functions for managing NUA outbound/keepalive parameters and STUN settings
 ***********************************************************************/

const url_t * tpsip_conn_get_local_url (TpsipConnection *conn);
void tpsip_conn_update_proxy_and_transport (TpsipConnection *conn);
void tpsip_conn_update_nua_outbound (TpsipConnection *conn);
void tpsip_conn_update_nua_keepalive_interval (TpsipConnection *conn);
void tpsip_conn_update_nua_contact_features (TpsipConnection *conn);
void tpsip_conn_update_stun_server (TpsipConnection *conn);
void tpsip_conn_resolv_stun_server (TpsipConnection *conn, const gchar *stun_host);
void tpsip_conn_discover_stun_server (TpsipConnection *conn);

/***********************************************************************
 * Functions for saving NUA events
 ***********************************************************************/

void tpsip_conn_save_event (TpsipConnection *conn,
                            nua_saved_event_t ret_saved [1]);

/***********************************************************************
 * SIP URI helpers
 ***********************************************************************/

gchar * tpsip_handle_normalize (TpHandleRepoIface *repo,
                                const gchar *sipuri,
                                gpointer context,
                                GError **error);

gchar *tpsip_normalize_contact (const gchar *sipuri,
    const url_t *base_url,
    const gchar *transport,
    GError **error);

const url_t* tpsip_conn_get_contact_url (TpsipConnection *conn,
                                         TpHandle handle);

TpHandle tpsip_handle_parse_from (TpHandleRepoIface *contact_repo,
                                  const sip_t *sip);

/***********************************************************************
 * Heartbeat management for keepalives
 ***********************************************************************/

void tpsip_conn_heartbeat_init (TpsipConnection *self);
void tpsip_conn_heartbeat_shutdown (TpsipConnection *self);

G_END_DECLS

#endif /* #ifndef __TPSIP_CONNECTION_HELPERS_H__*/
