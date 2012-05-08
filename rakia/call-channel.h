/*
 * call-channel.h - Header for RakiaCallChannel
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

#ifndef __RAKIA_CALL_CHANNEL_H__
#define __RAKIA_CALL_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/base-media-call-channel.h>

G_BEGIN_DECLS

typedef struct _RakiaCallChannel RakiaCallChannel;
typedef struct _RakiaCallChannelPrivate RakiaCallChannelPrivate;
typedef struct _RakiaCallChannelClass RakiaCallChannelClass;

struct _RakiaCallChannelClass {
    TpBaseMediaCallChannelClass parent_class;
};

struct _RakiaCallChannel {
    TpBaseMediaCallChannel parent;

    RakiaCallChannelPrivate *priv;
};

GType rakia_call_channel_get_type (void);

/* TYPE MACROS */
#define RAKIA_TYPE_CALL_CHANNEL \
  (rakia_call_channel_get_type ())
#define RAKIA_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
   RAKIA_TYPE_CALL_CHANNEL, RakiaCallChannel))
#define RAKIA_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
   RAKIA_TYPE_CALL_CHANNEL, RakiaCallChannelClass))
#define RAKIA_IS_CALL_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_CALL_CHANNEL))
#define RAKIA_IS_CALL_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_CALL_CHANNEL))
#define RAKIA_CALL_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
   RAKIA_TYPE_CALL_CHANNEL, RakiaCallChannelClass))

void
rakia_call_channel_hangup_error (RakiaCallChannel *self,
    TpCallStateChangeReason reason,
    const gchar *dbus_reason,
    const gchar *message);

G_END_DECLS

#endif /* #ifndef __RAKIA_CALL_CHANNEL_H__*/
