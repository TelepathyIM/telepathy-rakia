/*
 * text-manager.c - Text channel manager for SIP
 * Copyright (C) 2007-2008 Collabora Ltd.
 * Copyright (C) 2007-2011 Nokia Corporation
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

#include "rakia/text-manager.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>

#include "rakia/text-channel.h"
#include "rakia/base-connection.h"
#include "rakia/handles.h"

#include <sofia-sip/msg_header.h>
#include <sofia-sip/sip_tag.h>
#include <sofia-sip/sip_status.h>

#define DEBUG_FLAG RAKIA_DEBUG_IM
#include "rakia/debug.h"


static void channel_manager_iface_init (gpointer g_iface, gpointer iface_data);
static void connection_status_changed_cb (TpBaseConnection *conn,
    guint status, guint reason, RakiaTextManager *self);
static void rakia_text_manager_close_all (RakiaTextManager *self);

G_DEFINE_TYPE_WITH_CODE (RakiaTextManager, rakia_text_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init))

enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

typedef struct _RakiaTextManagerPrivate RakiaTextManagerPrivate;
struct _RakiaTextManagerPrivate
{
  TpBaseConnection *conn;
  /* guint handle => RakiaTextChannel *channel */
  GHashTable *channels;

  gulong status_changed_id;
  gulong message_received_id;

  gboolean dispose_has_run;
};

#define RAKIA_TEXT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RAKIA_TYPE_TEXT_MANAGER, RakiaTextManagerPrivate))

static void
rakia_text_manager_init (RakiaTextManager *fac)
{
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);

  priv->conn = NULL;
  priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_object_unref);

  priv->dispose_has_run = FALSE;
}

static void
rakia_text_manager_constructed (GObject *object)
{
  RakiaTextManager *fac = RAKIA_TEXT_MANAGER (object);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_text_manager_parent_class);

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (object);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}

static void
rakia_text_manager_dispose (GObject *object)
{
  RakiaTextManager *fac = RAKIA_TEXT_MANAGER (object);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  rakia_text_manager_close_all (fac);
  g_assert (priv->channels == NULL);

  if (G_OBJECT_CLASS (rakia_text_manager_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_text_manager_parent_class)->dispose (object);
}

static void
rakia_text_manager_get_property (GObject *object,
                               guint property_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  RakiaTextManager *fac = RAKIA_TEXT_MANAGER (object);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);

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
rakia_text_manager_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  RakiaTextManager *fac = RAKIA_TEXT_MANAGER (object);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = TP_BASE_CONNECTION (g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
rakia_text_manager_class_init (RakiaTextManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (RakiaTextManagerPrivate));

  object_class->constructed = rakia_text_manager_constructed;
  object_class->get_property = rakia_text_manager_get_property;
  object_class->set_property = rakia_text_manager_set_property;
  object_class->dispose = rakia_text_manager_dispose;

  param_spec = g_param_spec_object ("connection",
      "RakiaBaseConnection object",
      "SIP connection that owns this text channel manager",
      RAKIA_TYPE_BASE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);
}

static void
rakia_text_manager_close_all (RakiaTextManager *fac)
{
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);
  GHashTable *channels;

  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }

  if (!priv->channels)
    return;

  channels = priv->channels;
  priv->channels = NULL;

  g_hash_table_unref (channels);
}

struct _ForeachData
{
  TpExportableChannelFunc func;
  gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
  struct _ForeachData *data = (struct _ForeachData *)user_data;
  TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

  data->func (chan, data->user_data);
}

static void
rakia_text_manager_foreach_channel (TpChannelManager *manager,
                                    TpExportableChannelFunc func,
                                    gpointer user_data)
{
  RakiaTextManager *fac = RAKIA_TEXT_MANAGER (manager);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);
  struct _ForeachData data;

  data.func = func;
  data.user_data = user_data;

  g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

/**
 * text_channel_closed_cb:
 *
 * Signal callback for when a text channel is closed. Removes the references
 * that #RakiaChannelManager holds to them.
 */
static void
channel_closed (RakiaTextChannel *chan, gpointer user_data)
{
  RakiaTextManager *self = RAKIA_TEXT_MANAGER (user_data);
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (self);
  TpHandle contact_handle;
  gboolean really_destroyed = TRUE;

  tp_channel_manager_emit_channel_closed_for_object (self,
      (TpExportableChannel *) chan);

  if (priv->channels == NULL)
    return;

  g_object_get (chan,
      "handle", &contact_handle,
      "channel-destroyed", &really_destroyed,
      NULL);

  if (really_destroyed)
    {
      DEBUG ("removing text channel with handle %u", contact_handle);
      g_hash_table_remove (priv->channels, GINT_TO_POINTER (contact_handle));
    }
  else
    {
      DEBUG ("reopening channel with handle %u due to pending messages",
             contact_handle);
      tp_channel_manager_emit_new_channel (self,
          (TpExportableChannel *) chan, NULL);
    }
}

