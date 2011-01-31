/*
 * sip-base-connection.h - Header for SipBaseConnection
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

#ifndef __TPSIP_BASE_CONNECTION_H__
#define __TPSIP_BASE_CONNECTION_H__

#include <glib-object.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/contacts-mixin.h>

#include <tpsip/event-target.h>

G_BEGIN_DECLS

typedef struct _TpsipBaseConnection TpsipBaseConnection;
typedef struct _TpsipBaseConnectionClass TpsipBaseConnectionClass;
typedef struct _TpsipBaseConnectionPrivate TpsipBaseConnectionPrivate;

struct _TpsipBaseConnectionClass {
  TpBaseConnectionClass parent_class;
  TpContactsMixinClass contacts_mixin_class;

  nua_handle_t *(*create_handle) (TpsipBaseConnection *, TpHandle contact);
  void (*add_auth_handler) (TpsipBaseConnection *, TpsipEventTarget *);
};

struct _TpsipBaseConnection {
  TpBaseConnection parent;
  TpContactsMixin contacts_mixin;
  TpsipBaseConnectionPrivate *priv;
};

GType tpsip_base_connection_get_type (void) G_GNUC_CONST;

/* TYPE MACROS */
#define TPSIP_TYPE_BASE_CONNECTION \
  (tpsip_base_connection_get_type())
#define TPSIP_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      TPSIP_TYPE_BASE_CONNECTION, TpsipBaseConnection))
#define TPSIP_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
      TPSIP_TYPE_BASE_CONNECTION, TpsipBaseConnectionClass))
#define TPSIP_IS_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_BASE_CONNECTION))
#define TPSIP_IS_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_BASE_CONNECTION))
#define TPSIP_BASE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
      TPSIP_TYPE_BASE_CONNECTION, TpsipBaseConnectionClass))

/***********************************************************************
 * Functions for accessing Sofia-SIP interface handles
 ***********************************************************************/

nua_handle_t *tpsip_base_connection_create_handle (TpsipBaseConnection *,
    TpHandle contact);
void tpsip_base_connection_add_auth_handler (TpsipBaseConnection *self,
    TpsipEventTarget *target);
void tpsip_base_connection_save_event (TpsipBaseConnection *self,
    nua_saved_event_t ret_saved [1]);

G_END_DECLS

#endif /* #ifndef __TPSIP_BASE_CONNECTION_H__*/
