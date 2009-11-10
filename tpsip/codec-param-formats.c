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

typedef struct _TpsipCodecParamFormatting {
  TpsipCodecParamFormatFunc format;
  TpsipCodecParamParseFunc parse;
} TpsipCodecParamFormatting;

static GRegex *fmtp_attr_regex = NULL;
static GRegex *dtmf_events_regex = NULL;

static GHashTable *codec_param_formats[NUM_TP_MEDIA_STREAM_TYPES];

/**
 * tpsip_codec_param_format:
 * @media: the media type
 * @name: name of the codec, as per its MIME subtype registration
 * @params: the map of codec parameters
 * @out: a #GString for the output
 *
 * Formats the parameters passed in the @params into a string suitable for
 * <literal>a=fmtp</literal> attribute for an RTP payload description,
 * as specified for the media type defined by @media and @name.
 */
void
tpsip_codec_param_format (TpMediaStreamType media, const char *name,
                          GHashTable *params, GString *out)
{
  TpsipCodecParamFormatting *fmt;

  /* XXX: thread unsafe, we don't care for now */
  fmt = g_hash_table_lookup (codec_param_formats[media], name);

  if (fmt != NULL && fmt->format != NULL)
    fmt->format (params, out);
  else
    tpsip_codec_param_format_generic (params, out);
}

/**
 * tpsip_codec_param_parse:
 * @media: the media type
 * @name: name of the codec, as per its MIME subtype registration
 * @fmtp: a string with the codec-specific parameter data
 * @out: the parameter map to populate
 *
 * Parses the payload-specific parameter description as coming from an
 * <literal>a=fmtp</literal> attribute of an RTP payload description.
 * The media type is defined by @media and @name.
 */
void
tpsip_codec_param_parse (TpMediaStreamType media, const char *name,
                         const gchar *fmtp, GHashTable *out)
{
  TpsipCodecParamFormatting *fmt;

  /* XXX: thread unsafe, we don't care for now */
  fmt = g_hash_table_lookup (codec_param_formats[media], name);

  if (fmt != NULL && fmt->parse != NULL)
    fmt->parse (fmtp, out);
  else
    tpsip_codec_param_parse_generic (fmtp, out);
}

/**
 * tpsip_codec_param_register_format:
 * @media: the media type
 * @name: name of the codec, as per its MIME subtype registration. Must be a static string.
 * @format: pointer to the formatting function
 * @parse: pointer to the parsing function
 *
 * Registers custom SDP payload parameter formatting routines for a media
 * type.
 */
void
tpsip_codec_param_register_format (TpMediaStreamType media, const char *name,
                                   TpsipCodecParamFormatFunc format,
                                   TpsipCodecParamParseFunc parse)
{
  TpsipCodecParamFormatting *fmt;

  fmt = g_slice_new (TpsipCodecParamFormatting);
  fmt->format = format;
  fmt->parse = parse;

  /* XXX: thread unsafe, we don't care for now */
  g_hash_table_insert (codec_param_formats[media], name, fmt);
}

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

  g_assert (fmtp_attr_regex != NULL);

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

/* Custom format for audio/telephone-event */

static void
tpsip_codec_param_format_telephone_event (GHashTable *params, GString *out)
{
  const gchar *events;

  /* events parameter value comes first without the parameter name */
  events = g_hash_table_lookup (params, "events");
  if (events != NULL)
    {
      g_string_append (out, events);
      g_hash_table_remove (params, "events");
    }

  /* format the rest of the parameters, if any */
  tpsip_codec_param_format_generic (params, out);
}

static void
tpsip_codec_param_parse_telephone_event (const gchar *fmtp, GHashTable *out)
{
  GMatchInfo *match = NULL;
  gint end_pos = 0;

  g_assert (dtmf_events_regex != NULL);

  /* Parse the events list */

  g_regex_match (dtmf_events_regex, fmtp, 0, &match);

  if (g_match_info_matches (match))
    {
      gchar *events;

      events = g_match_info_fetch (match, 1);
      g_hash_table_insert (out, g_strdup ("events"), events);

      g_match_info_fetch_pos (match, 0, NULL, &end_pos);
    }

  g_match_info_free (match);

  /* Parse the remaining parameters, if any */
  tpsip_codec_param_parse_generic (fmtp + end_pos, out);
}

/**
 * tpsip_codec_param_formats_init:
 *
 * Initializes the codec parameter formatting infrastructure.
 * This function must be called before using any other functions in this module.
 * Calling the function more than once has no effect.
 */
void
tpsip_codec_param_formats_init ()
{
  static volatile gsize been_here = 0;

  int i;

  if (g_once_init_enter (&been_here))
    g_once_init_leave (&been_here, 1);
  else
    return;

  for (i = 0; i < NUM_TP_MEDIA_STREAM_TYPES; ++i)
    {
      /* XXX: we ignore deallocation of values for now */
      codec_param_formats[i] = g_hash_table_new (g_str_hash, g_str_equal);
    }

  tpsip_codec_param_register_format (
      TP_MEDIA_STREAM_TYPE_AUDIO, "telephone-event",
      tpsip_codec_param_format_telephone_event,
      tpsip_codec_param_parse_telephone_event);

  fmtp_attr_regex = g_regex_new (
      "(?<" FMTP_MATCH_NAME_PARAM ">" FMTP_TOKEN_PARAM ")"
      "\\s*=\\s*"
      "(?<"  FMTP_MATCH_NAME_VALUE ">" FMTP_TOKEN_VALUE ")"
      "\\s*(;\\s*|$)",
      G_REGEX_RAW | G_REGEX_OPTIMIZE,
      0 /* G_REGEX_MATCH_ANCHORED */,
      NULL);
  g_assert (fmtp_attr_regex != NULL);

#define DTMF_RANGE "[0-9]+(-[0-9]+)?"

  dtmf_events_regex = g_regex_new (
      "^(" DTMF_RANGE "(," DTMF_RANGE ")*)\\s*(;|$)",
      G_REGEX_RAW | G_REGEX_OPTIMIZE,
      0, NULL);
  g_assert (dtmf_events_regex != NULL);
}