/**
 * new_text_channel
 *
 * Creates a new empty RakiaTextChannel.
 */
static RakiaTextChannel *
rakia_text_manager_new_channel (RakiaTextManager *fac,
                                TpHandle handle,
                                TpHandle initiator,
                                gpointer request_token)
{
  RakiaTextManagerPrivate *priv;
  RakiaTextChannel *chan;
  gchar *object_path;
  TpBaseConnection *conn;
  GSList *request_tokens;

  priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);
  conn = priv->conn;

  object_path = g_strdup_printf ("%s/TextChannel%u",
      tp_base_connection_get_object_path (conn), handle);

  DEBUG ("object path %s", object_path);

  chan = g_object_new (RAKIA_TYPE_TEXT_CHANNEL,
                       "connection", priv->conn,
                       "object-path", object_path,
                       "handle", handle,
                       "initiator-handle", initiator,
                       NULL);

  g_free (object_path);

  g_signal_connect (chan, "closed", (GCallback) channel_closed, fac);

  g_hash_table_insert (priv->channels, GUINT_TO_POINTER (handle), chan);

  if (request_token != NULL)
    request_tokens = g_slist_prepend (NULL, request_token);
  else
    request_tokens = NULL;

  tp_channel_manager_emit_new_channel (fac,
      (TpExportableChannel *) chan, request_tokens);

  g_slist_free (request_tokens);

  return chan;
}


static const gchar * const text_channel_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};

static const gchar * const text_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static void
rakia_text_manager_type_foreach_channel_class (GType type,
    TpChannelManagerTypeChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  GValue *value;

  value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (table, (gchar *) text_channel_fixed_properties[0],
      value);

  value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (table, (gchar *) text_channel_fixed_properties[1],
      value);

  func (type, table, text_channel_allowed_properties, user_data);

  g_hash_table_unref (table);
}


static gboolean
rakia_text_manager_requestotron (RakiaTextManager *self,
                                 gpointer request_token,
                                 GHashTable *request_properties,
                                 gboolean require_new)
{
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (self);
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
  TpHandle handle;
  GError *error = NULL;
  TpExportableChannel *channel;

  if (tp_strdiff (tp_asv_get_string (request_properties,
          TP_IFACE_CHANNEL ".ChannelType"), TP_IFACE_CHANNEL_TYPE_TEXT))
    return FALSE;

  if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    return FALSE;

  /* validity already checked by TpBaseConnection */
  handle = tp_asv_get_uint32 (request_properties,
      TP_IFACE_CHANNEL ".TargetHandle", NULL);
  g_assert (handle != 0);

  if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          text_channel_fixed_properties, text_channel_allowed_properties,
          &error))
    goto error;

  channel = g_hash_table_lookup (priv->channels,
      GUINT_TO_POINTER (handle));

  if (channel == NULL)
    {
      rakia_text_manager_new_channel (self,
          handle, tp_base_connection_get_self_handle (base_conn),
          request_token);
      return TRUE;
    }

  if (require_new)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "Already chatting with contact #%u in another channel", handle);
      goto error;
    }

  tp_channel_manager_emit_request_already_satisfied (self, request_token,
      channel);
  return TRUE;

error:
  tp_channel_manager_emit_request_failed (self, request_token,
      error->domain, error->code, error->message);
  g_error_free (error);
  return TRUE;
}


static gboolean
rakia_text_manager_create_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  RakiaTextManager *self = RAKIA_TEXT_MANAGER (manager);

  return rakia_text_manager_requestotron (self, request_token,
      request_properties, TRUE);
}


static gboolean
rakia_text_manager_request_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  RakiaTextManager *self = RAKIA_TEXT_MANAGER (manager);

  return rakia_text_manager_requestotron (self, request_token,
      request_properties, FALSE);
}


static gboolean
rakia_text_manager_ensure_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  RakiaTextManager *self = RAKIA_TEXT_MANAGER (manager);

  return rakia_text_manager_requestotron (self, request_token,
      request_properties, FALSE);
}

static inline RakiaTextChannel *
rakia_text_manager_lookup_channel (RakiaTextManager *fac,
                                   TpHandle handle)
{
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (fac);

  return g_hash_table_lookup (priv->channels,
      GUINT_TO_POINTER(handle));
}

