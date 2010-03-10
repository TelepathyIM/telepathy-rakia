/*
 * sip-connection.c - Source for TpsipConnection
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

#include <config.h>
#include "debug.h"

#include "sip-connection-manager.h"
#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

#include <sofia-sip/su_log.h>

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *)g_object_new (
      TPSIP_TYPE_CONNECTION_MANAGER, NULL);
}

static void
sofia_log_handler (void *logdata, const char *format, va_list args)
{
  GString *buf = (GString *)logdata;
  g_assert (buf != NULL);

  /* Append the formatted message at the end of the buffer */
  g_string_append_vprintf (buf, format, args);

  /* If we have a terminated line, log it, stripping the newline */
  if (buf->str[buf->len - 1] == '\n')
    {
      g_string_truncate (buf, buf->len - 1);
      tpsip_log (TPSIP_DEBUG_SOFIA, G_LOG_LEVEL_DEBUG, "%s", buf->str);
      g_string_truncate (buf, 0);
    }
}

static gpointer
sofia_log_init ()
{
  GString *buf;

  buf = g_string_sized_new (80);

  su_log_redirect (NULL, sofia_log_handler, buf);

  return buf;
}

static void
sofia_log_finalize (gpointer logdata)
{
  GString *buf = (GString *)logdata;

  if (buf->len != 0)
    {
      /* Don't use tpsip_log here because the CM has already been finalized, so
       * out TpDebugSender will have too. It isn't crucial, anyway. */
      g_debug ("%s", buf->str);
      g_message ("last Sofia log message was not newline-terminated");
    }

  g_string_free (buf, TRUE);
}

int
main (int argc, char** argv)
{
  int status;
  gpointer logdata;
  guint fatal_mask;

  fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
  fatal_mask |= G_LOG_LEVEL_CRITICAL;
  g_log_set_always_fatal (fatal_mask);  

#ifdef ENABLE_DEBUG
  tpsip_debug_set_flags_from_env ();

  if (g_getenv ("TPSIP_PERSIST") || g_getenv ("SOFIASIP_PERSIST"))
    {
      tp_debug_set_persistent (TRUE);
    }
#endif

  tp_debug_divert_messages (g_getenv ("TPSIP_LOGFILE"));

  logdata = sofia_log_init ();

  status = tp_run_connection_manager ("telepathy-sofiasip", VERSION,
                                      construct_cm, argc, argv);

  sofia_log_finalize (logdata);

  return status;
}
