/*
 * call-channel.c - RakiaCallChannel
 * Copyright © 2011 Collabora Ltd.
 * @author Olivier Crete <olivier.crete@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"

#include "rakia/call-channel.h"

#include "rakia/call-content.h"

#define DEBUG_FLAG RAKIA_DEBUG_CALL
#include "rakia/debug.h"


G_DEFINE_TYPE(RakiaCallChannel, rakia_call_channel,
    TP_TYPE_BASE_MEDIA_CALL_CHANNEL)


static void rakia_call_channel_close (TpBaseChannel *base);

static TpBaseCallContent * rakia_call_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    GError **error);
static void rakia_call_channel_hangup (
    TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message);
static void rakia_call_channel_set_ringing (TpBaseCallChannel *base);
static void rakia_call_channel_set_queued (TpBaseCallChannel *base);

static void rakia_call_channel_accept (TpBaseMediaCallChannel *channel);
static void rakia_call_channel_hold_state_changed (TpBaseMediaCallChannel *self,
    TpLocalHoldState hold_state, TpLocalHoldStateReason hold_state_reason);


/* properties */
enum
{
  LAST_PROPERTY
};


/* private structure */
struct _RakiaCallChannelPrivate
{
  gboolean dispose_has_run;
};



static void
rakia_call_channel_init (RakiaCallChannel *self)
{
  RakiaCallChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_CHANNEL, RakiaCallChannelPrivate);

  self->priv = priv;
}

static void rakia_call_channel_dispose (GObject *object);
static void rakia_call_channel_finalize (GObject *object);


static void
rakia_call_channel_constructed (GObject *obj)
{
  GObjectClass *parent_object_class =
      G_OBJECT_CLASS (rakia_call_channel_parent_class);

  if (parent_object_class->constructed != NULL)
    parent_object_class->constructed (obj);

}

static void
rakia_call_channel_get_property (GObject    *object,
    guint       property_id,
    GValue     *value,
    GParamSpec *pspec)
{
  // RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);

  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}



static void
rakia_call_channel_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  // RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);

  switch (property_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
  }
}


static gchar *
rakia_call_channel_get_object_path_suffix (TpBaseChannel *base)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (base);

  return g_strdup_printf ("CallChannel%p", self);
}



static void
rakia_call_channel_class_init (
    RakiaCallChannelClass *rakia_call_channel_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_channel_class);
  TpBaseChannelClass *base_channel_class =
      TP_BASE_CHANNEL_CLASS (rakia_call_channel_class);
  TpBaseCallChannelClass *base_call_class =
      TP_BASE_CALL_CHANNEL_CLASS (rakia_call_channel_class);
  TpBaseMediaCallChannelClass *base_media_call_class =
      TP_BASE_MEDIA_CALL_CHANNEL_CLASS (rakia_call_channel_class);

  g_type_class_add_private (rakia_call_channel_class,
      sizeof (RakiaCallChannelPrivate));

  object_class->constructed = rakia_call_channel_constructed;
  object_class->get_property = rakia_call_channel_get_property;
  object_class->set_property = rakia_call_channel_set_property;

  object_class->dispose = rakia_call_channel_dispose;
  object_class->finalize = rakia_call_channel_finalize;


  base_channel_class->target_handle_type = TP_HANDLE_TYPE_CONTACT;
  base_channel_class->get_object_path_suffix =
      rakia_call_channel_get_object_path_suffix;
  base_channel_class->close = rakia_call_channel_close;

  base_call_class->add_content = rakia_call_channel_add_content;
  base_call_class->hangup = rakia_call_channel_hangup;
  base_call_class->set_ringing = rakia_call_channel_set_ringing;
  base_call_class->set_queued = rakia_call_channel_set_queued;

  base_media_call_class->accept = rakia_call_channel_accept;
  base_media_call_class->hold_state_changed =
      rakia_call_channel_hold_state_changed;
}


void
rakia_call_channel_dispose (GObject *object)
{
  RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);

  DEBUG ("hello thar");

  if (self->priv->dispose_has_run)
    return;

  self->priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (rakia_call_channel_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_call_channel_parent_class)->dispose (object);
}

void
rakia_call_channel_finalize (GObject *object)
{
  // RakiaCallChannel *self = RAKIA_CALL_CHANNEL (object);

  G_OBJECT_CLASS (rakia_call_channel_parent_class)->finalize (object);
}



static void
rakia_call_channel_close (TpBaseChannel *base)
{
}

static TpBaseCallContent *
rakia_call_channel_add_content (
    TpBaseCallChannel *base,
    const gchar *name,
    TpMediaStreamType type,
    GError **error)
{
  RakiaCallContent *content = NULL;

  return TP_BASE_CALL_CONTENT (content);

}
static void
rakia_call_channel_hangup (
    TpBaseCallChannel *base,
    guint reason,
    const gchar *detailed_reason,
    const gchar *message)
{
}

static void
rakia_call_channel_set_ringing (TpBaseCallChannel *base)
{
}

static void
rakia_call_channel_set_queued (TpBaseCallChannel *base)
{
}

static void
rakia_call_channel_accept (TpBaseMediaCallChannel *channel)
{
}

static void
rakia_call_channel_hold_state_changed (TpBaseMediaCallChannel *self,
    TpLocalHoldState hold_state,
    TpLocalHoldStateReason hold_state_reason)
{
}
