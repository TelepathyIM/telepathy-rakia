/*
 * sip-connection.c - Source for TpsipConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005-2008 Nokia Corporation
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

#ifdef ENABLE_SYSLOG
#include <syslog.h>

#define SOFIA_LOG_FAC LOG_USER
#define SOFIA_LOG_PRI LOG_MAKEPRI(SOFIA_LOG_FAC, LOG_INFO)

#endif

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *)g_object_new (
      TPSIP_TYPE_CONNECTION_MANAGER, NULL);
}

#ifdef ENABLE_SYSLOG

static void
sofia_log_handler (void *logdata, const char *format, va_list args)
{
  GString *buf = (GString *)logdata;
  gsize pos;
  gsize bytes_available;
  gsize length_added;

  g_assert (buf != NULL);

  /* Append the formatted message at the end of the buffer */
  pos = buf->len;
  for (;;)
    {
      bytes_available = buf->allocated_len - pos;
      length_added = g_vsnprintf (buf->str + pos,
                                  bytes_available,
                                  format, args);
      if (length_added < bytes_available)
        {
          buf->len = pos + length_added;
          g_assert (!buf->str[buf->len]);
          break;
        }
      g_string_set_size (buf, pos + length_added);
    }

  /* If we have a terminated line, pass it to syslog */
  if (buf->str[buf->len - 1] == '\n')
    {
      syslog (SOFIA_LOG_PRI, "%s", buf->str);
      g_string_truncate (buf, 0);
    }
}

static gpointer
sofia_log_init ()
{
  GString *buf;

  /* XXX: if GLib is modified to send log messages to syslog,
   * hopefully it would use the same flags and facility */
  openlog (NULL, LOG_PERROR | LOG_PID, SOFIA_LOG_FAC);

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
      syslog (SOFIA_LOG_PRI, "%s", buf->str);
      g_message ("last Sofia log message was not newline-terminated");
    }

  g_string_free (buf, TRUE);

  closelog();
}

#else /* !ENABLE_SYSLOG */

static gpointer
sofia_log_init ()
{
  return NULL;
}

static void
sofia_log_finalize (gpointer logdata)
{
}

#endif /* ENABLE_SYSLOG */

int
main (int argc, char** argv)
{
  int status;
  gpointer logdata;

#ifdef ENABLE_DEBUG
  tpsip_debug_set_flags_from_env ();
  tpsip_debug_setup_logfile ();

  if (g_getenv ("TPSIP_PERSIST") || g_getenv ("SOFIASIP_PERSIST"))
    {
      tp_debug_set_persistent (TRUE);
    }
#endif

  logdata = sofia_log_init ();

  status = tp_run_connection_manager ("telepathy-sofiasip", VERSION,
                                      construct_cm, argc, argv);

  sofia_log_finalize (logdata);

  return status;
}
