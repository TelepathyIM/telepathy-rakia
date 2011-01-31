/*
 * sofia-decls.h - A header file to pull in Sofia APIs
 * Copyright (C) 2006-2009 Nokia Corporation
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
 *       TpsipConnection is used as the context pointer.
 *       See {top}/docs/design.txt for further information.
 *
 *       Each NUA handle managed by Telepathy-SofiaSIP is bound to an object
 *       implementing TpsipEventTarget. The managed NUA handle survives at
 *       least as long as the bound object. When the object is destroyed,
 *       the handle is bound to a special end-of-life event handler object.
 *       Thus, any NUA handle has either the magic value of NULL, or
 *       pointing to an event target object attached to this handle.
 */

#define NUA_MAGIC_T      struct _TpsipBaseConnection
#define NUA_HMAGIC_T     struct _TpsipEventTarget
#define SU_ROOT_MAGIC_T  struct _TpsipConnectionManager
#define SU_TIMER_ARG_T   struct _TpsipBaseConnection
#define SU_WAKEUP_ARG_T  void

#define TPSIP_DEFAULT_STUN_PORT 3478

/* Maximum defer timeout for deferrable Sofia timers */
#define TPSIP_DEFER_TIMEOUT 30

#include <sofia-sip/nua.h>
#include <sofia-sip/su.h>

#endif /* _TPSIP_SOFIA_DECLS_H_*/
