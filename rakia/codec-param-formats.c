/*
 * codec-param-formats.c - Implementation of codec parameter formatter infra
 * Copyright (C) 2009, 2010 Nokia Corporation
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

#include "util.h"

#define DEBUG_FLAG RAKIA_DEBUG_UTILITIES
#include "debug.h"


/**
 * RakiaCodecParamFormatFunc:
 * @params: the map of codec parameters
 * @out: a #GString for the output
 *
 * Defines the function pointer signature for codec parameter formatters.
 * A formatter takes a codec parameter map as passed in
 * a org.freedesktop.Telepathy.Media.StreamHandler codec structure,
 * and outputs its SDP representation, as per the value for an
 * <literal>a=fmtp</literal> attribute, into the string buffer @out.
 *
 * <note>
 *   <para>The function is allowed to delete pairs from the @params hash table.
 *   This is useful to implement a custom formatter that processes the
 *   few parameters treated specially, removes them from the map, and 
 *   calls a more generic formatter such as rakia_codec_param_format_generic().
 *   </para>
 * </note>
 */
typedef void (* RakiaCodecParamFormatFunc) (RakiaSipCodec *codec,
    TpMediaStreamType media_type,
    GString *out);

/**
 * RakiaCodecParamParseFunc:
 * @str: a string value with format-specific parameter description
 * @out: the parameter map to populate
 *
 * Defines the function pointer signature for codec parameter parsers.
 * A parser takes the string value coming from an <literal>a=fmtp</literal>
 * SDP attribute, and populates the parameter hash table.
 */
typedef void (* RakiaCodecParamParseFunc) (const gchar *str,
    TpMediaStreamType media_type,
    RakiaSipCodec *codec);

/* Regexps for the name and the value parts of the parameter syntax */
#define FMTP_TOKEN_PARAM "[-A-Za-z0-9!#$%&'*+.^_`{|}~]+"
#define FMTP_TOKEN_VALUE "[^;\"\\s]+|\"([^\"\\\\]|\\\\.)*\""
/* Indexes of the respective match groups in the whole regexp below */
#define FMTP_MATCH_NAME_PARAM "p"
#define FMTP_MATCH_NAME_VALUE "v"

typedef struct _RakiaCodecParamFormatting {
  RakiaCodecParamFormatFunc format;
  RakiaCodecParamParseFunc parse;
} RakiaCodecParamFormatting;

static GRegex *fmtp_attr_regex = NULL;
static GRegex *dtmf_events_regex = NULL;

static GHashTable *codec_param_formats[NUM_TP_MEDIA_STREAM_TYPES];

static void rakia_codec_param_formats_init ();


static void rakia_codec_param_format_generic (RakiaSipCodec *codec,
    TpMediaStreamType media_type,
    GString *out);

static void rakia_codec_param_parse_generic (const gchar *str,
    TpMediaStreamType media_type,
    RakiaSipCodec *codec);


static void rakia_codec_param_register_format (
    TpMediaStreamType media,
    const char *name,
    RakiaCodecParamFormatFunc format,
    RakiaCodecParamParseFunc parse);

/**
 * rakia_codec_param_format:
 * @media_type: the media type
 * @name: name of the codec, as per its MIME subtype registration
 * @params: the map of codec parameters
 * @out: a #GString for the output
 *
 * Formats the parameters passed in the @params into a string suitable for
 * <literal>a=fmtp</literal> attribute for an RTP payload description,
 * as specified for the media type defined by @media and @name.
 */
void
rakia_codec_param_format (TpMediaStreamType media_type, RakiaSipCodec *codec,
    GString *out)
{
  RakiaCodecParamFormatting *fmt;

  rakia_codec_param_formats_init ();

  /* XXX: thread unsafe, we don't care for now */
  fmt = g_hash_table_lookup (codec_param_formats[media_type],
      codec->encoding_name);

  if (fmt != NULL && fmt->format != NULL)
    fmt->format (codec, media_type, out);
  else
    rakia_codec_param_format_generic (codec, media_type, out);
}

/**
 * rakia_codec_param_parse:
 * @media_type: the media type
 * @name: name of the codec, as per its MIME subtype registration
 * @fmtp: a string with the codec-specific parameter data. May be #NULL.
 * @out: the parameter map to populate
 *
 * Parses the payload-specific parameter description as coming from an
 * <literal>a=fmtp</literal> attribute of an RTP payload description.
 * The media type is defined by @media_type and @name.
 */
void
rakia_codec_param_parse (TpMediaStreamType media_type, RakiaSipCodec *codec,
                         const gchar *fmtp)
{
  RakiaCodecParamFormatting *fmt;

  if (fmtp == NULL)
    return;

  rakia_codec_param_formats_init ();

  /* XXX: thread unsafe, we don't care for now */
  fmt = g_hash_table_lookup (codec_param_formats[media_type],
      codec->encoding_name);

  if (fmt != NULL && fmt->parse != NULL)
    fmt->parse (fmtp, media_type, codec);
  else
    rakia_codec_param_parse_generic (fmtp, media_type, codec);
}

/**
 * rakia_codec_param_register_format:
 * @media_type: the media type
 * @name: name of the codec, as per its MIME subtype registration. Must be a static string.
 * @format: pointer to the formatting function
 * @parse: pointer to the parsing function
 *
 * Registers custom SDP payload parameter formatting routines for a media
 * type.
 */
