#include "config.h"

#ifdef ENABLE_DEBUG

#include <stdarg.h>

#include <glib.h>

#include <telepathy-glib/debug.h>

#include "debug.h"

static SIPDebugFlags flags = 0;

static GDebugKey keys[] = {
  { "media-channel", SIP_DEBUG_MEDIA },
  { "connection",    SIP_DEBUG_CONNECTION },
  { "im",            SIP_DEBUG_IM },
  { 0, },
};

void sip_debug_set_flags_from_env ()
{
  guint nkeys;
  const gchar *flags_string;

  for (nkeys = 0; keys[nkeys].value; nkeys++);

  flags_string = g_getenv ("SOFIASIP_DEBUG");

  if (flags_string)
    {
      tp_debug_set_flags_from_env ("SOFIASIP_DEBUG");
      sip_debug_set_flags (g_parse_debug_string (flags_string, keys, nkeys));
    }
}

void sip_debug_set_flags (SIPDebugFlags new_flags)
{
  flags |= new_flags;
}

gboolean sip_debug_flag_is_set (SIPDebugFlags flag)
{
  return flag & flags;
}

void sip_debug (SIPDebugFlags flag,
                const gchar *format,
                ...)
{
  if (flag & flags)
    {
      va_list args;
      va_start (args, format);
      g_logv (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, format, args);
      va_end (args);
    }
}

#endif /* ENABLE_DEBUG */
