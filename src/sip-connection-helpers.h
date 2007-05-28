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

/* note: As one Sofia-SIP NUA instance is created per SIP connection,
 *       SIPConnection is used as the primary context pointer. See
 *       {top}/docs/design.txt for further information.
 *
 *       Each NUA handle representing a call is mapped as follows:
 *       - A SIPMediaChannel has a pointer to a call NUA handle, which may
 *         start as NULL.
 *       - A call NUA handle has hmagic, which is either a pointer to a
 *         SIPMediaChannel, or NULL.
 *       - When the media channel is created because of an incoming call,
 *         its NUA handle is initialized to the call's NUA handle
 *       - When the media channel is created by user request (for an outgoing
 *         call), its NUA handle is initially NULL, then is set to the call's
 *         NUA handle once the call actually starts
 *
 *       In either case, as soon as the SIPMediaChannel's NUA handle becomes
 *       non-NULL, the NUA handle's hmagic is set to the SIPMediaChannel.
 *
 *       The NUA handle survives at least as long as the SIPMediaChannel.
 *       When the SIPMediaChannel is closed, the NUA handle's hmagic is set
 *       to the special value SIP_NH_EXPIRED.
 */

#define NUA_MAGIC_T      struct _SIPConnectionSofia
#define SU_ROOT_MAGIC_T  struct _SIPConnectionManager
#define SU_TIMER_ARG_T   struct _SIPConnection
#define NUA_HMAGIC_T     void

/* a magical distinct value for nua_hmagic_t */
extern void *_sip_nh_expired;
#define SIP_NH_EXPIRED (_sip_nh_expired)

#include <sofia-sip/nua.h>
#include <sofia-sip/su.h>
#include <sofia-sip/sl_utils.h>
#include <sofia-sip/su_glib.h>
#include <sofia-sip/sdp.h>
#include <sofia-sip/tport_tag.h>
#include <sofia-sip/stun_tag.h>
#include <sofia-sip/sresolv.h>

#include "sip-connection.h"

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

G_END_DECLS

#endif /* #ifndef __SIP_CONNECTION_HELPERS_H__*/
