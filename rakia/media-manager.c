/*
 * media-manager.c - Media channel manager for SIP connection manager
 * Copyright (C) 2007-2008 Collabora Ltd.
 * Copyright (C) 2007-2010 Nokia Corporation
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

#include "rakia/media-manager.h"

#include <string.h>

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#ifdef ENABLE_CHANNEL_TYPE_CALL
#include "rakia/call-channel.h"
#else
#include "rakia/media-channel.h"
#endif

#include "rakia/base-connection.h"
#include "rakia/handles.h"

#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG RAKIA_DEBUG_CONNECTION
#include "rakia/debug.h"

static void channel_manager_iface_init (gpointer, gpointer);
static void rakia_media_manager_constructed (GObject *object);
static void rakia_media_manager_close_all (RakiaMediaManager *fac);

G_DEFINE_TYPE_WITH_CODE (RakiaMediaManager, rakia_media_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
        channel_manager_iface_init))

enum
{
  PROP_CONNECTION = 1,
  PROP_STUN_SERVER,
  PROP_STUN_PORT,
  LAST_PROPERTY
};

typedef struct _RakiaMediaManagerPrivate RakiaMediaManagerPrivate;
struct _RakiaMediaManagerPrivate
{
  /* unreferenced (since it owns this manager) */
  TpBaseConnection *conn;
  /* array of referenced (RakiaCallChannel *) */
  GPtrArray *channels;
  /* for unique channel object paths, currently always increments */
  guint channel_index;

  gulong status_changed_id;
  gulong invite_received_id;

  gchar *stun_server;
  guint16 stun_port;

  gboolean dispose_has_run;
};

#define RAKIA_MEDIA_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RAKIA_TYPE_MEDIA_MANAGER, RakiaMediaManagerPrivate))

static void
rakia_media_manager_init (RakiaMediaManager *fac)
{
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

  priv->conn = NULL;
  priv->channels = g_ptr_array_new_with_free_func (g_object_unref);
  priv->channel_index = 0;
  priv->dispose_has_run = FALSE;
}

static void
rakia_media_manager_dispose (GObject *object)
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (object);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  rakia_media_manager_close_all (fac);
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (rakia_media_manager_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_media_manager_parent_class)->dispose (object);
}

static void
rakia_media_manager_finalize (GObject *object)
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (object);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

  g_free (priv->stun_server);
}

static void
rakia_media_manager_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (object);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

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
rakia_media_manager_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (object);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

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
rakia_media_manager_class_init (RakiaMediaManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (RakiaMediaManagerPrivate));

  object_class->constructed = rakia_media_manager_constructed;
  object_class->get_property = rakia_media_manager_get_property;
  object_class->set_property = rakia_media_manager_set_property;
  object_class->dispose = rakia_media_manager_dispose;
  object_class->finalize = rakia_media_manager_finalize;

  param_spec = g_param_spec_object ("connection",
      "RakiaBaseConnection object",
      "SIP connection that owns this media channel manager",
      RAKIA_TYPE_BASE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("stun-server", "STUN server address",
      "STUN server address",
      NULL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVER, param_spec);

  param_spec = g_param_spec_uint ("stun-port", "STUN port",
      "STUN port.",
      0, G_MAXUINT16,
      RAKIA_DEFAULT_STUN_PORT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_PORT, param_spec);
}

static void
rakia_media_manager_close_all (RakiaMediaManager *fac)
{
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (priv->channels != NULL)
    {
      GPtrArray *channels;

      channels = priv->channels;
      priv->channels = NULL;

      g_ptr_array_unref (channels);
    }
}

/**
 * media_channel_closed_cb:
 * Signal callback for when a media channel is closed. Removes the references
 * that #RakiaMediaManager holds to them.
 */
#ifdef ENABLE_CHANNEL_TYPE_CALL
static void
media_channel_closed_cb (RakiaCallChannel *chan, gpointer user_data)
#else
static void
media_channel_closed_cb (RakiaMediaChannel *chan, gpointer user_data)
#endif
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (user_data);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

  tp_channel_manager_emit_channel_closed_for_object (fac,
      TP_EXPORTABLE_CHANNEL (chan));

  if (priv->channels)
    {
      g_ptr_array_remove_fast (priv->channels, chan);
    }
}

/**
 * new_media_channel
 *
 * Creates a new empty RakiaMediaChannel.
 */
