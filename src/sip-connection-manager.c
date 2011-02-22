/*
 * sip-connection-manager.c - Source for RakiaConnectionManager
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection-manager).
 *   @author See gabble-connection-manager.c
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-protocol.h>

#include <telepathy-glib/debug-sender.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-connection-manager.h>

#include <rakia/sofia-decls.h>
#include <sofia-sip/su_glib.h>

#include "protocol.h"
#include "sip-connection-manager.h"
#include "sip-connection.h"

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "rakia/debug.h"


G_DEFINE_TYPE(RakiaConnectionManager, rakia_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

struct _RakiaConnectionManagerPrivate
{
  su_root_t *sofia_root;
  TpDebugSender *debug_sender;
};

#define TPSIP_CONNECTION_MANAGER_GET_PRIVATE(obj) ((obj)->priv)

static void
rakia_connection_manager_init (RakiaConnectionManager *obj)
{
  RakiaConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
        TPSIP_TYPE_CONNECTION_MANAGER, RakiaConnectionManagerPrivate);
  GSource *source;

  obj->priv = priv;

  priv->sofia_root = su_glib_root_create(obj);
  su_root_threading(priv->sofia_root, 0);
  source = su_root_gsource(priv->sofia_root);
  g_source_attach(source, NULL);

  priv->debug_sender = tp_debug_sender_dup ();
  g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);

#ifdef HAVE_LIBIPHB
  su_root_set_max_defer (priv->sofia_root, TPSIP_DEFER_TIMEOUT * 1000L);
#endif
}

static void
rakia_connection_manager_constructed (GObject *object)
{
  RakiaConnectionManager *self = TPSIP_CONNECTION_MANAGER (object);
  TpBaseConnectionManager *base = (TpBaseConnectionManager *) self;
  void (*constructed) (GObject *) =
      ((GObjectClass *) rakia_connection_manager_parent_class)->constructed;
  TpBaseProtocol *protocol;

  if (constructed != NULL)
    constructed (object);

  protocol = rakia_protocol_new (self->priv->sofia_root);
  tp_base_connection_manager_add_protocol (base, protocol);
  g_object_unref (protocol);
}

static void rakia_connection_manager_finalize (GObject *object);

static void
rakia_connection_manager_class_init (RakiaConnectionManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  TpBaseConnectionManagerClass *base_class = 
    (TpBaseConnectionManagerClass *)klass;

  g_type_class_add_private (klass, sizeof (RakiaConnectionManagerPrivate));

  object_class->constructed = rakia_connection_manager_constructed;
  object_class->finalize = rakia_connection_manager_finalize;

  base_class->cm_dbus_name = "sofiasip";
}

void
rakia_connection_manager_finalize (GObject *object)
{
  RakiaConnectionManager *self = TPSIP_CONNECTION_MANAGER (object);
  RakiaConnectionManagerPrivate *priv = TPSIP_CONNECTION_MANAGER_GET_PRIVATE (self);
  GSource *source;

  source = su_root_gsource(priv->sofia_root);
  g_source_destroy(source);
  su_root_destroy(priv->sofia_root);

  if (priv->debug_sender != NULL)
    {
      g_object_unref (priv->debug_sender);
      priv->debug_sender = NULL;
    }

  rakia_debug_free ();

  G_OBJECT_CLASS (rakia_connection_manager_parent_class)->finalize (object);
}
