/*
 * sip-sofia-decls.h - A header file to pull in Sofia APIs
 * Copyright (C) 2006-2008 Nokia Corporation
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

#ifndef _TPSIP_SOFIA_DECLS_H_
#define _TPSIP_SOFIA_DECLS_H_

/* note: As one Sofia-SIP NUA instance is created per SIP connection,
 *       TpsipConnection is used as the primary context pointer. See
 *       {top}/docs/design.txt for further information.
 *
 *       Each NUA handle representing a call is mapped as follows:
 *       - A TpsipMediaChannel has a pointer to a call NUA handle, which may
 *         start as NULL.
 *       - A call NUA handle has hmagic, which is either a pointer to a
 *         TpsipMediaChannel, or NULL.
 *       - When the media channel is created because of an incoming call,
 *         its NUA handle is initialized to the call's NUA handle
 *       - When the media channel is created by user request (for an outgoing
 *         call), its NUA handle is initially NULL, then is set to the call's
 *         NUA handle once the call actually starts
 *
 *       In either case, as soon as the TpsipMediaChannel's NUA handle becomes
 *       non-NULL, the NUA handle's hmagic is set to the TpsipMediaChannel.
 *
 *       The NUA handle survives at least as long as the TpsipMediaChannel.
 *       When the TpsipMediaChannel is closed, the NUA handle's hmagic is set
 *       to the special value TPSIP_NH_EXPIRED.
 */

#define NUA_MAGIC_T      struct _TpsipConnectionSofia
#define SU_ROOT_MAGIC_T  struct _TpsipConnectionManager
#define SU_TIMER_ARG_T   struct _TpsipConnection
#define NUA_HMAGIC_T     void

#include <sofia-sip/nua.h>
#include <sofia-sip/su.h>
#include <sofia-sip/su_glib.h>
#include <sofia-sip/sdp.h>
#include <sofia-sip/tport_tag.h>
#include <sofia-sip/sresolv.h>

G_BEGIN_DECLS

/* a magical distinct value for nua_hmagic_t */
extern NUA_HMAGIC_T * const _tpsip_nh_expired;
#define TPSIP_NH_EXPIRED (_tpsip_nh_expired)

G_END_DECLS

#endif /* _TPSIP_SOFIA_DECLS_H_*/