#ifdef ENABLE_CHANNEL_TYPE_CALL
static RakiaCallChannel *
#else
static RakiaMediaChannel *
#endif
new_media_channel (RakiaMediaManager *fac,
                   TpHandle initiator,
                   TpHandle maybe_peer,
                   GHashTable *request_properties)
{
  RakiaMediaManagerPrivate *priv;
#ifdef ENABLE_CHANNEL_TYPE_CALL
  RakiaCallChannel *chan = NULL;
#else
  RakiaMediaChannel *chan = NULL;
#endif
  gchar *object_path;
  const gchar *nat_traversal = "none";
  gboolean initial_audio = FALSE;
  gboolean initial_video = FALSE;
  gboolean immutable_streams = FALSE;
  const gchar *dtmf_initial_tones = NULL;
#ifdef ENABLE_CHANNEL_TYPE_CALL
  const gchar *initial_audio_name = NULL;
  const gchar *initial_video_name = NULL;
  TpStreamTransportType initial_transport = TP_STREAM_TRANSPORT_TYPE_RAW_UDP;
#endif

  g_assert (initiator != 0);

  priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);

#ifdef ENABLE_CHANNEL_TYPE_CALL
  object_path = g_strdup_printf ("%s/CallChannel%u", priv->conn->object_path,
      priv->channel_index++);
#else
  object_path = g_strdup_printf ("%s/MediaChannel%u", priv->conn->object_path,
      priv->channel_index++);
#endif

  DEBUG("channel object path %s", object_path);

  if (request_properties != NULL)
    {
#ifdef ENABLE_CHANNEL_TYPE_CALL
      initial_audio = tp_asv_get_boolean (request_properties,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, NULL);
      initial_video = tp_asv_get_boolean (request_properties,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, NULL);
      initial_audio_name = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME);
      initial_video_name = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME);
      dtmf_initial_tones = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_INTERFACE_DTMF_INITIAL_TONES);
      initial_transport = tp_asv_get_uint32 (request_properties,
          TP_PROP_CHANNEL_TYPE_CALL_INITIAL_TRANSPORT, NULL);

#else
      initial_audio = tp_asv_get_boolean (request_properties,
          TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, NULL);
      initial_video = tp_asv_get_boolean (request_properties,
          TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO, NULL);
      dtmf_initial_tones = tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_INTERFACE_DTMF_INITIAL_TONES);
#endif
    }

  g_object_get (priv->conn,
      "immutable-streams", &immutable_streams,
      NULL);

  if (priv->stun_server != NULL)
    {
      nat_traversal = "stun";
    }

#ifdef ENABLE_CHANNEL_TYPE_CALL
  chan = g_object_new (RAKIA_TYPE_CALL_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", maybe_peer,
                       "initiator-handle", initiator,
                       "initial-audio", initial_audio,
                       "initial-video", initial_video,
                       "initial-audio-name", initial_audio_name,
                       "initial-video-name", initial_video_name,
                       "initial-transport", initial_transport,
                       "immutable-contents", immutable_streams,
                       "initial-tones", dtmf_initial_tones,
                       "nat-traversal", nat_traversal,
                       NULL);
#else
  chan = g_object_new (RAKIA_TYPE_MEDIA_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", maybe_peer,
                       "initiator", initiator,
                       "initial-audio", initial_audio,
                       "initial-video", initial_video,
                       "immutable-streams", immutable_streams,
                       "nat-traversal", nat_traversal,
                       "initial-tones", dtmf_initial_tones,
                       NULL);
#endif

  g_free (object_path);

  if (priv->stun_server != NULL)
    {
      g_object_set ((GObject *) chan, "stun-server", priv->stun_server, NULL);
      if (priv->stun_port != 0)
        g_object_set ((GObject *) chan, "stun-port", priv->stun_port, NULL);
    }

  g_signal_connect (chan, "closed", G_CALLBACK (media_channel_closed_cb), fac);

  g_ptr_array_add (priv->channels, chan);

  return chan;
}

#ifndef ENABLE_CHANNEL_TYPE_CALL
static void
incoming_call_cb (RakiaMediaChannel *channel,
                  RakiaMediaManager *fac)
{
  g_signal_handlers_disconnect_by_func (channel,
      G_CALLBACK (incoming_call_cb), fac);
  tp_channel_manager_emit_new_channel (fac,
      TP_EXPORTABLE_CHANNEL (channel), NULL);
}
#endif