static void
rakia_codec_param_register_format (TpMediaStreamType media_type, const char *name,
                                   RakiaCodecParamFormatFunc format,
                                   RakiaCodecParamParseFunc parse)
{
  RakiaCodecParamFormatting *fmt;

  rakia_codec_param_formats_init ();

  fmt = g_slice_new (RakiaCodecParamFormatting);
  fmt->format = format;
  fmt->parse = parse;

  /* XXX: thread unsafe, we don't care for now */
  g_hash_table_insert (codec_param_formats[media_type], (gpointer) name, fmt);
}

/**
 * rakia_codec_param_format_generic:
 * @params: the map of codec parameters
 * @out: a #GString for the output
 *
 * Formats the parameters as a semicolon separated list of
 * <replaceable>parameter</replaceable><literal>=</literal><replaceable>value</replaceable>
 * pairs, as recommended in IETF RFC 4855 Section 3.
 */
static void
rakia_codec_param_format_generic (RakiaSipCodec *codec,
    TpMediaStreamType media_type, GString *out)
{
  guint i;

  if (codec->params == NULL)
    return;

  for (i = 0; i < codec->params->len; i++)
    {
      RakiaSipCodecParam *param = g_ptr_array_index (codec->params, i);
      RakiaCodecParamFormatting *fmt;

      /* Ignore the ones with special functions */
      fmt = g_hash_table_lookup (codec_param_formats[media_type],
          codec->encoding_name);
      if (fmt != NULL && fmt->format != NULL)
        continue;

      if (out->len != 0)
        g_string_append_c (out, ';');

      if (strpbrk (param->value, "; \t") == NULL)
        g_string_append_printf (out, "%s=%s", param->name, param->value);
      else
        {
          g_string_append (out, param->name);
          g_string_append_c (out, '=');
          rakia_string_append_quoted (out, param->value);
        }
    }
}

/**
 * rakia_codec_param_parse_generic:
 * @fmtp: a string value with the parameter description
 * @out: the parameter map to populate
 *
 * Parses parameters formatted as a semicolon separated list of
 * <replaceable>parameter</replaceable><literal>=</literal><replaceable>value</replaceable>
 * pairs, as recommended in IETF RFC 4855 Section 3.
 */
static void
rakia_codec_param_parse_generic (const gchar *fmtp, TpMediaStreamType media_type,
    RakiaSipCodec *codec)
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
          value = rakia_unquote_string (fmtp + value_start,
                                        value_end - value_start);
        }
      else
        {
          value = g_strndup (fmtp + value_start,
                             value_end - value_start);
        }

      rakia_sip_codec_add_param (codec, name, value);

      g_match_info_fetch_pos (match, 0, NULL, &pos);
      if (!fmtp[pos])
        break;

      g_match_info_next (match, NULL);
    }

  g_match_info_free (match);

  if (fmtp[pos])
    MESSAGE ("failed to parse part of format parameters"
               " as an attribute-value list: %s", &fmtp[pos]);
}

RakiaSipCodecParam *
find_param_by_name (RakiaSipCodec *codec, const gchar *name)
{
  guint i;

  if (codec->params == NULL)
    return NULL;

  for (i = 0; i < codec->params->len; i++)
    {
      RakiaSipCodecParam *param = g_ptr_array_index (codec->params, i);

      if (!strcmp (param->name, name))
        return param;
    }

  return NULL;
}


/* Custom format for audio/telephone-event */

static void
rakia_codec_param_format_telephone_event (RakiaSipCodec *codec,
    TpMediaStreamType media_type,
    GString *out)
{
  RakiaSipCodecParam *events;

  /* events parameter value comes first without the parameter name */
  events = find_param_by_name (codec, "events");
  if (events != NULL)
    {
      g_string_append (out, events->value);
    }

  /* format the rest of the parameters, if any */
  rakia_codec_param_format_generic (codec, media_type, out);
}

static void
rakia_codec_param_parse_telephone_event (const gchar *fmtp,
    TpMediaStreamType media_type,
    RakiaSipCodec *codec)
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
      rakia_sip_codec_add_param (codec, "events", events);
      g_free (events);
      g_match_info_fetch_pos (match, 0, NULL, &end_pos);
    }

  g_match_info_free (match);

  /* Parse the remaining parameters, if any */
  rakia_codec_param_parse_generic (fmtp + end_pos, media_type, codec);
}

/*
 * rakia_codec_param_formats_init:
 *
 * Initializes the codec parameter formatting infrastructure.
 * This function must be called before using any other functions in this module.
 * Calling the function more than once has no effect.
 */
static void
rakia_codec_param_formats_init ()
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

  rakia_codec_param_register_format (
      TP_MEDIA_STREAM_TYPE_AUDIO, "telephone-event",
      rakia_codec_param_format_telephone_event,
      rakia_codec_param_parse_telephone_event);

  fmtp_attr_regex = g_regex_new (
      "(?<" FMTP_MATCH_NAME_PARAM ">" FMTP_TOKEN_PARAM ")"
      "\\s*=\\s*"
      "(?<"  FMTP_MATCH_NAME_VALUE ">" FMTP_TOKEN_VALUE ")"
      "\\s*(;\\s*|$)",
      G_REGEX_RAW | G_REGEX_OPTIMIZE,
      0, NULL);
  g_assert (fmtp_attr_regex != NULL);

#define DTMF_RANGE "[0-9]+(-[0-9]+)?"

  dtmf_events_regex = g_regex_new (
      "^(" DTMF_RANGE "(," DTMF_RANGE ")*)\\s*(;|$)",
      G_REGEX_RAW | G_REGEX_OPTIMIZE,
      0, NULL);
  g_assert (dtmf_events_regex != NULL);
}
