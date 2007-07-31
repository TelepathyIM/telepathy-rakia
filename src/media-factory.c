/* 
 * media-factory.c - Media channel factory for SIP connection manager
 * Copyright (C) 2007 Collabora Ltd.
 * Copyright (C) 2007 Nokia Corporation
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
 * License along with this library; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA. 
 */

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/interfaces.h>
#include <string.h>
#include "media-factory.h"
#include "sip-connection.h"

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"

static void factory_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (SIPMediaFactory, sip_media_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      factory_iface_init))

enum
{
  PROP_CONNECTION = 1,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  LAST_PROPERTY
};

typedef struct _SIPMediaFactoryPrivate SIPMediaFactoryPrivate;
struct _SIPMediaFactoryPrivate
{
  /* unreferenced (since it owns this factory) */
  SIPConnection *conn;
  /* array of referenced (SIPMediaChannel *) */
  GPtrArray *channels;
  /* for unique channel object paths, currently always increments */
  guint channel_index;
  /* g_strdup'd gchar *sessionid => unowned SIPMediaChannel *chan */
  GHashTable *session_chans;

  gchar *stun_server;
  guint16 stun_port;

  gboolean dispose_has_run;
};

#define SIP_MEDIA_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), SIP_TYPE_MEDIA_FACTORY, SIPMediaFactoryPrivate))

static void
sip_media_factory_init (SIPMediaFactory *fac)
{
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  priv->conn = NULL;
  priv->channels = g_ptr_array_sized_new (1);
  priv->channel_index = 0;
  priv->session_chans = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
  priv->dispose_has_run = FALSE;
}

static void
sip_media_factory_dispose (GObject *object)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (object);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));

  g_assert (priv->channels == NULL);
  g_assert (priv->session_chans == NULL);

  g_free (priv->stun_server);

  if (G_OBJECT_CLASS (sip_media_factory_parent_class)->dispose)
    G_OBJECT_CLASS (sip_media_factory_parent_class)->dispose (object);
}

static void
sip_media_factory_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (object);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_STUN_SERVER:
      g_value_set_string (value, priv->stun_server);
      break;
    case PROP_STUN_PORT:
      g_value_set_uint (value, priv->stun_port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
sip_media_factory_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (object);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_STUN_SERVER:
      g_free (priv->stun_server);
      priv->stun_server = g_value_dup_string (value);
      break;
    case PROP_STUN_PORT:
      priv->stun_port = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
sip_media_factory_class_init (SIPMediaFactoryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (SIPMediaFactoryPrivate));

  object_class->get_property = sip_media_factory_get_property;
  object_class->set_property = sip_media_factory_set_property;
  object_class->dispose = sip_media_factory_dispose;

  param_spec = g_param_spec_object ("connection", "SIPConnection object",
                                    "SIP connection that owns this media "
                                    "channel factory",
                                    SIP_TYPE_CONNECTION,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("stun-server", "STUN server address",
                                    "STUN server address",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
                                  "STUN port.",
                                  0, G_MAXUINT16, SIP_DEFAULT_STUN_PORT,
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);
}

static void
unref_one (gpointer data, gpointer user_data)
{
  g_object_unref (data);
}

static void
sip_media_factory_close_all (TpChannelFactoryIface *iface)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (iface);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);
  GHashTable *session_chans;
  GPtrArray *channels;

  channels = priv->channels;
  priv->channels = NULL;
  if (channels)
    {
      g_ptr_array_foreach (channels, unref_one, NULL);
      g_ptr_array_free (channels, TRUE);
    }

  session_chans = priv->session_chans;
  priv->session_chans = NULL;
  if (session_chans)
    g_hash_table_destroy (session_chans);
}

static void
sip_media_factory_connecting (TpChannelFactoryIface *iface)
{
}

static void
sip_media_factory_connected (TpChannelFactoryIface *iface)
{
}

static void
sip_media_factory_disconnected (TpChannelFactoryIface *iface)
{
}

struct _ForeachData
{
  TpChannelFunc foreach;
  gpointer user_data;
};

static void
_foreach_slave (gpointer channel, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *)user_data;
  TpChannelIface *chan = TP_CHANNEL_IFACE (channel);

  data->foreach (chan, data->user_data);
}

static void
sip_media_factory_foreach (TpChannelFactoryIface *iface,
                          TpChannelFunc foreach,
                          gpointer user_data)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (iface);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  struct _ForeachData data = { foreach, user_data };

  g_ptr_array_foreach (priv->channels, _foreach_slave, &data);
}

static gboolean
hash_key_is (gpointer key, gpointer value, gpointer user_data)
{
  return (value == user_data);
}

/**
 * channel_closed:
 *
 * Signal callback for when a media channel is closed. Removes the references
 * that #SIPMediaFactory holds to them.
 */
static void
channel_closed (SIPMediaChannel *chan, gpointer user_data)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (user_data);
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  if (priv->session_chans)
    {
      g_hash_table_foreach_remove (priv->session_chans, hash_key_is, chan);
    }
  if (priv->channels)
    {
      g_ptr_array_remove_fast (priv->channels, chan);
      g_object_unref (chan);
    }
}