static gboolean
rakia_nua_i_invite_cb (TpBaseConnection    *conn,
                       const RakiaNuaEvent *ev,
                       tagi_t               tags[],
                       RakiaMediaManager   *fac)
{
#ifndef ENABLE_CHANNEL_TYPE_CALL
  RakiaMediaChannel *channel;
#endif
  TpHandle handle;

  /* figure out a handle for the identity */

  handle = rakia_handle_by_requestor (conn, ev->sip);
  if (!handle)
    {
      MESSAGE ("incoming INVITE with invalid sender information");
      nua_respond (ev->nua_handle, 400, "Invalid From address", TAG_END());
      return TRUE;
    }

  DEBUG("Got incoming invite from <%s>",
        rakia_handle_inspect (conn, handle));

  if (handle == conn->self_handle)
    {
      DEBUG("cannot handle calls from self");
      nua_respond (ev->nua_handle, 501, "Calls from self are not supported", TAG_END());
      return TRUE;
    }

#ifdef ENABLE_CHANNEL_TYPE_CALL
  new_media_channel (fac, handle, handle, NULL);
#else
  channel = new_media_channel (fac, handle, handle, NULL);
#endif

  rakia_handle_unref (conn, handle);

#ifndef ENABLE_CHANNEL_TYPE_CALL
  /* We delay emission of NewChannel(s) until we have the data on
   * initial media */
  g_signal_connect (channel, "incoming-call",
      G_CALLBACK (incoming_call_cb), fac);

  rakia_media_channel_attach_to_nua_handle (channel, ev->nua_handle);
#endif

  return TRUE;
}

static void
connection_status_changed_cb (RakiaBaseConnection *conn,
                              guint status,
                              guint reason,
                              RakiaMediaManager *self)
{
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (self);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTED:

      priv->invite_received_id = g_signal_connect (conn,
          "nua-event::nua_i_invite",
          G_CALLBACK (rakia_nua_i_invite_cb), self);

      break;
    case TP_CONNECTION_STATUS_DISCONNECTED:

      rakia_media_manager_close_all (self);

      if (priv->invite_received_id != 0)
        {
          g_signal_handler_disconnect (conn, priv->invite_received_id);
          priv->invite_received_id = 0;
        }

      break;
    default:
      break;
    }
}

static void
rakia_media_manager_constructed (GObject *object)
{
  RakiaMediaManager *self = RAKIA_MEDIA_MANAGER (object);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (self);
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_media_manager_parent_class);

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (object);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}

static void
rakia_media_manager_foreach_channel (TpChannelManager *manager,
                                     TpExportableChannelFunc foreach,
                                     gpointer user_data)
{
  RakiaMediaManager *fac = RAKIA_MEDIA_MANAGER (manager);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (fac);
  guint i;

  for (i = 0; i < priv->channels->len; i++)
    {
      TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (
          g_ptr_array_index (priv->channels, i));

      foreach (channel, user_data);
    }
}

static const gchar * const media_channel_fixed_properties[] = {
    TP_PROP_CHANNEL_CHANNEL_TYPE,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
    NULL
};

static const gchar * const named_channel_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE,
    TP_PROP_CHANNEL_TARGET_ID,
#ifdef ENABLE_CHANNEL_TYPE_CALL
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO_NAME,
    TP_PROP_CHANNEL_TYPE_CALL_INITIAL_TRANSPORT,
#else
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO,
#endif
    TP_PROP_CHANNEL_INTERFACE_DTMF_INITIAL_TONES,
    NULL
};

/* not advertised in type_foreach_channel_class - can only be requested with
 * RequestChannel, not with CreateChannel/EnsureChannel */
static const gchar * const anon_channel_allowed_properties[] = {
    NULL
};

static void
rakia_media_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value, *handle_type_value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
#ifdef ENABLE_CHANNEL_TYPE_CALL
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CALL);
#else
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
#endif
  g_hash_table_insert (table, TP_PROP_CHANNEL_CHANNEL_TYPE, value);

  handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  /* no uint value yet - we'll change it for each channel class */
  g_hash_table_insert (table, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
      handle_type_value);

  g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_CONTACT);
  func (type, table, named_channel_allowed_properties, user_data);

  g_hash_table_unref (table);
}

typedef enum
{
  METHOD_REQUEST,
  METHOD_CREATE,
  METHOD_ENSURE,
} RequestMethod;

