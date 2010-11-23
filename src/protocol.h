/*
 * protocol.h - header for TpsipProtocol
 * Copyright (C) 2007-2010 Collabora Ltd.
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

#ifndef TPSIP_PROTOCOL_H
#define TPSIP_PROTOCOL_H

#include <glib-object.h>
#include <telepathy-glib/base-protocol.h>

#include <tpsip/sofia-decls.h>
#include <sofia-sip/su_glib.h>

G_BEGIN_DECLS

typedef struct _TpsipProtocol TpsipProtocol;
typedef struct _TpsipProtocolPrivate TpsipProtocolPrivate;
typedef struct _TpsipProtocolClass TpsipProtocolClass;
typedef struct _TpsipProtocolClassPrivate TpsipProtocolClassPrivate;

struct _TpsipProtocolClass {
    TpBaseProtocolClass parent_class;

    TpsipProtocolClassPrivate *priv;
};

struct _TpsipProtocol {
    TpBaseProtocol parent;

    TpsipProtocolPrivate *priv;
};

GType tpsip_protocol_get_type (void);

#define TPSIP_TYPE_PROTOCOL \
    (tpsip_protocol_get_type ())
#define TPSIP_PROTOCOL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
        TPSIP_TYPE_PROTOCOL, \
        TpsipProtocol))
#define TPSIP_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), \
        TPSIP_TYPE_PROTOCOL, \
        TpsipProtocolClass))
#define TPSIP_IS_PROTOCOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
        TPSIP_TYPE_PROTOCOL))
#define TPSIP_PROTOCOL_GET_CLASS(klass) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), \
        TPSIP_TYPE_PROTOCOL, \
        TpsipProtocolClass))

gchar *tpsip_protocol_normalize_contact (const gchar *id,
    GError **error);

TpBaseProtocol *tpsip_protocol_new (su_root_t *sofia_root);

G_END_DECLS

#endif
