/*
 * event-target.h - Header for RakiaEventTarget interface and related utilities
 * Copyright (C) 2008 Nokia Corporation
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
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

#ifndef __RAKIA_EVENT_TARGET_H__
#define __RAKIA_EVENT_TARGET_H__

#include <glib-object.h>
#include <rakia/sofia-decls.h>

G_BEGIN_DECLS

typedef struct _RakiaNuaEvent RakiaNuaEvent;

/**
 * RakiaNuaEvent:
 * @nua_event: The NUA event identifier
 * @status: a SIP status code, or a status value used by Sofia-SIP 
 * @text: The text corresponding to the status code
 * @nua: Pointer to the NUA stack
 * @nua_handle: The NUA operation handle for the event
 * @sip: Headers in the parsed incoming message, or NULL
 *
 * This structure contains data passed to the NUA event callback.
 * The event tag list is not included and passed as a separate parameter.
 */
struct _RakiaNuaEvent {
  nua_event_t   nua_event;
  gint          status;
  const gchar  *text;
  nua_t        *nua;
  nua_handle_t *nua_handle;
  const sip_t  *sip;
};

/**
 * RakiaEventTarget:
 *
 * A typedef representing any implementation of this interface.
 */
typedef struct _RakiaEventTarget RakiaEventTarget;

typedef struct _RakiaEventTargetInterface RakiaEventTargetInterface;

/* TYPE MACROS */
#define RAKIA_TYPE_EVENT_TARGET \
  (rakia_event_target_get_type ())
#define RAKIA_EVENT_TARGET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), RAKIA_TYPE_EVENT_TARGET, RakiaEventTarget))
#define RAKIA_IS_EVENT_TARGET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), RAKIA_TYPE_EVENT_TARGET))
#define RAKIA_EVENT_TARGET_GET_INTERFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE((obj), RAKIA_TYPE_EVENT_TARGET, RakiaEventTargetInterface))

struct _RakiaEventTargetInterface {
  GTypeInterface base_iface;

/*
  gboolean (* nua_event) (RakiaEventTarget *self,
                          const RakiaEvent *event,
                          tagi_t            tags[]);
*/
};

GType rakia_event_target_get_type (void) G_GNUC_CONST;

void rakia_event_target_attach (nua_handle_t *nh, GObject *target);
void rakia_event_target_detach (nua_handle_t *nh);

gboolean rakia_event_target_emit_nua_event (gpointer             instance,
                                            const RakiaNuaEvent *event,
                                            tagi_t               tags[]);

G_END_DECLS

#endif /*__RAKIA_EVENT_TARGET_H__*/
