/*
 * sip-connection.c - Source for RakiaConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2008, 2010 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection).
 *   @author See gabble.c
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

#include "rakia/debug.h"

#include "sip-connection-manager.h"
#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *)g_object_new (
      RAKIA_TYPE_CONNECTION_MANAGER, NULL);
}


int
main (int argc, char** argv)
{
  int status;
  guint fatal_mask;
  const gchar *logfile_string;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);  

#ifdef ENABLE_DEBUG
  rakia_debug_set_flags_from_env ();
#endif

  if (g_getenv ("RAKIA_PERSIST") || g_getenv ("TPSIP_PERSIST"))
    {
      tp_debug_set_persistent (TRUE);
    }

  logfile_string = g_getenv ("RAKIA_LOGFILE");
  if (logfile_string == NULL)
    logfile_string = g_getenv ("TPSIP_LOGFILE");

  tp_debug_divert_messages (logfile_string);

  tp_debug_divert_messages (logfile_string);

  status = tp_run_connection_manager ("telepathy-rakia", VERSION,
                                      construct_cm, argc, argv);

  return status;
}
