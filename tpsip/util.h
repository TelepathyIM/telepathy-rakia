/*
 * util.h - declarations for Telepathy-SofiaSIP utilities
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

#ifndef TPSIP_UTIL_H_
#define TPSIP_UTIL_H_

#include <glib.h>

G_BEGIN_DECLS

gchar * tpsip_quote_string (const gchar *src);

gchar * tpsip_unquote_string (const gchar *src, gssize len);

void tpsip_string_append_quoted (GString *buf, const gchar *text);

G_END_DECLS

#endif /* !TPSIP_UTIL_H_ */