static gboolean
rakia_media_manager_requestotron (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties,
                                  RequestMethod method)
{
  RakiaMediaManager *self = RAKIA_MEDIA_MANAGER (manager);
  RakiaMediaManagerPrivate *priv = RAKIA_MEDIA_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleType handle_type;
  TpHandle handle;
#ifdef ENABLE_CHANNEL_TYPE_CALL
  RakiaCallChannel *channel = NULL;
#else
  RakiaMediaChannel *channel = NULL;
#endif
  GError *error = NULL;
  GSList *request_tokens;
  gboolean require_target_handle;
  gboolean add_peer_to_remote_pending;

  /* Supported modes of operation:
   * - RequestChannel(None, 0):
   *     channel is anonymous;
   *     caller uses RequestStreams to set the peer and start the call.
   * - RequestChannel(Contact, n) where n != 0:
   *     channel has TargetHandle=n;
   *     n is in remote pending;
   *     call is started when caller calls RequestStreams.
   * - CreateChannel({THT: Contact, TH: n}):
   *     channel has TargetHandle=n
   *     n is not in the group interface at all
   *     call is started when caller calls RequestStreams.
   * - EnsureChannel({THT: Contact, TH: n}):
   *     look for a channel whose peer is n, and return that if found with
   *       whatever properties and group membership it has;
   *     otherwise the same as into CreateChannel
   */
  switch (method)
    {
    case METHOD_REQUEST:
      require_target_handle = FALSE;
      add_peer_to_remote_pending = TRUE;
      break;
    case METHOD_CREATE:
    case METHOD_ENSURE:
      require_target_handle = TRUE;
      add_peer_to_remote_pending = FALSE;
      break;
    default:
      g_assert_not_reached ();
    }

#ifdef ENABLE_CHANNEL_TYPE_CALL
  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_CHANNEL_TYPE),
        TP_IFACE_CHANNEL_TYPE_CALL))
#else
  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_PROP_CHANNEL_CHANNEL_TYPE),
        TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
#endif
    return FALSE;

  handle_type = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandleType", NULL);

  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);

  switch (handle_type)
    {
    case TP_HANDLE_TYPE_NONE:
      g_assert (handle == 0);

      if (require_target_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "A valid Contact handle must be provided when requesting a media "
              "channel");
          goto error;
        }

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, anon_channel_allowed_properties,
              &error))
        goto error;

      channel = new_media_channel (self, conn->self_handle, 0, NULL);
      break;

    case TP_HANDLE_TYPE_CONTACT:
      g_assert (handle != 0);

      if (tp_channel_manager_asv_has_unknown_properties (request_properties,
              media_channel_fixed_properties, named_channel_allowed_properties,
              &error))
        goto error;

      /* Calls to self are problematic in terms of StreamedMedia channel
       * interface and its semantically required Group member changes;
       * we disable them until a better API is available through
       * Call channel type */
      if (handle == conn->self_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Cannot call self");
          goto error;
        }

      if (method == METHOD_ENSURE)
        {
          guint i;
          TpHandle peer = 0;

          for (i = 0; i < priv->channels->len; i++)
            {
              channel = g_ptr_array_index (priv->channels, i);
              g_object_get (channel, "peer", &peer, NULL);

              if (peer == handle)
                {
                  tp_channel_manager_emit_request_already_satisfied (self,
                      request_token, TP_EXPORTABLE_CHANNEL (channel));
                  return TRUE;
                }
            }
        }

#ifdef ENABLE_CHANNEL_TYPE_CALL
      new_media_channel (self, conn->self_handle, handle,
          request_properties);
#else
      channel = new_media_channel (self, conn->self_handle, handle,
          request_properties);
#endif

      if (add_peer_to_remote_pending)
        {
#ifndef ENABLE_CHANNEL_TYPE_CALL
          if (!_rakia_media_channel_add_member ((GObject *) channel, handle,
                "", &error))
            {
              /* FIXME: do we really want to emit Closed in this case?
               * There wasn't a NewChannel/NewChannels emission */
              rakia_media_channel_close (channel);
              goto error;
            }
#endif
        }

      break;

    default:
      return FALSE;
    }

  g_assert (channel != NULL);

  request_tokens = g_slist_prepend (NULL, request_token);
  tp_channel_manager_emit_new_channel (self,
      TP_EXPORTABLE_CHANNEL (channel), request_tokens);
  g_slist_free (request_tokens);

#ifndef ENABLE_CHANNEL_TYPE_CALL
  rakia_media_channel_create_initial_streams (channel);
#endif

  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}

static gboolean
rakia_media_manager_request_channel (TpChannelManager *manager,
                                     gpointer request_token,
                                     GHashTable *request_properties)
{
  return rakia_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_REQUEST);
}

static gboolean
rakia_media_manager_create_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  return rakia_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_CREATE);
}

static gboolean
rakia_media_manager_ensure_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  return rakia_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_ENSURE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = rakia_media_manager_foreach_channel;
  iface->type_foreach_channel_class =
    rakia_media_manager_type_foreach_channel_class;
  iface->request_channel = rakia_media_manager_request_channel;
  iface->create_channel = rakia_media_manager_create_channel;
  iface->ensure_channel = rakia_media_manager_ensure_channel;
}
