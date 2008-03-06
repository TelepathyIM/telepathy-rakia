
#ifndef __DEBUG_H__
#define __DEBUG_H_

#include "config.h"

#ifdef ENABLE_DEBUG

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  SIP_DEBUG_CONNECTION    = 1 << 0,
  SIP_DEBUG_MEDIA         = 1 << 1,
  SIP_DEBUG_IM            = 1 << 2,
} SIPDebugFlags;

void sip_debug_set_flags_from_env ();
void sip_debug_set_flags (SIPDebugFlags flags);
gboolean sip_debug_flag_is_set (SIPDebugFlags flag);
void sip_debug (SIPDebugFlags flag, const gchar *format, ...)
    G_GNUC_PRINTF (2, 3);

G_END_DECLS

#ifdef DEBUG_FLAG

#define DEBUG(format, ...) \
  sip_debug(DEBUG_FLAG, "%s: " format, G_STRFUNC, ##__VA_ARGS__)

#define DEBUGGING sip_debug_flag_is_set(DEBUG_FLAG)

#endif /* DEBUG_FLAG */

#else /* ENABLE_DEBUG */

#ifdef DEBUG_FLAG

#define DEBUG(format, ...)

#define DEBUGGING 0

#define NODE_DEBUG(n, s)

#endif /* DEBUG_FLAG */

#endif /* ENABLE_DEBUG */

#endif /* __DEBUG_H__ */
