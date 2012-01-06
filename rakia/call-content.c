/*
 * call-content.c - RakiaCallContent
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

#include "rakia/call-content.h"

#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "debug.h"


static void rakia_call_content_deinit (TpBaseCallContent *base);

G_DEFINE_TYPE (RakiaCallContent, rakia_call_content,
    TP_TYPE_BASE_MEDIA_CALL_CONTENT);

/* private structure */
struct _RakiaCallContentPrivate
{

  gboolean dispose_has_run;
  gboolean deinit_has_run;
};

static void
rakia_call_content_init (RakiaCallContent *self)
{
  RakiaCallContentPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_CALL_CONTENT, RakiaCallContentPrivate);

  self->priv = priv;
}

static void rakia_call_content_dispose (GObject *object);

static void
rakia_call_content_class_init (
    RakiaCallContentClass *rakia_call_content_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (rakia_call_content_class);
  TpBaseCallContentClass *bcc_class =
      TP_BASE_CALL_CONTENT_CLASS (rakia_call_content_class);

  g_type_class_add_private (rakia_call_content_class,
      sizeof (RakiaCallContentPrivate));

  object_class->dispose = rakia_call_content_dispose;
  bcc_class->deinit = rakia_call_content_deinit;
}

static void
rakia_call_content_dispose (GObject *object)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (object);
  RakiaCallContentPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;


  if (G_OBJECT_CLASS (rakia_call_content_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_call_content_parent_class)->dispose (object);
}

static void
rakia_call_content_deinit (TpBaseCallContent *base)
{
  RakiaCallContent *self = RAKIA_CALL_CONTENT (base);
  RakiaCallContentPrivate *priv = self->priv;

  if (priv->deinit_has_run)
    return;

  priv->deinit_has_run = TRUE;

  TP_BASE_CALL_CONTENT_CLASS (
    rakia_call_content_parent_class)->deinit (base);
}
