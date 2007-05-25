/*
 * telepathy-helpers.h - Header for various helper functions
 * for telepathy implementation
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __TELEPATHY_HELPERS_H__
#define __TELEPATHY_HELPERS_H__

#include <glib.h>
#include <dbus/dbus-glib.h>


#define DEFINE_TP_STRUCT_TYPE(func, ...) \
  GType func () /* G_GNUC_CONST */                      \
  {                                                     \
    static GType type = 0;                              \
    if (!type)                                          \
      type = dbus_g_type_get_struct ("GValueArray",     \
                                     __VA_ARGS__,       \
                                     G_TYPE_INVALID);   \
    return type;                                        \
  }

#define DEFINE_TP_LIST_TYPE(func, elem_type) \
  GType func () /* G_GNUC_CONST */                      \
  {                                                     \
    static GType type = 0;                              \
    if (!type)                                          \
      type = dbus_g_type_get_collection ("GPtrArray",   \
                                         elem_type);    \
    return type;                                        \
  }


#endif /* __TELEPATHY_HELPERS_H__ */

