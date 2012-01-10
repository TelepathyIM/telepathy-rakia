/*
 * rakia-sip-session.c - Source for RakiaSipSession
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

#include "rakia/sip-session.h"

#include <sofia-sip/sip_status.h>


#define DEBUG_FLAG RAKIA_DEBUG_MEDIA
#include "rakia/debug.h"


/* The timeout for outstanding re-INVITE transactions in seconds.
 * Chosen to match the allowed cancellation timeout for proxies
 * described in RFC 3261 Section 13.3.1.1 */
#define RAKIA_REINVITE_TIMEOUT 180

G_DEFINE_TYPE(RakiaSipSession,
    rakia_sip_session,
    G_TYPE_OBJECT)



#ifdef ENABLE_DEBUG

/**
 * Sip session states:
 * - created, objects created, local cand/codec query ongoing
 * - invite-sent, an INVITE with local SDP sent, awaiting response
 * - invite-received, a remote INVITE received, response is pending
 * - response-received, a 200 OK received, codec intersection is in progress
 * - active, codecs and candidate pairs have been negotiated (note,
 *   SteamEngine might still fail to verify connectivity and report
 *   an error)
 * - reinvite-sent, a local re-INVITE sent, response is pending
 * - reinvite-received, a remote re-INVITE received, response is pending
 * - ended, session has ended
 */
static const char *const session_states[NUM_RAKIA_SIP_SESSION_STATES] =
{
    "created",
    "invite-sent",
    "invite-received",
    "response-received",
    "active",
    "reinvite-sent",
    "reinvite-received",
    "reinvite-pending",
    "ended"
};

#define SESSION_DEBUG(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "session [%-17s]: " format, \
      session_states[(session)->priv->state],##__VA_ARGS__)

#define SESSION_MESSAGE(session, format, ...) \
  rakia_log (DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "session [%-17s]: " format, \
      session_states[(session)->priv->state],##__VA_ARGS__)

#else /* !ENABLE_DEBUG */

#define SESSION_DEBUG(session, format, ...) G_STMT_START { } G_STMT_END
#define SESSION_MESSAGE(session, format, ...) G_STMT_START { } G_STMT_END

#endif /* ENABLE_DEBUG */


/* private structure */
struct _RakiaSipSessionPrivate
{
  RakiaSipSessionState state;           /* session state */

  GPtrArray *streams;
};


#define RAKIA_SIP_SESSION_GET_PRIVATE(session) ((session)->priv)



static void rakia_sip_session_dispose (GObject *object);
static void rakia_sip_session_finalize (GObject *object);


static void
rakia_sip_session_init (RakiaSipSession *self)
{
  RakiaSipSessionPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      RAKIA_TYPE_SIP_SESSION, RakiaSipSessionPrivate);

  self->priv = priv;

  priv->state = RAKIA_SIP_SESSION_STATE_CREATED;

  /* allocate any data required by the object here */
  priv->streams = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
rakia_sip_session_class_init (RakiaSipSessionClass *klass)
{
 GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (RakiaSipSessionPrivate));

  object_class->dispose = rakia_sip_session_dispose;
  object_class->finalize = rakia_sip_session_finalize;
}


static void
rakia_sip_session_dispose (GObject *object)
{
  RakiaSipSession *self = RAKIA_SIP_SESSION (object);

  DEBUG("enter");

  if (self->priv->streams)
    {
      g_ptr_array_unref (self->priv->streams);
      self->priv->streams = NULL;
    }

  if (G_OBJECT_CLASS (rakia_sip_session_parent_class)->dispose)
    G_OBJECT_CLASS (rakia_sip_session_parent_class)->dispose (object);

  DEBUG("exit");
}

static void
rakia_sip_session_finalize (GObject *object)
{
  //RakiaSipSession *self = RAKIA_SIP_SESSION (object);

  G_OBJECT_CLASS (rakia_sip_session_parent_class)->finalize (object);

  DEBUG("exit");
}


