/*
 * rakia-sip-media.c - Source for RakiaSipMedia
 * Copyright (C) 2005-2011 Collabora Ltd.
 *   @author Olivier Crete <olivier.crete@collabora.com>
 * Copyright (C) 2005-2010 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation
 *   @author Ole Andre Vadla Ravnaas <ole.andre.ravnaas@collabora.co.uk>
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

#include "rakia/sip-media.h"

#include <sofia-sip/sip_status.h>


#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"


/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define RAKIA_REINVITE_TIMEOUT 180

G_DEFINE_TYPE(RakiaSipMedia,
    rakia_sip_media,
    G_TYPE_OBJECT)



/* private structure */
struct _RakiaSipMediaPrivate
{
  int empty;
};


#define RAKIA_SIP_MEDIA_GET_PRIVATE(media) ((media)->priv)



static void rakia_sip_media_dispose (GObject *object);
static void rakia_sip_media_finalize (GObject *object);


static void
rakia_sip_media_init (RakiaSipMedia *self)
{
  RakiaSipMediaPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaPrivate);

  self->priv = priv;
}

static void
rakia_sip_media_class_init (RakiaSipMediaClass *klass)
{
 GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RakiaSipMediaPrivate));

  object_class->dispose = rakia_sip_media_dispose;
  object_class->finalize = rakia_sip_media_finalize;
}


static void
rakia_sip_media_dispose (GObject *object)
{
  // RakiaSipMedia *self = RAKIA_SIP_MEDIA (object);

  DEBUG("enter");

  if (G_OBJECT_CLASS (rakia_sip_media_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_sip_media_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
rakia_sip_media_finalize (GObject *object)
{
  //RakiaSipMedia *self = RAKIA_SIP_MEDIA (object);

  G_OBJECT_CLASS (rakia_sip_media_parent_class)->finalize (object);

  DEBUG("exit");
}


