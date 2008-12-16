/*
 * conn-aliasing.c - Aliasing interface implementation for TpsipConnection
 * Copyright (C) 2008 Nokia Corporation
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

#include "conn-aliasing.h"

#include <telepathy-glib/errors.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include "sip-connection-helpers.h"

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "debug.h"

static void
tpsip_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *iface,
                                  DBusGMethodInvocation *context)
{
  TpBaseConnection *base = TP_BASE_CONNECTION (iface);

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  /* No server-side aliasing yet */
  tp_svc_connection_interface_aliasing_return_from_get_alias_flags (
      context, 0);
}

static gchar *
conn_get_alias (TpsipConnection *self,
                TpHandleRepoIface *contact_handles,
                TpHandle handle)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  gchar *alias = NULL;

  if (handle == base->self_handle)
    {
      /* Get our user-settable alias from the connection property */
      g_object_get (self, "alias", &alias, NULL);
    }

  if (alias == NULL)
    {
      const url_t *url;

      url = tpsip_conn_get_contact_url (self, handle);
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
    }

  g_assert (alias != NULL);
  DEBUG("handle %u got alias %s", handle, alias);

  return alias;
}

static void
tpsip_connection_request_aliases (TpSvcConnectionInterfaceAliasing *iface,
                                  const GArray *contacts,
                                  DBusGMethodInvocation *context)
{
  TpsipConnection *self = TPSIP_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
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

      alias = conn_get_alias (self, contact_handles, handle);

      g_array_append_val (aliases, alias);
    }

  res = (gchar **) g_array_free (aliases, FALSE);

  tp_svc_connection_interface_aliasing_return_from_request_aliases (
      context, (const gchar **) res);

  g_strfreev (res);
}

static void
tpsip_connection_get_aliases (TpSvcConnectionInterfaceAliasing *iface,
                              const GArray *contacts,
                              DBusGMethodInvocation *context)
{
  TpsipConnection *self = TPSIP_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles;
  GHashTable *result;
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

  result = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, g_free);

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle;
      gchar *alias;

      handle = g_array_index (contacts, TpHandle, i);

      alias = conn_get_alias (self, contact_handles, handle);

      g_hash_table_insert (result, GUINT_TO_POINTER (handle), alias);
    }

  tp_svc_connection_interface_aliasing_return_from_get_aliases (context,
      result);

  g_hash_table_destroy (result);
}

static void
emit_self_alias_change (TpsipConnection *self, const gchar *alias)
{
  TpBaseConnection *base = (TpBaseConnection *) self;
  GPtrArray *change_data;
  GValue change_pair = { 0, };

  g_value_init (&change_pair, TP_STRUCT_TYPE_ALIAS_PAIR);
  g_value_take_boxed (&change_pair,
      dbus_g_type_specialized_construct (TP_STRUCT_TYPE_ALIAS_PAIR));
  dbus_g_type_struct_set (&change_pair,
      0, base->self_handle,
      1, alias,
      G_MAXUINT);
  change_data = g_ptr_array_sized_new (1);
  g_ptr_array_add (change_data, g_value_get_boxed (&change_pair));

  tp_svc_connection_interface_aliasing_emit_aliases_changed (self, change_data);

  g_ptr_array_free (change_data, TRUE);
  g_value_unset (&change_pair);
}

static void
tpsip_connection_set_aliases (TpSvcConnectionInterfaceAliasing *iface,
                              GHashTable *aliases,
                              DBusGMethodInvocation *context)
{
  TpsipConnection *self = TPSIP_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
  const gchar *alias;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  /* We only care about the self alias */
  alias = g_hash_table_lookup (aliases, GINT_TO_POINTER (base->self_handle));

  if (alias == NULL || g_hash_table_size (aliases) > 1)
    {
      /* One of the handles (if there are any) cannot be the self handle */
      GError err = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Cannot set aliases for any contact except self" };
      dbus_g_method_return_error (context, &err);
      return;
    }

  DEBUG("setting alias for self: %s", alias);
  g_object_set (self, "alias", alias, NULL);

  emit_self_alias_change (self, alias);

  tp_svc_connection_interface_aliasing_return_from_set_aliases (context);
}

static void
tpsip_conn_aliasing_fill_contact_attributes (GObject *obj,
    const GArray *contacts, GHashTable *attributes_hash)
{
  TpsipConnection *self = TPSIP_CONNECTION (obj);
  TpBaseConnection *base = (TpBaseConnection *) self;
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
          conn_get_alias (self, contact_handles, handle));

      tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
          TP_IFACE_CONNECTION_INTERFACE_ALIASING "/alias", val);
    }
}

void
tpsip_conn_aliasing_init (TpsipConnection *conn)
{
  tp_contacts_mixin_add_contact_attributes_iface (G_OBJECT (conn),
      TP_IFACE_CONNECTION_INTERFACE_ALIASING,
      tpsip_conn_aliasing_fill_contact_attributes);
}

void
tpsip_conn_aliasing_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcConnectionInterfaceAliasingClass *klass =
    (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, tpsip_connection_##x)
  IMPLEMENT(get_alias_flags);
  IMPLEMENT(request_aliases);
  IMPLEMENT(get_aliases);
  IMPLEMENT(set_aliases);
#undef IMPLEMENT
}
