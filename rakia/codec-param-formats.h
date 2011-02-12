/*
 * codec-param-formats.h - Declarations for codec parameter formatters
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

#ifndef __TPSIP_CODEC_PARAM_FORMATS_H__
#define __TPSIP_CODEC_PARAM_FORMATS_H__

#include <glib.h>

#include <telepathy-glib/enums.h>

G_BEGIN_DECLS

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
typedef void (* RakiaCodecParamFormatFunc) (GHashTable *params, GString *out);

/**
 * RakiaCodecParamParseFunc:
 * @str: a string value with format-specific parameter description
 * @out: the parameter map to populate
 *
 * Defines the function pointer signature for codec parameter parsers.
 * A parser takes the string value coming from an <literal>a=fmtp</literal>
 * SDP attribute, and populates the parameter hash table.
 */
typedef void (* RakiaCodecParamParseFunc) (const gchar *str, GHashTable *out);

void rakia_codec_param_format (TpMediaStreamType media, const char *name,
                               GHashTable *params, GString *out);

void rakia_codec_param_parse (TpMediaStreamType media, const char *name,
                              const gchar *fmtp, GHashTable *out);

void rakia_codec_param_register_format (
            TpMediaStreamType media,
            const char *name,
            RakiaCodecParamFormatFunc format,
            RakiaCodecParamParseFunc parse);

void rakia_codec_param_format_generic (GHashTable *params, GString *out);

void rakia_codec_param_parse_generic (const gchar *str, GHashTable *out);

G_END_DECLS

#endif /* !__TPSIP_CODEC_PARAM_FORMATS_H__ */
