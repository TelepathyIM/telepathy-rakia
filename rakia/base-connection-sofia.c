/*
 * sip-connection-sofia.c - Source for RakiaConnection Sofia event handling
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2007-2008 Collabora Ltd.
 *   @author Kai Vehmanen <first.surname@nokia.com>
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

#include "config.h"

#include <rakia/base-connection.h>
#include <sofia-sip/su_tag_io.h>

#define DEBUG_FLAG TPSIP_DEBUG_EVENTS
#include "rakia/debug.h"

static void
priv_r_shutdown(int status,
                nua_t *nua)
{
  GSource *source;
  gboolean source_recursive;

  if (status < 200)
    return;

  /* Should be the source of the Sofia root */
  source = g_main_current_source ();

  /* XXX: temporarily allow recursion in the Sofia source to work around
   * nua_destroy() requiring nested mainloop iterations to complete
   * (Sofia-SIP bug #1624446). Actual recursion safety of the source is to be
   * examined. */
  source_recursive = g_source_get_can_recurse (source);
  if (!source_recursive)
    {
      DEBUG("forcing Sofia root GSource to be recursive");
      g_source_set_can_recurse (source, TRUE);
    }

  DEBUG("destroying Sofia-SIP NUA at address %p", nua);
  nua_destroy (nua);

  if (!source_recursive)
    g_source_set_can_recurse (source, FALSE);
}

#if 0
static void
priv_r_unregister (int status,
                   char const *phrase,
                   nua_handle_t *nh)
{
  DEBUG("un-REGISTER got response: %03d %s", status, phrase);

  if (status < 200)
    return;

  if (status == 401 || status == 407)
    {
      /* In SIP, de-registration can fail! However, there's not a lot we can
       * do about this in the Telepathy model - once you've gone DISCONNECTED
       * you're really not meant to go "oops, I'm still CONNECTED after all".
       * So we ignore it and hope it goes away. */
      WARNING ("Registrar won't let me unregister: %d %s", status, phrase);
    }
}
#endif

#ifdef ENABLE_DEBUG
static void
priv_r_get_params (int status,
                   nua_t *nua,
                   nua_handle_t *nh,
                   tagi_t tags[])
{
  if (status < 200)
    return;

  if (nh != NULL)
    return;

  /* note: print contents of all tags to stdout */
  tl_print(stdout, "Sofia-SIP NUA stack parameters:\n", tags);
}
#endif

/**
 * Callback for events delivered by the SIP stack.
 *
 * See libsofia-sip-ua/nua/nua.h documentation.
 */
void
rakia_base_connection_sofia_callback (nua_event_t event,
                                      int status,
                                      char const *phrase,
                                      nua_t *nua,
                                      RakiaBaseConnection *conn,
                                      nua_handle_t *nh,
                                      RakiaEventTarget *target,
                                      sip_t const *sip,
                                      tagi_t tags[])
{
  DEBUG("event %s: %03d %s",
        nua_event_name (event), status, phrase);

  switch (event)
    {
#ifdef ENABLE_DEBUG
    case nua_r_get_params:
      priv_r_get_params (status, nua, nh, tags);
      return;
#endif
    case nua_r_shutdown:
      priv_r_shutdown (status, nua);
      return;
    default:
      break;
    }

  g_assert (conn != NULL);

  DEBUG("connection %p, refcount %d", conn, ((GObject *)conn)->ref_count);

  {
    RakiaNuaEvent ev = {
        event,
        status,
        phrase,
        nua,
        nh,
        sip
      };

    if (target == NULL)
      {
        target = (RakiaEventTarget *) conn;
        DEBUG("dispatching to connection %p (unbound handle %p)", conn, nh);
      }
    else
      {
        g_assert (nh != NULL);
        DEBUG("dispatching to target %p (handle %p)", target, nh);
      }

    if (!rakia_event_target_emit_nua_event (target,
                                            &ev,
                                            tags))
      {
        DEBUG("event %s for target %p was not consumed", nua_event_name (event), target);
      }
  }

  DEBUG ("exit");
}
