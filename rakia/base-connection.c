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
 * Based on rakia-connection and gabble implementation (gabble-connection).
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

#include <rakia/base-connection.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>
#include <rakia/debug.h>
#include <rakia/sofia-decls.h>
#include <stdlib.h>

struct _RakiaBaseConnectionPrivate
{
  su_root_t *sofia_root;
  /* guint: handle => owned url_t */
  GHashTable *uris;

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

G_DEFINE_TYPE_WITH_CODE (RakiaBaseConnection,
    rakia_base_connection, TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (RAKIA_TYPE_EVENT_TARGET, event_target_iface_init);
);

static void
rakia_base_connection_init (RakiaBaseConnection *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, RAKIA_TYPE_BASE_CONNECTION,
      RakiaBaseConnectionPrivate);

  self->priv->uris = g_hash_table_new_full (NULL, NULL, NULL, free);
}

static void
rakia_base_connection_constructed(GObject *object)
{
  if (G_OBJECT_CLASS(rakia_base_connection_parent_class)->constructed)
    G_OBJECT_CLASS(rakia_base_connection_parent_class)->constructed(object);
}

static void
rakia_base_connection_dispose(GObject *object)
{
  RakiaBaseConnection *self = RAKIA_BASE_CONNECTION(object);

  if (self->priv->dispose_has_run)
    return;
  self->priv->dispose_has_run = 1;

  G_OBJECT_CLASS(rakia_base_connection_parent_class)->dispose(object);
}

static void
rakia_base_connection_finalize(GObject *object)
{
  RakiaBaseConnection *self = RAKIA_BASE_CONNECTION (object);

  tp_clear_pointer (&self->priv->uris, g_hash_table_unref);

  G_OBJECT_CLASS(rakia_base_connection_parent_class)->finalize(object);
}

static void
rakia_base_connection_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  RakiaBaseConnection *self = RAKIA_BASE_CONNECTION (object);
  RakiaBaseConnectionPrivate *priv = self->priv;

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
rakia_base_connection_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  RakiaBaseConnection *self = RAKIA_BASE_CONNECTION (object);
  RakiaBaseConnectionPrivate *priv = self->priv;

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

static void
rakia_base_connection_disconnected (TpBaseConnection *base)
{
  RakiaBaseConnection *self = RAKIA_BASE_CONNECTION (base);

  /* handles are no longer meaningful */
  g_hash_table_remove_all (self->priv->uris);
}

/* -------------------------------------------------------------------------- */

static void
rakia_base_connection_class_init (RakiaBaseConnectionClass *klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  TpBaseConnectionClass *conn_class = (TpBaseConnectionClass *) klass;

  g_type_class_add_private (klass, sizeof (RakiaBaseConnectionPrivate));

  object_class->constructed = rakia_base_connection_constructed;
  object_class->dispose = rakia_base_connection_dispose;
  object_class->finalize = rakia_base_connection_finalize;
  object_class->get_property = rakia_base_connection_get_property;
  object_class->set_property = rakia_base_connection_set_property;
  conn_class->disconnected = rakia_base_connection_disconnected;

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
}

nua_handle_t *
rakia_base_connection_create_handle (RakiaBaseConnection *self,
                                     TpHandle tphandle)
{
  RakiaBaseConnectionClass *cls = RAKIA_BASE_CONNECTION_GET_CLASS (self);

  return cls->create_handle (self, tphandle);
}

void
rakia_base_connection_add_auth_handler (RakiaBaseConnection *self,
                                        RakiaEventTarget *target)
{
  RakiaBaseConnectionClass *cls = RAKIA_BASE_CONNECTION_GET_CLASS (self);

  if (cls->add_auth_handler)
    cls->add_auth_handler (self, target);
}

void
rakia_base_connection_save_event (RakiaBaseConnection *self,
                                  nua_saved_event_t ret_saved [1])
{
  nua_t *nua;

  g_object_get (self, "sofia-nua", &nua, NULL);

  nua_save_event (nua, ret_saved);
}

const url_t*
rakia_base_connection_handle_to_uri (RakiaBaseConnection *self,
    TpHandle handle)
{
  TpHandleRepoIface *repo;
  url_t *url;
  GError *error = NULL;

  repo = tp_base_connection_get_handles (TP_BASE_CONNECTION (self),
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handle_is_valid (repo, handle, &error))
    {
      DEBUG ("invalid handle %u: %s", handle, error->message);
      g_error_free (error);
      return NULL;
    }

  url = g_hash_table_lookup (self->priv->uris, GUINT_TO_POINTER (handle));

  if (url == NULL)
    {
      url = url_make (NULL, tp_handle_inspect (repo, handle));

      g_hash_table_replace (self->priv->uris, GUINT_TO_POINTER (handle),
          url);
    }

  return url;
}
