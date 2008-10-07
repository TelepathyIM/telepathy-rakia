/*
 * sip-connection-private.h - Private structures for TpsipConnection
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#ifndef __TPSIP_CONNECTION_PRIVATE_H__
#define __TPSIP_CONNECTION_PRIVATE_H__

#include <telepathy-glib/channel-factory-iface.h>

#include <tpsip/sofia-decls.h>
#include <sofia-sip/sresolv.h>

struct _TpsipConnectionPrivate
{
  su_root_t *sofia_root;
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
  gchar *alias;
  gchar *transport;
  TpsipConnectionKeepaliveMechanism keepalive_mechanism;
  gint keepalive_interval;
  gboolean discover_stun;
  gchar *stun_host;
  guint stun_port;
  gchar *local_ip_address;
  guint local_port;
  gchar *extra_auth_user;
  gchar *extra_auth_password;
  gboolean loose_routing;
  gboolean discover_binding;

  gboolean dispose_has_run;
};

/* #define TPSIP_PROTOCOL_STRING               "sip" */

#define TPSIP_CONNECTION_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_CONNECTION, TpsipConnectionPrivate))

#endif /*__TPSIP_CONNECTION_PRIVATE_H__*/
