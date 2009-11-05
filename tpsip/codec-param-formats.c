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

  if (strchr (value, ';') == NULL)
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
