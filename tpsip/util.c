/*
 * util.c - implementation of Telepathy-SofiaSIP utilities
 * Copyright (C) 2009 Nokia Corporation
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
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

#include "util.h"


static const guchar escape_table[256] =
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1,
      /* Control characters except LF and CR.
       * NOTE: null character is intentionally flagged */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  /* 0x10 - 0x1f */
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x22 == '"' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,  /* 0x5c == '\\' */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,  /* 0x7f */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

/**
 * tpsip_string_append_quoted:
 * @buf: a #GString to append the quoted text to
 * @text: text to append as a quoted string
 *
 * Appends to @buf the content of @text as a quoted string accordingly to SIP
 * or MIME syntax.
 */
void
tpsip_string_append_quoted (GString *buf, const gchar *text)
{
  const gchar *p;
  gchar quoted_pair[2] = { '\\', };

  g_string_append_c (buf, '"');

  p = text;
  while (*p)
    {
      const gchar *q;
      gchar ch;

      /* Get the following text span to append verbatim */
      for (q = p; !escape_table[ch = *q]; ++q);
      g_string_append_len (buf, p, q - p);

      if (ch == '\0')
        break;

      quoted_pair[1] = ch;
      g_string_append_len (buf, quoted_pair, 2);

      p = q + 1;
    }

  g_string_append_c (buf, '"');
}

/**
 * tpsip_quote_string:
 * @src: the source string
 *
 * Formats the content of @text as a quoted string accordingly to SIP
 * or MIME syntax.
 *
 * Returns: a newly allocated quoted string. The string is to be freed with
 * g_free().
 */
gchar *
tpsip_quote_string (const gchar *src)
{
  GString *buf;

  buf = g_string_sized_new (2);

  tpsip_string_append_quoted (buf, src);

  return g_string_free (buf, FALSE);
}
