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

#include "config.h"

#ifdef ENABLE_DEBUG

#include <stdarg.h>

#include <glib.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

static TpsipDebugFlags tpsip_debug_flags = 0;

static const GDebugKey tpsip_debug_keys[] = {
  { "media-channel", TPSIP_DEBUG_MEDIA },
  { "connection",    TPSIP_DEBUG_CONNECTION },
  { "im",            TPSIP_DEBUG_IM },
  { "events",        TPSIP_DEBUG_EVENTS },
};

void tpsip_debug_set_flags_from_env ()
{
  const gchar *flags_string;

  flags_string = g_getenv ("TPSIP_DEBUG");

  if (flags_string)
    {
      tp_debug_set_flags (flags_string);

      tpsip_debug_set_flags (g_parse_debug_string (flags_string,
                                                   tpsip_debug_keys,
                                                   G_N_ELEMENTS(tpsip_debug_keys)));
    }
}

void tpsip_debug_set_flags (TpsipDebugFlags new_flags)
{
  tpsip_debug_flags |= new_flags;
}

gboolean tpsip_debug_flag_is_set (TpsipDebugFlags flag)
{
  return flag & tpsip_debug_flags;
}

void tpsip_debug (TpsipDebugFlags flag,
                  const gchar *format,
                  ...)
{
  if (flag & tpsip_debug_flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

#endif /* ENABLE_DEBUG */
