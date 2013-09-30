/*
 * connection-aliasing.c - Implementation for RakiaConnectionAliasing interface
 * Copyright (C) 2008-2011 Nokia Corporation
 *   @author Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
 *   @author Pekka Pessi <pekka.pessi@nokia.com>
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

#include "config.h"

#include <rakia/connection-aliasing.h>
#include <rakia/base-connection.h>
#include <rakia/handles.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "rakia/handles.h"

#include <string.h>

#define DEBUG_FLAG RAKIA_DEBUG_CONNECTION
#include "rakia/debug.h"

enum {
  PROP_NONE,
  PROP_ALIAS,
};

static void
rakia_connection_aliasing_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      initialized = TRUE;

      g_object_interface_install_property (klass,
          g_param_spec_string ("alias", "Alias",
              "User's display name",
              NULL,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    }
}

GType
rakia_connection_aliasing_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0))
    {
      static const GTypeInfo info = {
        sizeof (RakiaConnectionAliasingInterface),
        rakia_connection_aliasing_base_init, /* base_init */
        NULL, /* base_finalize */
        NULL, /* class_init */
        NULL, /* class_finalize */
        NULL, /* class_data */
        0,
        0, /* n_preallocs */
        NULL /* instance_init */
      };

      type = g_type_register_static (G_TYPE_INTERFACE,
          "RakiaConnectionAliasingInterface", &info, 0);

      g_type_interface_add_prerequisite (type, RAKIA_TYPE_BASE_CONNECTION);
      g_type_interface_add_prerequisite (type,
          TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING);
    }

  return type;
}

static gchar *
conn_get_default_alias (TpBaseConnection *base,
                        TpHandleRepoIface *contact_handles,
                        TpHandle handle)
{
  const url_t *url;
  gchar *alias = NULL;

  url = rakia_base_connection_handle_to_uri (RAKIA_BASE_CONNECTION (base),
      handle);

  switch (url->url_type)
    {
    case url_sip:
      /* Return the SIP URI stripped down to [user@]host */
      if (url->url_user != NULL)
        alias = g_strdup_printf ("%s@%s",
                                 url->url_user, url->url_host);
      else
        alias = g_strdup (url->url_host);
      break;
    case url_tel:
      /* Retrieve the telephone number */
      alias = g_strdup (url->url_host);
      break;
    default:
      /* Return the handle string as is */
      alias = g_strdup (tp_handle_inspect (contact_handles, handle));
    }
  return alias;
}

static gchar *
conn_get_alias (TpBaseConnection *base,
                TpHandleRepoIface *contact_handles,
                TpHandle handle)
{
  gchar *alias = NULL;

  if (handle == tp_base_connection_get_self_handle (base))
    {
      /* Get our user-settable alias from the connection property */
      g_object_get (base, "alias", &alias, NULL);
    }

  if (alias == NULL)
    alias = conn_get_default_alias (base, contact_handles, handle);

  g_assert (alias != NULL);
  DEBUG("handle %u got alias %s", handle, alias);

  return alias;
}

static void
rakia_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
                                  const GArray *contacts,
                                  DBusGMethodInvocation *context)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);
  TpHandleRepoIface *contact_handles;
  GArray *aliases;
  gchar **res;
  GError *error = NULL;
  guint i;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  aliases = g_array_sized_new (TRUE, FALSE, sizeof (gchar *), contacts->len);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      gchar *alias;

      handle = g_array_index (contacts, TpHandle, i);

      alias = conn_get_alias (base, contact_handles, handle);

      g_array_append_val (aliases, alias);
    }

  res = (gchar **) g_array_free (aliases, FALSE);

  tp_svc_connection_interface_aliasing_return_from_request_aliases (
      context, (const gchar **) res);

  g_strfreev (res);
}

static void
emit_self_alias_change (TpBaseConnection *base, const gchar *alias)
{
  GHashTable *change_data;

  change_data = g_hash_table_new (NULL, NULL);

  g_hash_table_insert (change_data,
      GUINT_TO_POINTER (tp_base_connection_get_self_handle (base)),
      (gchar *) alias);

  tp_svc_connection_interface_aliasing_emit_aliases_changed (base, change_data);

  g_hash_table_unref (change_data);
}

static const gchar *
collapse_whitespace (const gchar *str, gchar **to_free)
{
  static GRegex *whitespace_regex = NULL;

  const gchar *p;
  gchar *subst_res;

  p = (const gchar *) strpbrk (str, " \t\r\n");
  if (p == NULL)
    return str;

  if (whitespace_regex == NULL)
    {
      whitespace_regex = g_regex_new ("[[:space:]]+",
          G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    }

  subst_res = g_regex_replace_literal (whitespace_regex, str, -1, p - str, " ",
      0, NULL);

  *to_free = subst_res;
  return subst_res;
}

static void
rakia_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
                              GHashTable *aliases,
                              DBusGMethodInvocation *context)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);
  TpHandleRepoIface *contact_handles;
  const gchar *alias;
  gchar *default_alias;
  gchar *to_free = NULL;
  TpHandle self_handle = tp_base_connection_get_self_handle (base);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  /* We only care about the self alias */
  alias = g_hash_table_lookup (aliases, GINT_TO_POINTER (self_handle));

  if (alias == NULL || g_hash_table_size (aliases) > 1)
    {
      /* One of the handles (if there are any) cannot be the self handle */
      GError err = { TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Cannot set aliases for any contact except self" };
      dbus_g_method_return_error (context, &err);
      return;
    }

  alias = collapse_whitespace (alias, &to_free);

  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  default_alias = conn_get_default_alias (base,
      contact_handles, self_handle);

  if (strcmp (alias, default_alias) == 0)
    {
      DEBUG("using default alias for self");
      g_object_set (base, "alias", NULL, NULL);
    }
  else
    {
      DEBUG("setting alias for self: %s", alias);
      g_object_set (base, "alias", alias, NULL);
    }

  emit_self_alias_change (base, alias);

  g_free (default_alias);
  g_free (to_free);

  tp_svc_connection_interface_aliasing_return_from_set_aliases (context);
}

static void
rakia_conn_aliasing_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (obj);
  TpHandleRepoIface *contact_handles;
  guint i;

  contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      GValue *val;

      handle = g_array_index (contacts, TpHandle, i);

      val = tp_g_value_slice_new (G_TYPE_STRING);

      g_value_take_string (val,
          conn_get_alias (base, contact_handles, handle));

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          TP_IFACE_CONNECTION_INTERFACE_ALIASING "/alias", val);
    }
}

void
rakia_connection_aliasing_init (gpointer instance)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (instance),
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      rakia_conn_aliasing_fill_contact_attributes);
}


void
rakia_connection_aliasing_svc_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass =
    (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, rakia_connection_##x)
  IMPLEMENT(request_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}
