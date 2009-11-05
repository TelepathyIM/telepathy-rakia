/*
 * codec-param-formats.c - Implementation of codec parameter formatter infra
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

#include "codec-param-formats.h"

#include <string.h>

#include <tpsip/util.h>

/* Regexps for the name and the value parts of the parameter syntax */
#define FMTP_TOKEN_PARAM "[-A-Za-z0-9!#$%&'*+.^_`{|}~]+"
#define FMTP_TOKEN_VALUE "[^;\"\\s]+|\"([^\"\\\\]|\\\\.)*\""
/* Indexes of the respective match groups in the whole regexp below */
#define FMTP_MATCH_NAME_PARAM "p"
#define FMTP_MATCH_NAME_VALUE "v"

static GRegex *fmtp_attr_regex = NULL;

static void
format_param_generic (gpointer key, gpointer val, gpointer user_data)
{
  const gchar *name = key;
  const gchar *value = val;
  GString *out = user_data;

  /* Ignore freaky parameters */
  g_return_if_fail (name != NULL && name[0]);
  g_return_if_fail (value != NULL && value[0]);

  if (out->len != 0)
    g_string_append_c (out, ';');

  if (strpbrk (value, "; \t") == NULL)
    g_string_append_printf (out, "%s=%s", name, value);
  else
    {
      g_string_append (out, name);
      g_string_append_c (out, '=');
      tpsip_string_append_quoted (out, value);
    }
}

/**
 * tpsip_codec_param_format_generic:
 * @params: the map of codec parameters
 * @out: a #GString for the output
 *
 * Formats the parameters as a semicolon separated list of
 * <replaceable>parameter</replaceable><literal>=</literal><replaceable>value</replaceable>
 * pairs, as recommended in IETF RFC 4855 Section 3.
 */
void
tpsip_codec_param_format_generic (GHashTable *params, GString *out)
{
  g_hash_table_foreach (params, format_param_generic, out);
}

/**
 * tpsip_codec_param_parse_generic:
 * @fmtp: a string value with the parameter description
 * @out: the parameter map to populate
 *
 * Parses parameters formatted as a semicolon separated list of
 * <replaceable>parameter</replaceable><literal>=</literal><replaceable>value</replaceable>
 * pairs, as recommended in IETF RFC 4855 Section 3.
 */
void
tpsip_codec_param_parse_generic (const gchar *fmtp, GHashTable *out)
{
  GMatchInfo *match = NULL;
  gint pos;
  gint value_start;
  gint value_end;

  if (fmtp == NULL)
    return;

  pos = 0;

  /* Fast path for trivial cases, not involving the regex engine */
  while (g_ascii_isspace (fmtp[pos]))
    ++pos;
  if (!fmtp[pos])
    return;

  g_regex_match_full (fmtp_attr_regex,
      fmtp, -1, pos, G_REGEX_MATCH_ANCHORED, &match, NULL);

  while (g_match_info_matches (match))
    {
      gchar *name;
      gchar *value;

      name = g_match_info_fetch_named (match, FMTP_MATCH_NAME_PARAM);

      g_match_info_fetch_named_pos (match, FMTP_MATCH_NAME_VALUE,
          &value_start, &value_end);

      if (value_end - 1 > value_start
          && fmtp[value_start] == '\"' && fmtp[value_end - 1] == '\"')
        {
          value = tpsip_unquote_string (fmtp + value_start,
                                        value_end - value_start);
        }
      else
        {
          value = g_strndup (fmtp + value_start,
                             value_end - value_start);
        }

      g_hash_table_insert (out, name, value);

      g_match_info_fetch_pos (match, 0, NULL, &pos);
      if (!fmtp[pos])
        break;

      g_match_info_next (match, NULL);
    }

  g_match_info_free (match);

  if (fmtp[pos])
    g_message ("failed to parse part of format parameters"
               " as an attribute-value list: %s", &fmtp[pos]);
}

/**
 * tpsip_codec_param_formats_init:
 *
 * Initializes the codec formatting infrastructure.
 * This function must be called before using any other functions in this module.
 * Calling the function more than once has no effect.
 */
void
tpsip_codec_param_formats_init ()
{
  static volatile gsize been_here = 0;

  if (g_once_init_enter (&been_here))
    g_once_init_leave (&been_here, 1);
  else
    return;

  fmtp_attr_regex = g_regex_new (
      "(?<" FMTP_MATCH_NAME_PARAM ">" FMTP_TOKEN_PARAM ")"
      "\\s*=\\s*"
      "(?<"  FMTP_MATCH_NAME_VALUE ">" FMTP_TOKEN_VALUE ")"
      "\\s*(;\\s*|$)",
      G_REGEX_RAW | G_REGEX_OPTIMIZE,
      0 /* G_REGEX_MATCH_ANCHORED */,
      NULL);
  g_assert (fmtp_attr_regex != NULL);
}
