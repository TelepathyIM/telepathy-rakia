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

#include "config.h"

#ifdef ENABLE_DEBUG

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  TPSIP_DEBUG_CONNECTION    = 1 << 0,
  TPSIP_DEBUG_MEDIA         = 1 << 1,
  TPSIP_DEBUG_IM            = 1 << 2,
  TPSIP_DEBUG_EVENTS        = 1 << 3,
} TpsipDebugFlags;

void tpsip_debug_set_flags_from_env ();
void tpsip_debug_set_flags (TpsipDebugFlags flags);
gboolean tpsip_debug_flag_is_set (TpsipDebugFlags flag);
void tpsip_debug (TpsipDebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

G_END_DECLS

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  tpsip_debug(DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

/* #define DEBUGGING tpsip_debug_flag_is_set(DEBUG_FLAG) */

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

#endif /* __DEBUG_H__ */
