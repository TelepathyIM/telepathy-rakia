
#ifndef __DEBUG_H__
#define __DEBUG_H_

#include "config.h"

#ifdef ENABLE_DEBUG

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  TPSIP_DEBUG_CONNECTION    = 1 << 0,
  TPSIP_DEBUG_MEDIA         = 1 << 1,
  TPSIP_DEBUG_IM            = 1 << 2,
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
