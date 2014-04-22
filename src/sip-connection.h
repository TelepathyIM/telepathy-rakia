/*
 * sip-connection.h - Header for RakiaConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2009 Nokia Corporation
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

#ifndef __RAKIA_CONNECTION_H__
#define __RAKIA_CONNECTION_H__

#include <glib-object.h>

#include <rakia/base-connection.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef enum
{
  RAKIA_CONNECTION_KEEPALIVE_AUTO = 0,	/** Keepalive management is up to the implementation */
  RAKIA_CONNECTION_KEEPALIVE_NONE,	/** Disable keepalive management */
  RAKIA_CONNECTION_KEEPALIVE_REGISTER,	/** Maintain registration with REGISTER requests */
  RAKIA_CONNECTION_KEEPALIVE_OPTIONS,	/** Maintain registration with OPTIONS requests */
  RAKIA_CONNECTION_KEEPALIVE_STUN,	/** Maintain registration with STUN as described in IETF draft-sip-outbound */
} RakiaConnectionKeepaliveMechanism;

typedef struct _RakiaConnection RakiaConnection;
typedef struct _RakiaConnectionClass RakiaConnectionClass;
typedef struct _RakiaConnectionPrivate RakiaConnectionPrivate;

struct _RakiaConnectionClass {
    RakiaBaseConnectionClass parent_class;
};

struct _RakiaConnection {
    RakiaBaseConnection parent;
};

/* TYPE MACROS */
#define RAKIA_TYPE_CONNECTION \
  (rakia_connection_get_type())
#define RAKIA_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_CONNECTION, RakiaConnection))
#define RAKIA_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), RAKIA_TYPE_CONNECTION, RakiaConnectionClass))
#define RAKIA_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_CONNECTION))
#define RAKIA_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), RAKIA_TYPE_CONNECTION))
#define RAKIA_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), RAKIA_TYPE_CONNECTION, RakiaConnectionClass))

GType rakia_connection_get_type (void) G_GNUC_CONST;

void rakia_connection_connect_auth_handler (RakiaConnection *self,
                                            RakiaEventTarget *target);

const gchar **rakia_connection_get_implemented_interfaces (void);

G_END_DECLS

#endif /* #ifndef __RAKIA_CONNECTION_H__*/
