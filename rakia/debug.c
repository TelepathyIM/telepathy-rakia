/*
 * debug.h - Debug helpers for Telepathy-SofiaSIP, implementation
 * Copyright (C) 2007-2008 Nokia Corporation
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

#include "debug.h"

#include <glib.h>

#include <telepathy-glib/debug.h>
#include <telepathy-glib/debug-sender.h>

#include "config.h"

static RakiaDebugFlags rakia_debug_flags = 0;

static const GDebugKey rakia_debug_keys[] = {
  { "media-channel", RAKIA_DEBUG_MEDIA },
  { "connection",    RAKIA_DEBUG_CONNECTION },
  { "im",            RAKIA_DEBUG_IM },
  { "events",        RAKIA_DEBUG_EVENTS },
  { "sofia",         RAKIA_DEBUG_SOFIA },
  { "utilities",     RAKIA_DEBUG_UTILITIES },
};

static GHashTable *flag_to_domains = NULL;

#ifdef ENABLE_DEBUG
static GString *sofia_log_buf = NULL;
#endif

static void rakia_sofia_log_close (void);


void rakia_debug_set_flags_from_env ()
{
  const gchar *flags_string;

  flags_string = g_getenv ("RAKIA_DEBUG");
  if (flags_string == NULL)
    flags_string = g_getenv ("TPSIP_DEBUG");

  if (flags_string != NULL)
    {
      tp_debug_set_flags (flags_string);

      rakia_debug_set_flags (g_parse_debug_string (flags_string,
                                                   rakia_debug_keys,
                                                   G_N_ELEMENTS(rakia_debug_keys)));
    }
}

void rakia_debug_set_flags (RakiaDebugFlags new_flags)
{
  rakia_debug_flags |= new_flags;
}

gboolean rakia_debug_flag_is_set (RakiaDebugFlags flag)
{
  return (flag & rakia_debug_flags) ? TRUE : FALSE;
}

static const gchar *
debug_flag_to_domain (RakiaDebugFlags flag)
{
  if (G_UNLIKELY (flag_to_domains == NULL))
    {
      guint i;

      flag_to_domains = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, g_free);

      for (i = 0; i < G_N_ELEMENTS(rakia_debug_keys); i++)
        {
          GDebugKey key = (GDebugKey) rakia_debug_keys[i];
          gchar *val;

          val = g_strdup_printf ("%s/%s", "rakia", key.key);
          g_hash_table_insert (flag_to_domains,
              GUINT_TO_POINTER (key.value), val);
        }
    }

  return g_hash_table_lookup (flag_to_domains, GUINT_TO_POINTER (flag));
}

void
rakia_debug_free (void)
{
  rakia_sofia_log_close ();

  if (flag_to_domains == NULL)
    return;

  g_hash_table_destroy (flag_to_domains);
  flag_to_domains = NULL;
}

void rakia_log (RakiaDebugFlags flag,
                GLogLevelFlags level,
                const gchar *format,
                ...)
{
  TpDebugSender *dbg;
  gchar *message = NULL;
  gchar **message_out;
  va_list args;

  message_out =
      (level > G_LOG_LEVEL_DEBUG || (flag & rakia_debug_flags) != 0)?
      &message : NULL;

  dbg = tp_debug_sender_dup ();

  va_start (args, format);
  tp_debug_sender_add_message_vprintf (dbg, NULL, message_out,
      debug_flag_to_domain (flag), level, format, args);
  va_end (args);

  g_object_unref (dbg);

  if (message_out != NULL)
    {
      g_log (G_LOG_DOMAIN, level, "%s", message);
      g_free (message);
    }
}

void
rakia_sofia_log_handler (void *logdata, const char *format, va_list args)
{
#ifdef ENABLE_DEBUG
  if (G_UNLIKELY (sofia_log_buf == NULL))
    sofia_log_buf = g_string_sized_new (
        g_printf_string_upper_bound (format, args));

  /* Append the formatted message at the end of the buffer */
  g_string_append_vprintf (sofia_log_buf, format, args);

  /* If we have a newline-terminated line, log it, stripping the newline */
  if (sofia_log_buf->str[sofia_log_buf->len - 1] == '\n')
    {
      g_string_truncate (sofia_log_buf, sofia_log_buf->len - 1);
      rakia_log (RAKIA_DEBUG_SOFIA, G_LOG_LEVEL_DEBUG, "%s",
          sofia_log_buf->str);
      g_string_truncate (sofia_log_buf, 0);
    }
#endif
}

static void
rakia_sofia_log_close (void)
{
#ifdef ENABLE_DEBUG
  if (sofia_log_buf == NULL)
    return;

  if (sofia_log_buf->len != 0)
    {
      rakia_log (RAKIA_DEBUG_SOFIA, G_LOG_LEVEL_DEBUG, "%s",
          sofia_log_buf->str);
      rakia_log (RAKIA_DEBUG_SOFIA, G_LOG_LEVEL_DEBUG,
          "(the preceding message may have been deferred"
          " due to not being newline-terminated)");
    }

  g_string_free (sofia_log_buf, TRUE);
  sofia_log_buf = NULL;
#endif
}
