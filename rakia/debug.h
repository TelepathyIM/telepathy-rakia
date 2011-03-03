/*
 * debug.h - Debug helpers for Telepathy-SofiaSIP, headers
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

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  TPSIP_DEBUG_CONNECTION    = 1 << 0,
  TPSIP_DEBUG_MEDIA         = 1 << 1,
  TPSIP_DEBUG_IM            = 1 << 2,
  TPSIP_DEBUG_EVENTS        = 1 << 3,
  TPSIP_DEBUG_SOFIA         = 1 << 4,
  TPSIP_DEBUG_UTILITIES     = 1 << 5,
} RakiaDebugFlags;

void rakia_debug_set_flags_from_env ();
void rakia_debug_set_flags (RakiaDebugFlags flags);
gboolean rakia_debug_flag_is_set (RakiaDebugFlags flag);
void rakia_log (RakiaDebugFlags flag, GLogLevelFlags level,
    const gchar *format, ...) G_GNUC_PRINTF (3, 4);
void rakia_debug_free (void);

gpointer rakia_sofia_log_init ();
void rakia_sofia_log_finalize (gpointer logdata);

G_END_DECLS

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  rakia_log(DEBUG_FLAG, G_LOG_LEVEL_DEBUG, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define WARNING(format, ...) \
  rakia_log(DEBUG_FLAG, G_LOG_LEVEL_WARNING, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)
#define MESSAGE(format, ...) \
  rakia_log(DEBUG_FLAG, G_LOG_LEVEL_MESSAGE, "%s: " format, \
      G_STRFUNC, ##__VA_ARGS__)

/* #define DEBUGGING rakia_debug_flag_is_set(DEBUG_FLAG) */

#else /* DEBUG_FLAG */

#define DEBUG(format, ...)
#define WARNING(format, ...)
#define MESSAGE(format, ...)

#endif /* DEBUG_FLAG */

#endif /* __DEBUG_H__ */
