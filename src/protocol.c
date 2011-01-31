/*
 * protocol.c - source for TpsipProtocol
 * Copyright (C) 2007-2010 Collabora Ltd.
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

#include "protocol.h"

#include <string.h>

#include <telepathy-glib/telepathy-glib.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-glib.h>

#include <tpsip/sofia-decls.h>
#include <tpsip/handles.h>
#include <sofia-sip/su_glib.h>

#define DEBUG_FLAG TPSIP_DEBUG_CONNECTION
#include "debug.h"
#include "media-factory.h"
#include "sip-connection.h"
#include "sip-connection-helpers.h"
#include "text-factory.h"

#define PROTOCOL_NAME "sip"
#define ICON_NAME "im-" PROTOCOL_NAME
#define VCARD_FIELD_NAME "x-" PROTOCOL_NAME
#define ENGLISH_NAME "SIP"

G_DEFINE_TYPE (TpsipProtocol,
    tpsip_protocol,
    TP_TYPE_BASE_PROTOCOL)

enum {
    PROP_SOFIA_ROOT = 1,
};

struct _TpsipProtocolPrivate
{
  su_root_t *sofia_root;
};

/* Used in the otherwise-unused offset field of the TpCMParamSpec. The first
 * one is nonzero to catch implicit zero-initialization. */
enum {
    PARAM_EASY = 1,
    PARAM_SET_SEPARATELY
};

static TpCMParamSpec tpsip_params[] = {
    /* Account (a sip: URI)
     *
     * FIXME: validate account SIP URI properly, using appropriate RFCs */
    { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_REGISTER,
      NULL, PARAM_SET_SEPARATELY, tp_cm_param_filter_string_nonempty, NULL },

    /* Username to register with, if different than in the account URI */
    { "auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, PARAM_EASY },

    /* Password */
    { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_SECRET, NULL, PARAM_EASY },

    /* Display name for self */
    { "alias", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL, PARAM_EASY,
      /* setting a 0-length alias makes no sense */
      tp_cm_param_filter_string_nonempty, NULL },

    /* Registrar */
    { "registrar", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
      PARAM_EASY },

    /* Used to compose proxy URI */
    { "proxy-host", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
      PARAM_SET_SEPARATELY },
    { "port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(SIP_DEFAULT_PORT),
      PARAM_SET_SEPARATELY, tp_cm_param_filter_uint_nonzero },
    { "transport", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "auto", PARAM_SET_SEPARATELY },

    /* Enables loose routing as per RFC 3261 */
    { "loose-routing", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      PARAM_EASY },

    /* Used to enable proactive NAT traversal techniques */
    { "discover-binding", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE),
      PARAM_EASY },

    /* Mechanism used for connection keepalive maintenance */
    { "keepalive-mechanism", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, "auto", PARAM_SET_SEPARATELY },

    /* Keep-alive interval */
    { "keepalive-interval", DBUS_TYPE_UINT32_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(0), PARAM_EASY },

    /* Use SRV DNS lookup to discover STUN server */
    { "discover-stun", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(TRUE), PARAM_EASY },

    /* STUN server */
    { "stun-server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
      PARAM_EASY },

    /* STUN port */
    { "stun-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT,
      GUINT_TO_POINTER(TPSIP_DEFAULT_STUN_PORT), PARAM_EASY,
      tp_cm_param_filter_uint_nonzero },

    /* If the session content cannot be modified once initially set up */
    { "immutable-streams", DBUS_TYPE_BOOLEAN_AS_STRING, G_TYPE_BOOLEAN,
      TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT, GUINT_TO_POINTER(FALSE),
      PARAM_EASY },

    /* Local IP address to use, workaround purposes only */
    { "local-ip-address", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, PARAM_EASY },

    /* Local port for SIP, workaround purposes only */
    { "local-port", DBUS_TYPE_UINT16_AS_STRING, G_TYPE_UINT, 0, NULL,
      PARAM_EASY },

    /* Extra-realm authentication */
    { "extra-auth-user", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      0, NULL, PARAM_EASY },
    { "extra-auth-password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
      TP_CONN_MGR_PARAM_FLAG_SECRET, NULL, PARAM_EASY },

    { NULL }
};

static void
tpsip_protocol_init (TpsipProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TPSIP_TYPE_PROTOCOL,
      TpsipProtocolPrivate);
}

static const TpCMParamSpec *
get_parameters (TpBaseProtocol *self G_GNUC_UNUSED)
{
  return tpsip_params;
}

