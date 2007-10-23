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

#include "sip-connection-sofia.h"

struct _SIPConnectionPrivate
{
  SIPConnectionSofia *sofia;
  nua_t  *sofia_nua;
  su_home_t *sofia_home;
  nua_handle_t *register_op;
  sres_resolver_t *sofia_resolver;
  url_t *account_url;
  url_t *proxy_url;
  url_t *registrar_url;

  gchar *registrar_realm;

  GHashTable *auth_table;

  /* channels */
  TpChannelFactoryIface *text_factory;
  TpChannelFactoryIface *media_factory;

  gchar *address;
  gchar *auth_user;
  gchar *password;
  SIPConnectionKeepaliveMechanism keepalive_mechanism;
  gint keepalive_interval;
  gchar *http_proxy;
  gboolean discover_stun;
  gchar *stun_host;
  guint stun_port;
  gchar *local_ip_address;
  guint local_port;
  gchar *extra_auth_user;
  gchar *extra_auth_password;
  gboolean discover_binding;

  gboolean dispose_has_run;
};

#define SIP_PROTOCOL_STRING               "sip"

#define SIP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_CONNECTION, SIPConnectionPrivate))

#endif /*__SIP_CONNECTION_PRIVATE_H__*/