/**
 * new_media_channel
 *
 * Creates a new empty SIPMediaChannel.
 */
SIPMediaChannel *
sip_media_factory_new_channel (SIPMediaFactory *fac,
                               gpointer request,
                               TpHandleType handle_type,
                               TpHandle handle,
                               GError **error)
{
  SIPMediaFactoryPrivate *priv;
  SIPMediaChannel *chan;
  TpBaseConnection *conn;
  gchar *object_path;
  const gchar *nat_traversal = "none";

  g_assert (SIP_IS_MEDIA_FACTORY (fac));

  priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);
  conn = (TpBaseConnection *)priv->conn;

  object_path = g_strdup_printf ("%s/MediaChannel%u", conn->object_path,
      priv->channel_index++);

  DEBUG("channel object path %s", object_path);

  if (priv->stun_server != NULL)
    {
      nat_traversal = "stun";
    }

  chan = g_object_new (SIP_TYPE_MEDIA_CHANNEL,
                       "connection", priv->conn,
                       "factory", fac,
                       "object-path", object_path,
                       "nat-traversal", nat_traversal,
                       NULL);

  g_free (object_path);

  if (priv->stun_server != NULL)
    {
      g_object_set ((GObject *) chan, "stun-server", priv->stun_server, NULL);
      if (priv->stun_port != 0)
        g_object_set ((GObject *) chan, "stun-port", priv->stun_port, NULL);
    }

  if (handle_type == TP_HANDLE_TYPE_CONTACT)
    {
      GArray *contacts;
      gboolean added;
#if 0
      TpHandle self_handle;
      if (!tp_group_mixin_get_self_handle (G_OBJECT (chan),
                                           &self_handle,
                                           error))
        goto err;
      if (handle == self_handle)
        {
          g_error_set (error, TP_ERROR, TP_ERROR_INVALID_HANDLE, "Cannot call self");
          goto err;
        }
#endif
      contacts = g_array_sized_new (FALSE, FALSE, sizeof (TpHandle), 1);
      g_array_append_val (contacts, handle);
      added = tp_group_mixin_add_members (G_OBJECT (chan),
                                          contacts,
                                          "", error); 
      g_array_free (contacts, TRUE);
      if (!added)
        goto err;
    }

  g_signal_connect (chan, "closed", (GCallback) channel_closed, fac);

  g_ptr_array_add (priv->channels, chan);

  tp_channel_factory_iface_emit_new_channel (fac, (TpChannelIface *)chan,
      request);

  return chan;

err:
  g_object_unref (chan);
  return NULL;
}

static TpChannelFactoryRequestStatus
sip_media_factory_request (TpChannelFactoryIface *iface,
                          const gchar *chan_type,
                          TpHandleType handle_type,
                          TpHandle handle,
                          gpointer request,
                          TpChannelIface **ret,
                          GError **error)
{
  SIPMediaFactory *fac = SIP_MEDIA_FACTORY (iface);
  TpChannelIface *chan;

  if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }

  chan = (TpChannelIface *) sip_media_factory_new_channel (fac,
                                                           request,
                                                           handle_type,
                                                           handle,
                                                           error);
  if (chan != NULL)
    {
      *ret = chan;
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }
  else
    {
      return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;
    }
}

const gchar *
sip_media_factory_session_id_allocate (SIPMediaFactory *fac)
{
  SIPMediaFactoryPrivate *priv;
  guint32 val;
  gchar *sid = NULL;
  gboolean unique = FALSE;

  g_assert (SIP_IS_MEDIA_FACTORY (fac));
  priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  while (!unique)
    {
      gpointer k, v;

      val = g_random_int_range (1000000, G_MAXINT);

      g_free (sid);
      sid = g_strdup_printf ("%u", val);

      unique = !g_hash_table_lookup_extended (priv->session_chans,
                                              sid, &k, &v);
    }

  g_hash_table_insert (priv->session_chans, sid, NULL);

  return (const gchar *) sid;
}

void
sip_media_factory_session_id_register (SIPMediaFactory *fac,
                                       const gchar *sid,
                                       gpointer channel)
{
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: binding sid %s to %p", G_STRFUNC, sid, channel);

  g_hash_table_insert (priv->session_chans, g_strdup (sid), channel);
}

void
sip_media_factory_session_id_unregister (SIPMediaFactory *fac,
                                         const gchar *sid)
{
  SIPMediaFactoryPrivate *priv = SIP_MEDIA_FACTORY_GET_PRIVATE (fac);

  g_debug ("%s: unbinding sid %s", G_STRFUNC, sid);

  /* FIXME: this leaks the strings, as a way of marking that a SID has been
   * used in this process' lifetime. Surely there's something better
   * we can do?
   */
  g_hash_table_insert (priv->session_chans, g_strdup (sid), NULL);
}

static void
factory_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

#define IMPLEMENT(x) klass->x = sip_media_factory_##x
  IMPLEMENT(close_all);
  IMPLEMENT(foreach);
  IMPLEMENT(request);
  IMPLEMENT(connecting);
  IMPLEMENT(connected);
  IMPLEMENT(disconnected);
#undef IMPLEMENT
}
