/* 
 * text-factory.c - Text channel factory for SIP connection manager
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007-2008 Nokia Corporation
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

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/interfaces.h>
#include <string.h>
#include "text-factory.h"

#define DEBUG_FLAG TPSIP_DEBUG_IM
#include "debug.h"


static void factory_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (TpsipTextFactory, tpsip_text_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      factory_iface_init))

enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _TpsipTextFactoryPrivate TpsipTextFactoryPrivate;
struct _TpsipTextFactoryPrivate
{
  TpsipConnection *conn;
  /* guint handle => TpsipTextChannel *channel */
  GHashTable *channels;

  gboolean dispose_has_run;
};

#define TPSIP_TEXT_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TPSIP_TYPE_TEXT_FACTORY, TpsipTextFactoryPrivate))

static void
tpsip_text_factory_init (TpsipTextFactory *fac)
{
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  priv->conn = NULL;
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->dispose_has_run = FALSE;
}

static void
tpsip_text_factory_dispose (GObject *object)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (object);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (tpsip_text_factory_parent_class)->dispose)
    G_OBJECT_CLASS (tpsip_text_factory_parent_class)->dispose (object);
}

static void
tpsip_text_factory_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (object);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
tpsip_text_factory_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (object);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
tpsip_text_factory_class_init (TpsipTextFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (TpsipTextFactoryPrivate));

  object_class->get_property = tpsip_text_factory_get_property;
  object_class->set_property = tpsip_text_factory_set_property;
  object_class->dispose = tpsip_text_factory_dispose;

  param_spec = g_param_spec_object ("connection", "TpsipConnection object",
      "SIP connection that owns this text channel factory",
      TPSIP_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static gboolean
tpsip_text_factory_close_one (gpointer key,
                            gpointer data,
                            gpointer user_data)
{
  tpsip_text_channel_close (TPSIP_TEXT_CHANNEL(data));
  return TRUE;
}

static void
tpsip_text_factory_close_all (TpChannelFactoryIface *iface)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (iface);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);
  GHashTable *channels;

  if (!priv->channels)
    return;

  channels = priv->channels;
  priv->channels = NULL;

  g_hash_table_foreach_remove (channels, tpsip_text_factory_close_one, NULL);
}

static void
tpsip_text_factory_connecting (TpChannelFactoryIface *iface)
{
}

static void
tpsip_text_factory_connected (TpChannelFactoryIface *iface)
{
}

static void
tpsip_text_factory_disconnected (TpChannelFactoryIface *iface)
{
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *)user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (value);

  data->foreach (chan, data->user_data);
}

static void
tpsip_text_factory_foreach (TpChannelFactoryIface *iface,
                          TpChannelFunc foreach,
                          gpointer user_data)
{
  struct _ForeachData data = { foreach, user_data };
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (iface);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

/**
 * text_channel_closed_cb:
 *
 * Signal callback for when a text channel is closed. Removes the references
 * that #TpsipChannelFactory holds to them.
 */
static void
channel_closed (TpsipTextChannel *chan, gpointer user_data)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (user_data);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);
  TpHandle contact_handle;

  g_object_get (chan, "handle", &contact_handle, NULL);
  DEBUG("removing text channel with handle %u", contact_handle);

  if (priv->channels)
    g_hash_table_remove (priv->channels, GINT_TO_POINTER (contact_handle));
}

/**
 * new_text_channel
 *
 * Creates a new empty TpsipTextChannel.
 */
TpsipTextChannel *
tpsip_text_factory_new_channel (TpChannelFactoryIface *iface,
                                TpHandle handle,
                                TpHandle initiator,
                                gpointer request)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (iface);
  TpsipTextFactoryPrivate *priv;
  TpsipTextChannel *chan;
  gchar *object_path;
  TpBaseConnection *conn;

  g_assert (TPSIP_IS_TEXT_FACTORY (fac));

  priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *)(priv->conn);

  object_path = g_strdup_printf ("%s/TextChannel%u",
      conn->object_path, handle);

  g_debug ("%s: object path %s", G_STRFUNC, object_path);

  chan = g_object_new (TPSIP_TYPE_TEXT_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "initiator", initiator,
                       NULL);

  g_free (object_path);

  g_signal_connect (chan, "closed", (GCallback) channel_closed, fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *)chan,
      request);

  return chan;
}

TpsipTextChannel *
tpsip_text_factory_lookup_channel (TpChannelFactoryIface *iface,
                                 guint handle)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (iface);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);

  return (TpsipTextChannel *)g_hash_table_lookup (priv->channels,
      GUINT_TO_POINTER(handle));
}

static TpChannelFactoryRequestStatus
tpsip_text_factory_request (TpChannelFactoryIface *iface,
                            const gchar *chan_type,
                            TpHandleType handle_type,
                            guint handle,
                            gpointer request,
                            TpChannelIface **ret,
                            GError **error)
{
  TpsipTextFactory *fac = TPSIP_TEXT_FACTORY (iface);
  TpsipTextFactoryPrivate *priv = TPSIP_TEXT_FACTORY_GET_PRIVATE (fac);
  TpChannelIface *chan;
  TpChannelFactoryRequestStatus status;

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }

  if (handle_type != TP_HANDLE_TYPE_CONTACT)
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }

  status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
  chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));
  if (!chan)
    {
      TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn; 

      chan = (TpChannelIface *)tpsip_text_factory_new_channel (iface,
          handle, base_conn->self_handle, request);

      status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }
  *ret = chan;
  return status;
}

static void
factory_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

#define IMPLEMENT(x) klass->x = tpsip_text_factory_##x
  IMPLEMENT(close_all);
  IMPLEMENT(foreach);
  IMPLEMENT(request);
  IMPLEMENT(connecting);
  IMPLEMENT(connected);
  IMPLEMENT(disconnected);
#undef IMPLEMENT
}
