/*
 * sip-connection-sofia.h - Header for TpsipConnection Sofia event handling
 * Copyright (C) 2006-2008 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
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

#ifndef __TPSIP_CONNECTION_SOFIA_H__
#define __TPSIP_CONNECTION_SOFIA_H__

#include "sip-sofia-decls.h"
#include "sip-connection.h"

G_BEGIN_DECLS

typedef struct _TpsipConnectionSofia {
  /* The owner SIP connection object */
  TpsipConnection *conn;
  /* Event loop root for Sofia-SIP */
  su_root_t *sofia_root;
} TpsipConnectionSofia;

TpsipConnectionSofia * tpsip_connection_sofia_new (TpsipConnection *conn);
void tpsip_connection_sofia_destroy (TpsipConnectionSofia *conn);

/**
 * Callback for events delivered by the SIP stack.
 *
 * See libsofia-sip-ua/nua/nua.h documentation.
 */
void tpsip_connection_sofia_callback(nua_event_t event,
				   int status, char const *phrase,
				   nua_t *nua, TpsipConnectionSofia *sofia,
				   nua_handle_t *nh, nua_hmagic_t *op, sip_t const *sip,
				   tagi_t tags[]);

G_END_DECLS

#endif /* #ifndef __TPSIP_CONNECTION_SOFIA_H__*/