static TpsipConnectionKeepaliveMechanism
priv_parse_keepalive (const gchar *str)
{
  if (str == NULL || strcmp (str, "auto") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_AUTO;
  if (strcmp (str, "register") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_REGISTER;
  if (strcmp (str, "options") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_OPTIONS;
  if (strcmp (str, "stun") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_STUN;
  if (strcmp (str, "off") == 0)
    return TPSIP_CONNECTION_KEEPALIVE_NONE;

  WARNING ("unsupported keepalive-method value \"%s\", falling back to auto", str);
  return TPSIP_CONNECTION_KEEPALIVE_AUTO;
}

static gchar *
priv_compose_proxy_uri (const gchar *host,
                        const gchar *transport,
                        guint port)
{
  const gchar *scheme = "sip";

  if (host == NULL)
    return NULL;

  /* Set scheme to SIPS if transport is TLS */

  if (transport != NULL && !g_ascii_strcasecmp (transport, "tls"))
    scheme = "sips";

  /* Format the resulting URI */

  if (port)
    return g_strdup_printf ("%s:%s:%u", scheme, host, port);
  else
    return g_strdup_printf ("%s:%s", scheme, host);
}

/**
 * Returns a default SIP proxy address based on the public 
 * SIP address 'sip_address' and . For instance
 * "sip:first.surname@company.com" would result in "sip:company.com".
 * The SIP stack will further utilize DNS lookups to find the IP address 
 * for the SIP server responsible for the domain "company.com".
 */
static gchar *
priv_compose_default_proxy_uri (const gchar *sip_address,
                                const gchar *transport)
{
  char *result = NULL;
  char *host;
  char *found;

  g_return_val_if_fail (sip_address != NULL, NULL);

  /* skip sip and sips prefixes, updating transport if necessary */
  found = strchr (sip_address, ':');
  if (found != NULL) {
    if (g_ascii_strncasecmp ("sip:", sip_address, 4) == 0)
      ;
    else if (g_ascii_strncasecmp ("sips:", sip_address, 5) == 0)
      {
        if (transport == NULL ||
            strcmp (transport, "auto") == 0)
          transport = "tls";
      }
    else
      {
        /* error, unknown URI prefix */
        return NULL;
      }

    sip_address = found + 1;
  }

  /* skip userinfo */
  found = strchr (sip_address, '@');
  if (found != NULL)
    sip_address = found + 1;

  /* copy rest of the string */
  host = g_strdup (sip_address);

  /* mark end (before uri-parameters defs or headers) */
  found = strchr (host, ';');
  if (found == NULL)
    found = strchr (host, '?');
  if (found != NULL)
    *found = '\0';

  result = priv_compose_proxy_uri (host, transport, 0);

  g_free (host);

  return result;
}

static TpBaseConnection *
new_connection (TpBaseProtocol *protocol,
                GHashTable *params,
                GError **error)
{
  TpsipProtocol *self = TPSIP_PROTOCOL (protocol);
  TpsipConnection *conn;
  guint i;
  const gchar *account;
  const gchar *transport;
  const gchar *proxy_host;
  guint16 port;
  gchar *proxy;
  TpsipConnectionKeepaliveMechanism keepalive_mechanism;

  account = tp_asv_get_string (params, "account");
  transport = tp_asv_get_string (params, "transport");
  port = tp_asv_get_uint32 (params, "port", NULL);

  conn = g_object_new (TPSIP_TYPE_CONNECTION,
                       "protocol", PROTOCOL_NAME,
                       "sofia-root", self->priv->sofia_root,
                       "address", account,
                       NULL);

  proxy_host = tp_asv_get_string (params, "proxy-host");

  if (tp_str_empty (proxy_host))
    {
      proxy = priv_compose_default_proxy_uri (account, transport);
      DEBUG("set outbound proxy address to <%s>, based on <%s>", proxy,
          account);
    }
  else
    {
      proxy = priv_compose_proxy_uri (proxy_host, transport, port);
    }

  g_object_set (conn,
      "proxy", proxy,
      NULL);
  g_free (proxy);

  if (!tp_str_empty (transport) && tp_strdiff (transport, "auto"))
    g_object_set (conn,
        "transport", transport,
        NULL);

  for (i = 0; tpsip_params[i].name != NULL; i++)
    {
      if (tpsip_params[i].offset == PARAM_SET_SEPARATELY)
        {
          DEBUG ("Parameter %s is handled specially", tpsip_params[i].name);
          continue;
        }

      g_assert (tpsip_params[i].offset == PARAM_EASY);

      switch (tpsip_params[i].gtype)
        {
          case G_TYPE_STRING:
              {
                const gchar *s = tp_asv_get_string (params,
                    tpsip_params[i].name);

                if (!tp_str_empty (s))
                  g_object_set (conn,
                      tpsip_params[i].name, s,
                      NULL);
              }
            break;

          case G_TYPE_UINT:
              {
                gboolean valid = FALSE;
                guint u = tp_asv_get_uint32 (params,
                    tpsip_params[i].name, &valid);

                if (valid)
                  g_object_set (conn,
                      tpsip_params[i].name, u,
                      NULL);
              }
            break;

          case G_TYPE_BOOLEAN:
              {
                gboolean valid = FALSE;
                gboolean b = tp_asv_get_boolean (params, tpsip_params[i].name,
                    &valid);

                if (valid)
                  g_object_set (conn,
                      tpsip_params[i].name, b,
                      NULL);
              }
            break;

          default:
            /* no other parameters have been written yet */
            g_assert_not_reached ();
        }
    }

  keepalive_mechanism = priv_parse_keepalive (tp_asv_get_string (params,
        "keepalive-mechanism"));
  g_object_set (conn,
      "keepalive-mechanism", keepalive_mechanism,
      NULL);

  return TP_BASE_CONNECTION (conn);
}

static gchar *
normalize_contact (TpBaseProtocol *self G_GNUC_UNUSED,
                   const gchar *contact,
                   GError **error)
{
  return tpsip_normalize_contact (contact, NULL, NULL, error);
}

static gchar *
identify_account (TpBaseProtocol *self G_GNUC_UNUSED,
    GHashTable *asv,
    GError **error)
{
  const gchar *account = tp_asv_get_string (asv, "account");

  g_assert (account != NULL);
  return g_strdup (account);
}

static GStrv
get_interfaces (TpBaseProtocol *self)
{
  return g_new0 (gchar *, 1);
}

static void
get_connection_details (TpBaseProtocol *self,
    GStrv *connection_interfaces,
    GType **channel_managers,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
          (GStrv) tpsip_connection_get_implemented_interfaces ());
    }

  if (channel_managers != NULL)
    {
      GType types[] = {
          TPSIP_TYPE_TEXT_FACTORY,
          TPSIP_TYPE_MEDIA_FACTORY,
          G_TYPE_INVALID };

      *channel_managers = g_memdup (types, sizeof(types));
    }

  if (icon_name != NULL)
    {
      *icon_name = g_strdup (ICON_NAME);
    }

  if (vcard_field != NULL)
    {
      *vcard_field = g_strdup (VCARD_FIELD_NAME);
    }

  if (english_name != NULL)
    {
      *english_name = g_strdup (ENGLISH_NAME);
    }
}

static GStrv
dup_authentication_types (TpBaseProtocol *base)
{
  const gchar * const types[] = {
    TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
    NULL
  };

  return g_strdupv ((GStrv) types);
}

static void
tpsip_protocol_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpsipProtocol *self = TPSIP_PROTOCOL (object);

  switch (property_id)
    {
      case PROP_SOFIA_ROOT:
        g_value_set_pointer (value, self->priv->sofia_root);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tpsip_protocol_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  TpsipProtocol *self = TPSIP_PROTOCOL (object);

  switch (property_id)
    {
      case PROP_SOFIA_ROOT:
        self->priv->sofia_root = g_value_get_pointer (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
tpsip_protocol_class_init (TpsipProtocolClass *klass)
{
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (TpsipProtocolPrivate));

  base_class->get_parameters = get_parameters;
  base_class->new_connection = new_connection;
  base_class->normalize_contact = normalize_contact;
  base_class->identify_account = identify_account;
  base_class->get_interfaces = get_interfaces;
  base_class->get_connection_details = get_connection_details;
  base_class->dup_authentication_types = dup_authentication_types;

  object_class->get_property = tpsip_protocol_get_property;
  object_class->set_property = tpsip_protocol_set_property;

  param_spec = g_param_spec_pointer ("sofia-root", "Sofia-SIP root",
      "the root object for Sofia-SIP",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SOFIA_ROOT,
      param_spec);
}

TpBaseProtocol *
tpsip_protocol_new (su_root_t *sofia_root)
{
  return g_object_new (TPSIP_TYPE_PROTOCOL,
      "name", PROTOCOL_NAME,
      "sofia-root", sofia_root,
      NULL);
}
