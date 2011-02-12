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

#include <rakia/sofia-decls.h>
#include <rakia/event-target.h>

G_BEGIN_DECLS

typedef struct _RakiaBaseConnection RakiaBaseConnection;
typedef struct _RakiaBaseConnectionClass RakiaBaseConnectionClass;
typedef struct _RakiaBaseConnectionPrivate RakiaBaseConnectionPrivate;

struct _RakiaBaseConnectionClass {
  TpBaseConnectionClass parent_class;
  TpContactsMixinClass contacts_mixin_class;

  nua_handle_t *(*create_handle) (RakiaBaseConnection *, TpHandle contact);
  void (*add_auth_handler) (RakiaBaseConnection *, RakiaEventTarget *);
};

struct _RakiaBaseConnection {
  TpBaseConnection parent;
  TpContactsMixin contacts_mixin;
  RakiaBaseConnectionPrivate *priv;
};

GType rakia_base_connection_get_type (void) G_GNUC_CONST;

/* TYPE MACROS */
#define TPSIP_TYPE_BASE_CONNECTION \
  (rakia_base_connection_get_type())
#define TPSIP_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
      TPSIP_TYPE_BASE_CONNECTION, RakiaBaseConnection))
#define TPSIP_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
      TPSIP_TYPE_BASE_CONNECTION, RakiaBaseConnectionClass))
#define TPSIP_IS_BASE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TPSIP_TYPE_BASE_CONNECTION))
#define TPSIP_IS_BASE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TPSIP_TYPE_BASE_CONNECTION))
#define TPSIP_BASE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
      TPSIP_TYPE_BASE_CONNECTION, RakiaBaseConnectionClass))

/***********************************************************************
 * Functions for accessing Sofia-SIP interface handles
 ***********************************************************************/

nua_handle_t *rakia_base_connection_create_handle (RakiaBaseConnection *,
    TpHandle contact);
void rakia_base_connection_add_auth_handler (RakiaBaseConnection *self,
    RakiaEventTarget *target);
void rakia_base_connection_save_event (RakiaBaseConnection *self,
    nua_saved_event_t ret_saved [1]);

/** Callback for events delivered by the SIP stack. */
void rakia_base_connection_sofia_callback (nua_event_t event,
    int status, char const *phrase,
    nua_t *nua, RakiaBaseConnection *conn,
    nua_handle_t *nh, RakiaEventTarget *target,
    sip_t const *sip,
    tagi_t tags[]);

G_END_DECLS

#endif /* #ifndef __TPSIP_BASE_CONNECTION_H__*/
