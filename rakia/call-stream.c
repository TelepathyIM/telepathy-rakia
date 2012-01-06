/*
 * call-stream.c - RakiaCallStream
 * Copyright (C) 2011 Collabora Ltd.
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

#include "rakia/call-stream.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "debug.h"


static GPtrArray *rakia_call_stream_add_local_candidates (
    TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error);
static gboolean rakia_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error);
static void rakia_call_stream_request_receiving (TpBaseMediaCallStream *stream,
    TpHandle contact, gboolean receive);


static void rakia_call_stream_dispose (GObject *object);
static void rakia_call_stream_finalize (GObject *object);


G_DEFINE_TYPE(RakiaCallStream, rakia_call_stream,
    TP_TYPE_BASE_MEDIA_CALL_STREAM)


/* private structure */
struct _RakiaCallStreamPrivate
{
  gboolean dispose_has_run;
};

static void
rakia_call_stream_init (RakiaCallStream *self)
{
  RakiaCallStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_STREAM, RakiaCallStreamPrivate);

  self->priv = priv;
}


static void
rakia_call_stream_class_init (RakiaCallStreamClass *rakia_call_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_stream_class);
  TpBaseMediaCallStreamClass *bmcs_class =
      TP_BASE_MEDIA_CALL_STREAM_CLASS (rakia_call_stream_class);

  g_type_class_add_private (rakia_call_stream_class,
    sizeof (RakiaCallStreamPrivate));

  object_class->dispose = rakia_call_stream_dispose;
  object_class->finalize = rakia_call_stream_finalize;

  bmcs_class->add_local_candidates = rakia_call_stream_add_local_candidates;
  bmcs_class->set_sending = rakia_call_stream_set_sending;
  bmcs_class->request_receiving = rakia_call_stream_request_receiving;
}



void
rakia_call_stream_dispose (GObject *object)
{
  RakiaCallStream *self = RAKIA_CALL_STREAM (object);
  RakiaCallStreamPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (G_OBJECT_CLASS (rakia_call_stream_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_call_stream_parent_class)->dispose (object);
}

void
rakia_call_stream_finalize (GObject *object)
{
  G_OBJECT_CLASS (rakia_call_stream_parent_class)->finalize (object);
}


static GPtrArray *
rakia_call_stream_add_local_candidates (TpBaseMediaCallStream *stream,
    const GPtrArray *candidates,
    GError **error)
{
  GPtrArray *accepted_candidates = g_ptr_array_sized_new (candidates->len);


  return accepted_candidates;
}


static gboolean
rakia_call_stream_set_sending (TpBaseMediaCallStream *stream,
    gboolean sending,
    GError **error)
{
  // RakiaCallStream *self = RAKIA_CALL_STREAM (stream);

  return TRUE;
}

static void rakia_call_stream_request_receiving (TpBaseMediaCallStream *stream,
    TpHandle contact, gboolean receive)
{
}
