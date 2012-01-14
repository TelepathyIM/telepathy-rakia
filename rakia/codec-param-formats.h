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

#ifndef __RAKIA_CODEC_PARAM_FORMATS_H__
#define __RAKIA_CODEC_PARAM_FORMATS_H__

#include <glib.h>

#include <telepathy-glib/enums.h>

#include <rakia/sip-media.h>

G_BEGIN_DECLS


void rakia_codec_param_format (RakiaMediaType media_type, RakiaSipCodec *codec,
    GString *out);

void rakia_codec_param_parse (RakiaMediaType media_type, RakiaSipCodec *codec,
    const gchar *fmtp);


G_END_DECLS

#endif /* !__RAKIA_CODEC_PARAM_FORMATS_H__ */
