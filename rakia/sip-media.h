/*
 * rakia-sip-media.h - Header for RakiaSipMedia
 * Copyright (C) 2005-2012 Collabora Ltd.
 * Copyright (C) 2005-2010 Nokia Corporation
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

#ifndef __RAKIA_SIP_MEDIA_H__
#define __RAKIA_SIP_MEDIA_H__

#include <glib-object.h>
#include <telepathy-glib/handle.h>
#include <sofia-sip/sdp.h>

G_BEGIN_DECLS


typedef struct _RakiaSipMedia RakiaSipMedia;
typedef struct _RakiaSipMediaClass RakiaSipMediaClass;
typedef struct _RakiaSipMediaPrivate RakiaSipMediaPrivate;

struct _RakiaSipMediaClass {
    GObjectClass parent_class;
};

struct _RakiaSipMedia {
    GObject parent;
    RakiaSipMediaPrivate *priv;
};

GType rakia_sip_media_get_type(void);

/* TYPE MACROS */
#define RAKIA_TYPE_SIP_MEDIA \
  (rakia_sip_media_get_type())
#define RAKIA_SIP_MEDIA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_SIP_MEDIA, RakiaSipMedia))
#define RAKIA_SIP_MEDIA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaClass))
#define RAKIA_IS_SIP_MEDIA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_SIP_MEDIA))
#define RAKIA_IS_SIP_MEDIA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_SIP_MEDIA))
#define RAKIA_SIP_MEDIA_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_SIP_MEDIA, RakiaSipMediaClass))

G_END_DECLS

#endif /* #ifndef __RAKIA_SIP_MEDIA_H__*/
