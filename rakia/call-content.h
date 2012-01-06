/*
 * call-content.h - Header for RakiaCallContent
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

#ifndef __RAKIA_CALL_CONTENT_H__
#define __RAKIA_CALL_CONTENT_H__

#include <glib-object.h>

#include <telepathy-glib/base-media-call-content.h>

G_BEGIN_DECLS

typedef struct _RakiaCallContent RakiaCallContent;
typedef struct _RakiaCallContentPrivate RakiaCallContentPrivate;
typedef struct _RakiaCallContentClass RakiaCallContentClass;

struct _RakiaCallContentClass {
    TpBaseMediaCallContentClass parent_class;
};

struct _RakiaCallContent {
    TpBaseMediaCallContent parent;

    RakiaCallContentPrivate *priv;
};

GType rakia_call_content_get_type (void);

/* TYPE MACROS */
#define RAKIA_TYPE_CALL_CONTENT \
  (rakia_call_content_get_type ())
#define RAKIA_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      RAKIA_TYPE_CALL_CONTENT, RakiaCallContent))
#define RAKIA_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
    RAKIA_TYPE_CALL_CONTENT, RakiaCallContentClass))
#define RAKIA_IS_CALL_CONTENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_CALL_CONTENT))
#define RAKIA_IS_CALL_CONTENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_CALL_CONTENT))
#define RAKIA_CALL_CONTENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
    RAKIA_TYPE_CALL_CONTENT, RakiaCallContentClass))

G_END_DECLS

#endif /* #ifndef __RAKIA_CALL_CONTENT_H__*/
