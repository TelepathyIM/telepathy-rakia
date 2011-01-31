/*
 * sip-base-connection.c - source for SipBaseConnection
 * Copyright (C) 2011 Nokia Corporation.
 * Copyright (C) 2005-2007 Collabora Ltd.
 * Copyright (C) 2005-2011 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *   @author Pekka Pessi <pekka.pessi@nokia.com>
 *
 * Based on tpsip-connection and gabble implementation (gabble-connection).
 *   @author See gabble-connection.c
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

#include <tpsip/base-connection.h>

#include <telepathy-glib/telepathy-glib.h>
#include <tpsip/sofia-decls.h>

struct _TpsipBaseConnectionPrivate
{
  su_root_t *sofia_root;

  unsigned dispose_has_run:1; unsigned :0;
};

enum {
  PROP_NONE,
  PROP_SOFIA_ROOT,
  PROP_SOFIA_NUA,
};

/* ---------------------------------------------------------------------- */
/* GObject implementation */

static void event_target_iface_init (gpointer iface, gpointer data) {}

G_DEFINE_TYPE_WITH_CODE (TpsipBaseConnection,
    tpsip_base_connection, TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
        tp_contacts_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TPSIP_TYPE_EVENT_TARGET, event_target_iface_init);
);

static void
tpsip_base_connection_init (TpsipBaseConnection *self)
{
  GObject *object = G_OBJECT (self);
  TpBaseConnection *base = TP_BASE_CONNECTION (self);

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TPSIP_TYPE_BASE_CONNECTION,
      TpsipBaseConnectionPrivate);

  tp_contacts_mixin_init (object,
      G_STRUCT_OFFSET (TpsipBaseConnection, contacts_mixin));

  /* org.freedesktop.Telepathy.Connection attributes */
  tp_base_connection_register_with_contacts_mixin (base);
}

static void
tpsip_base_connection_constructed(GObject *object)
{
  if (G_OBJECT_CLASS(tpsip_base_connection_parent_class)->constructed)
    G_OBJECT_CLASS(tpsip_base_connection_parent_class)->constructed(object);
}

static void
tpsip_base_connection_dispose(GObject *object)
{
  TpsipBaseConnection *self = TPSIP_BASE_CONNECTION(object);

  if (self->priv->dispose_has_run)
    return;
  self->priv->dispose_has_run = 1;

  G_OBJECT_CLASS(tpsip_base_connection_parent_class)->dispose(object);
}

void
tpsip_base_connection_finalize(GObject *object)
{
  G_OBJECT_CLASS(tpsip_base_connection_parent_class)->finalize(object);
}

static void
tpsip_base_connection_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  TpsipBaseConnection *self = TPSIP_BASE_CONNECTION (object);
  TpsipBaseConnectionPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_SOFIA_ROOT:
      priv->sofia_root = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tpsip_base_connection_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  TpsipBaseConnection *self = TPSIP_BASE_CONNECTION (object);
  TpsipBaseConnectionPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_SOFIA_ROOT:
      g_value_set_pointer (value, priv->sofia_root);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/* -------------------------------------------------------------------------- */

static void
tpsip_base_connection_class_init (TpsipBaseConnectionClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (TpsipBaseConnectionPrivate));

  object_class->constructed = tpsip_base_connection_constructed;
  object_class->dispose = tpsip_base_connection_dispose;
  object_class->finalize = tpsip_base_connection_finalize;
  object_class->get_property = tpsip_base_connection_get_property;
  object_class->set_property = tpsip_base_connection_set_property;

  g_object_class_install_property (object_class,
      PROP_SOFIA_ROOT,
      g_param_spec_pointer ("sofia-root",
          "Sofia-SIP root",
          "The root object for Sofia-SIP",
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_SOFIA_NUA,
      g_param_spec_pointer ("sofia-nua",
          "Sofia-SIP UA",
          "The UA object for Sofia-SIP",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  tp_contacts_mixin_class_init (object_class,
      G_STRUCT_OFFSET(TpsipBaseConnectionClass, contacts_mixin_class));
}

nua_handle_t *
tpsip_base_connection_create_handle (TpsipBaseConnection *self,
                                     TpHandle tphandle)
{
  TpsipBaseConnectionClass *cls = TPSIP_BASE_CONNECTION_GET_CLASS (self);

  return cls->create_handle (self, tphandle);
}

void
tpsip_base_connection_add_auth_handler (TpsipBaseConnection *self,
                                        TpsipEventTarget *target)
{
  TpsipBaseConnectionClass *cls = TPSIP_BASE_CONNECTION_GET_CLASS (self);

  if (cls->add_auth_handler)
    cls->add_auth_handler (self, target);
}

void
tpsip_base_connection_save_event (TpsipBaseConnection *self,
                                  nua_saved_event_t ret_saved [1])
{
  nua_t *nua;

  g_object_get (self, "sofia-nua", &nua, NULL);

  nua_save_event (nua, ret_saved);
}
