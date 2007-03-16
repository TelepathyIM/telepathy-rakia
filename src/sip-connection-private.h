/* 
 * Copyright (C) 2005-2007 Collabora Ltd. and Nokia Corporation
 *
 * sip-connection-private.h- Private structues for SIPConnection
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 */

#ifndef __SIP_CONNECTION_PRIVATE_H__
#define __SIP_CONNECTION_PRIVATE_H__

#include <telepathy-glib/channel-factory-iface.h>

#include "sip-media-channel.h"

enum {
  SIP_NUA_SHUTDOWN_NOT_STARTED = 0,
  SIP_NUA_SHUTDOWN_STARTED,
  SIP_NUA_SHUTDOWN_DONE
};

struct _SIPConnectionPrivate
{
  gchar *requested_address;
  gboolean dispose_has_run;
  
  nua_t  *sofia_nua;
  su_root_t *sofia_root;
  su_home_t *sofia_home;
  nua_handle_t *register_op;

  gint sofia_shutdown;

  /* channels */
  TpChannelFactoryIface *text_factory;
  TpChannelFactoryIface *media_factory;

  gchar *address;
  gchar *password;
  gchar *proxy;
  gchar *registrar;
  SIPConnectionKeepaliveMechanism keepalive_mechanism;
  gint keepalive_interval;
  gchar *http_proxy;
  gchar *stun_server;
  gchar *extra_auth_user;
  gchar *extra_auth_password;
  gboolean discover_binding;
};

#define SIP_PROTOCOL_STRING               "sip"

#define SIP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_CONNECTION, SIPConnectionPrivate))

/* signal enum */
enum
{
    DISCONNECTED,
    LAST_SIGNAL
};

extern guint sip_conn_signals[LAST_SIGNAL];

#endif /*__SIP_CONNECTION_PRIVATE_H__*/
