/*
 * sip-connection-helpers.h - Helper routines used by SIPConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2006 Nokia Corporation
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

#ifndef __SIP_CONNECTION_HELPERS_H__
#define __SIP_CONNECTION_HELPERS_H__

#include <glib.h>

#include "sip-connection.h"
#include "sip-sofia-decls.h"

G_BEGIN_DECLS

/***********************************************************************
 * Functions for accessing Sofia-SIP interface handles
 ***********************************************************************/

nua_handle_t *sip_conn_create_register_handle (SIPConnection *conn,
    TpHandle contact);
nua_handle_t *sip_conn_create_request_handle (SIPConnection *conn,
    TpHandle contact);

/***********************************************************************
 * Functions for managing NUA outbound/keepalive parameters and STUN settings
 ***********************************************************************/

void sip_conn_update_nua_outbound (SIPConnection *conn);
void sip_conn_update_nua_keepalive_interval (SIPConnection *conn);
void sip_conn_update_nua_contact_features (SIPConnection *conn);
void sip_conn_update_stun_server (SIPConnection *conn);
void sip_conn_resolv_stun_server (SIPConnection *conn, const gchar *stun_server);
void sip_conn_discover_stun_server (SIPConnection *conn);

/***********************************************************************
 * Functions for saving NUA events
 ***********************************************************************/

void sip_conn_save_event (SIPConnection *conn,
                          nua_saved_event_t ret_saved [1]);

/***********************************************************************
 * SIP URI helpers *
 ***********************************************************************/

gchar * sip_conn_normalize_uri (SIPConnection *conn,
                                const gchar *sipuri,
                                GError **error);
gchar * sip_conn_domain_from_uri (const gchar *str);

G_END_DECLS

#endif /* #ifndef __SIP_CONNECTION_HELPERS_H__*/