static gboolean
rakia_nua_i_message_cb (TpBaseConnection    *conn,
                        const RakiaNuaEvent *ev,
                        tagi_t               tags[],
                        RakiaTextManager    *fac)
{
  RakiaTextChannel *channel;
  TpHandle handle;
  const sip_t *sip = ev->sip;
  const char *text = "";
  gsize len = 0;
  char *allocated_text = NULL;

  /* Block anything else except text/plain messages (like isComposings) */
  if (sip->sip_content_type
      && (g_ascii_strcasecmp ("text/plain", sip->sip_content_type->c_type)))
    {
      nua_respond (ev->nua_handle,
                   SIP_415_UNSUPPORTED_MEDIA,
                   SIPTAG_ACCEPT_STR("text/plain"),
                   NUTAG_WITH_THIS(ev->nua),
                   TAG_END());
      goto end;
    }

  /* If there is some text, assure it's in UTF-8 encoding */
  if (sip->sip_payload && sip->sip_payload->pl_len > 0)
    {
      const char *charset = NULL;
      if (sip->sip_content_type)
        {
          charset = msg_header_find_param (
              (msg_common_t *) sip->sip_content_type, "charset");
        }

      /* Default charset is UTF-8, we only need to convert if it's a different one */
      if (charset && g_ascii_strcasecmp (charset, "UTF-8"))
        {
          GError *error;
          gsize in_len;
          allocated_text = g_convert (
              sip->sip_payload->pl_data, sip->sip_payload->pl_len,
              "UTF-8", charset, &in_len, &len, &error);

          if (allocated_text == NULL)
            {
              gint status;
              const char *message = NULL;

              MESSAGE ("character set conversion failed for the message body: %s", error->message);

              if (error->code == G_CONVERT_ERROR_ILLEGAL_SEQUENCE)
                {
                  status = 400;
                  message = "Invalid character sequence in the message body";
                }
              else
                {
                  status = 500;
                  message = "Character set conversion failed for the message body";
                }
              nua_respond (ev->nua_handle,
                           status, message,
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());

              g_error_free (error);
              goto end;
            }

          text = allocated_text;

          if (in_len != sip->sip_payload->pl_len)
            {
              nua_respond (ev->nua_handle,
                           400, "Incomplete character sequence at the "
                                "end of the message body",
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());
              goto end;
            }
        }
      else
        {
          if (!g_utf8_validate (sip->sip_payload->pl_data,
                                sip->sip_payload->pl_len,
                                NULL))
            {
              nua_respond (ev->nua_handle,
                           400, "Invalid character sequence in the message body",
                           NUTAG_WITH_THIS(ev->nua),
                           TAG_END());
              goto end;
            }
          text = sip->sip_payload->pl_data;
          len = (gsize) sip->sip_payload->pl_len;
        }
    }

  handle = rakia_handle_by_requestor (conn, sip);

  if (!handle)
    {
      nua_respond (ev->nua_handle,
                   400, "Invalid From address",
                   NUTAG_WITH_THIS(ev->nua),
                   TAG_END());
      goto end;
    }

  /* Send the final response immediately as recommended by RFC 3428 */
  nua_respond (ev->nua_handle,
               SIP_200_OK,
               NUTAG_WITH_THIS(ev->nua),
               TAG_END());

  DEBUG("Got incoming message from <%s>",
        rakia_handle_inspect (conn, handle));

  channel = rakia_text_manager_lookup_channel (fac, handle);

  if (!channel)
      channel = rakia_text_manager_new_channel (fac,
          handle, handle, NULL);

  rakia_text_channel_receive (channel,
      sip, handle, text, len);

end:
  g_free (allocated_text);

  return TRUE;
}

static void
connection_status_changed_cb (TpBaseConnection *conn,
                              guint status,
                              guint reason,
                              RakiaTextManager *self)
{
  RakiaTextManagerPrivate *priv = RAKIA_TEXT_MANAGER_GET_PRIVATE (self);

  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:

      priv->message_received_id = g_signal_connect (conn,
          "nua-event::nua_i_message",
          G_CALLBACK (rakia_nua_i_message_cb),
          self);

      break;
    case TP_CONNECTION_STATUS_DISCONNECTED:
      rakia_text_manager_close_all (self);

      if (priv->message_received_id != 0)
        {
          g_signal_handler_disconnect (conn, priv->message_received_id);
          priv->message_received_id = 0;
        }

      break;
    default:
      break;
    }
}

static void
channel_manager_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = rakia_text_manager_foreach_channel;
  iface->type_foreach_channel_class =
    rakia_text_manager_type_foreach_channel_class;
  iface->create_channel = rakia_text_manager_create_channel;
  iface->request_channel = rakia_text_manager_request_channel;
  iface->ensure_channel = rakia_text_manager_ensure_channel;
}
