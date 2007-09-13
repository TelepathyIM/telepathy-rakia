/*
 * sip-connection-sofia.c - Source for SIPConnection Sofia event handling
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2007 Collabora Ltd.
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
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

#include <dbus/dbus-glib.h>

#include <telepathy-glib/interfaces.h>

#include "sip-connection-sofia.h"
#include "sip-connection-private.h"
#include "media-factory.h"
#include "text-factory.h"

#include <sofia-sip/sip_header.h>
#include <sofia-sip/sip_parser.h>
#include <sofia-sip/sip_status.h>
#include <sofia-sip/msg_parser.h>
#include <sofia-sip/msg_types.h>
#include <sofia-sip/su_tag_io.h> /* for tl_print() */

#define DEBUG_FLAG SIP_DEBUG_CONNECTION
#include "debug.h"

SIPConnectionSofia *
sip_connection_sofia_new (SIPConnection *conn)
{
  SIPConnectionSofia *sofia = g_slice_new0 (SIPConnectionSofia);
  sofia->conn = conn;
  return sofia;
}

static void
priv_r_shutdown(int status,
                char const *phrase, 
                nua_t *nua,
                SIPConnectionSofia *sofia)
{
  GSource *source;
  gboolean source_recursive;

  DEBUG("nua_shutdown: %03d %s", status, phrase);

  if (status < 200)
    return;

  g_assert(sofia->conn == NULL);

  source = su_root_gsource (sofia->sofia_root);

  /* XXX: temporarily allow recursion in the Sofia source to work around
   * nua_destroy() requiring nested mainloop iterations to complete
   * (Sofia-SIP bug #1624446). Actual recursion safety of the source is to be
   * examined. */
  source_recursive = g_source_get_can_recurse (source);
  if (!source_recursive)
    {
      DEBUG("forcing Sofia root GSource to be recursive");
      g_source_set_can_recurse (source, TRUE);
    }

  DEBUG("destroying Sofia-SIP NUA at address %p", nua);
  nua_destroy (nua);

  if (!source_recursive)
    g_source_set_can_recurse (source, FALSE);

  g_slice_free (SIPConnectionSofia, sofia);
}

/* We have a monster auth handler method with a traffic light
 * return code. Might think about refactoring it someday... */
typedef enum {
  SIP_AUTH_FAILURE,
  SIP_AUTH_PASS,
  SIP_AUTH_HANDLED
} SIPAuthStatus;

static SIPAuthStatus
priv_handle_auth (SIPConnection* self,
                  int status,
                  nua_handle_t *nh,
                  const sip_t *sip,
                  gboolean home_realm)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  sip_www_authenticate_t const *wa;
  sip_proxy_authenticate_t const *pa;
  const char *method = NULL;
  const char *realm = NULL;
  const char *user =  NULL;
  const char *password =  NULL;
  gchar *auth = NULL;

  DEBUG("enter");

  if (status != 401 && status != 407)
    {
      /* Clear the last used credentials saved for loop detection
       * and proceed with normal handling */
      if (priv->last_auth != NULL)
        {
          g_free (priv->last_auth);
          priv->last_auth = NULL;
        }
      return SIP_AUTH_PASS;
    }

  wa = sip->sip_www_authenticate;
  pa = sip->sip_proxy_authenticate;

  /* step: figure out the realm of the challenge */
  if (wa) {
    realm = msg_params_find(wa->au_params, "realm=");
    method = wa->au_scheme;
  }
  else if (pa) {
    realm = msg_params_find(pa->au_params, "realm=");
    method = pa->au_scheme;
  }

  if (realm == NULL)
    {
      g_warning ("no realm presented for authentication");
      return SIP_AUTH_FAILURE;
    }

  /* step: determine which set of credentials to use */
  if (home_realm)
    {
      /* Save the realm presented by the registrar */
      if (priv->registrar_realm == NULL)
        priv->registrar_realm = g_strdup (realm);
      else if (wa && strcmp(priv->registrar_realm, realm) != 0)
        {
          g_message ("registrar realm changed from '%s' to '%s'", priv->registrar_realm, realm);
          g_free (priv->registrar_realm);
          priv->registrar_realm = g_strdup (realm);
        }
    }
  else if (priv->registrar_realm != NULL
           && strcmp(priv->registrar_realm, realm) == 0)
    home_realm = TRUE;

  if (home_realm)
    {
      /* use authentication username if provided */
      user = priv->auth_user;
      password = priv->password;

      DEBUG("using the primary auth credentials");
    }
  else
    {
      if (priv->extra_auth_user)
        user = priv->extra_auth_user;
      else
        /* fall back to the main username */
        user = priv->auth_user;
      password = priv->extra_auth_password;

      DEBUG("using the extra auth credentials");
    }

  if (user == NULL)
    {
      sip_from_t const *sipfrom = sip->sip_from;
      if (sipfrom && sipfrom->a_url)
        /* or use the userpart in "From" header */
        user = sipfrom->a_url->url_user;
    }

  if (password == NULL)
    password = "";

  /* step: if all info is available, create an authorization response */
  g_assert (realm != NULL);
  if (user && method) {
    if (realm[0] == '"')
      auth = g_strdup_printf ("%s:%s:%s:%s", 
			      method, realm, user, password);
    else
      auth = g_strdup_printf ("%s:\"%s\":%s:%s", 
			      method, realm, user, password);

    DEBUG("%s authenticating user='%s' realm='%s' nh='%p'",
	       wa ? "server" : "proxy", user, realm, nh);
  }

  /* step: do sanity checks, avoid resubmitting the exact same response */
  /* XXX: we don't check if the nonce is the same, presumably should be
   * taken care of by the stack */
  if (auth == NULL)
    g_warning ("sofiasip: authentication data are incomplete");
  else if (priv->last_auth != NULL && strcmp (auth, priv->last_auth) == 0)
    {
      g_debug ("authentication challenge repeated, dropping");
      g_free (auth);
      auth = NULL;
    }
  else
    {
      /* Save the credential string, taking ownership */
      g_free (priv->last_auth);
      priv->last_auth = auth;
    }

  if (auth == NULL)
    return SIP_AUTH_FAILURE;

  /* step: authenticate */
  nua_authenticate(nh, NUTAG_AUTH(auth), TAG_END());

  return SIP_AUTH_HANDLED;
}

static void
priv_r_invite (int status,
               char const *phrase,
               nua_t *nua,
               SIPConnection *self,
               nua_handle_t *nh,
               nua_handle_t *nh_magic,
               sip_t const *sip,
               tagi_t tags[])
{
  DEBUG("outbound INVITE got %03d %s", status, phrase);

  /* Ignore provisional responses in this handler.
   * NOTE: these responses will not also reset the auth credentials
   * for priv_handle_auth() */
  if (status < 200)
    return;

  if (nh_magic == SIP_NH_EXPIRED)
    {
      /* 487 Request Terminated is OK on destroyed channels */
      if (status != 487)
        g_message ("response %03d received for a destroyed media channel", status);
      return;
    }

  if (nh_magic == NULL)
    {
      g_message ("response %03d, on a NUA handle %p which does "
                 "not belong to any media channel, ignored", status, nh);
      return;
    }

  if (priv_handle_auth (self, status, nh, sip, FALSE) == SIP_AUTH_HANDLED)
    return;

  if (status >= 300)
    sip_media_channel_peer_error (SIP_MEDIA_CHANNEL (nh_magic),
                                  status,
                                  phrase);
}

static void
priv_r_register (int status,
                 char const *phrase, 
                 nua_t *nua,
                 SIPConnection *self,
                 nua_handle_t *nh,
                 sip_t const *sip,
                 tagi_t tags[])
{
  TpBaseConnection *base = (TpBaseConnection *)self;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  DEBUG("enter");

  g_message ("sofiasip: REGISTER: %03d %s", status, phrase);

  if (status < 200) {
    return;
  }

  switch (priv_handle_auth (self, status, nh, sip, TRUE))
    {
    case SIP_AUTH_FAILURE:
      g_message ("sofiasip: REGISTER failed, insufficient/wrong credentials, disconnecting.");
      tp_base_connection_change_status (base, TP_CONNECTION_STATUS_DISCONNECTED,
                TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
      return;
    case SIP_AUTH_HANDLED:
      return;
    case SIP_AUTH_PASS:
      break;
    }

  if (status == 403) {
    g_message ("sofiasip: REGISTER failed, wrong credentials, disconnecting.");
    tp_base_connection_change_status (base, TP_CONNECTION_STATUS_DISCONNECTED,
                TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
  }
  else if (status >= 300) {
    g_message ("sofiasip: REGISTER failed, disconnecting.");
    tp_base_connection_change_status (base, TP_CONNECTION_STATUS_DISCONNECTED,
                TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  }
  else /* if (status == 200) */ {
    g_message ("sofiasip: succesfully registered %s to network", priv->address);
    priv->register_succeeded = TRUE;
    tp_base_connection_change_status (base, TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_REQUESTED);
  }
}

static void
priv_r_unregister (int status,
                   char const *phrase,
                   nua_handle_t *nh)
{
  DEBUG("un-REGISTER: %03d %s", status, phrase);

  if (status < 200)
    return;

  if (status == 401 || status == 407)
    {
      /* In SIP, de-registration can fail! However, there's not a lot we can
       * do about this in the Telepathy model - once you've gone DISCONNECTED
       * you're really not meant to go "oops, I'm still CONNECTED after all".
       * So we ignore it and hope it goes away. */
      g_warning ("Registrar won't let me unregister: %d %s", status, phrase);
    }
}

static void
priv_r_get_params (int status,
                   char const *phrase,
                   nua_t *nua,
                   SIPConnection *self,
                   nua_handle_t *nh,
                   sip_t const *sip,
                   tagi_t tags[])
{
  DEBUG("nua_r_get_params: %03d %s", status, phrase);

  if (status < 200)
    return;

  /* note: print contents of all tags to stdout */
  tl_print(stdout, "tp-sofiasip stack parameters:\n", tags);
}

static TpHandle
priv_handle_parse_from (const sip_t *sip,
                        su_home_t *home,
                        TpHandleRepoIface *contact_repo)
{
  TpHandle handle = 0;
  gchar *url_str;

  g_return_val_if_fail (sip != NULL, 0);

  if (sip->sip_from)
    {
      url_str = url_as_string (home, sip->sip_from->a_url);

      handle = tp_handle_ensure (contact_repo, url_str, NULL, NULL);

      su_free (home, url_str);

      /* TODO: set qdata for the display name */
    }

  return handle;
}

static TpHandle
priv_handle_parse_to (const sip_t *sip,
                      su_home_t *home,
                      TpHandleRepoIface *contact_repo)
{
  TpHandle handle = 0;
  gchar *url_str;
  
  g_return_val_if_fail (sip != NULL, 0);

  if (sip->sip_to)
    {
      url_str = url_as_string (home, sip->sip_to->a_url);

      handle = tp_handle_ensure (contact_repo, url_str, NULL, NULL);

      su_free (home, url_str);

      /* TODO: set qdata for the display name */
    }

  return handle;
}

static void
priv_r_message (int status,
                char const *phrase,
                nua_t *nua,
                SIPConnection *self,
                nua_handle_t *nh,
                sip_t const *sip,
                tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPTextChannel *channel;
  TpHandleRepoIface *contact_repo;
  TpHandle handle;

  DEBUG("nua_r_message: %03d %s", status, phrase);

  if (priv_handle_auth (self, status, nh, sip, FALSE) == SIP_AUTH_HANDLED)
    return;

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)self, TP_HANDLE_TYPE_CONTACT);

  handle = priv_handle_parse_to (sip, priv->sofia_home, contact_repo);

  if (!handle)
    {
      g_warning ("Message apparently delivered to an invalid recipient, ignoring");
      return;
    }

  if (status == 200)
    DEBUG("Message delivered for <%s>",
          tp_handle_inspect (contact_repo, handle));

  channel = sip_text_factory_lookup_channel (priv->text_factory, handle);

  tp_handle_unref (contact_repo, handle);

  if (!channel)
    g_warning ("Delivery status ignored for a non-existant channel");
  else if (status >= 200)
    sip_text_channel_emit_message_status (channel, nh, status);
}

static void
priv_i_invite (int status,
               char const *phrase,
               nua_t *nua,
               SIPConnection *self,
               nua_handle_t *nh,
               nua_hmagic_t *nh_magic,
               sip_t const *sip,
               tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPMediaChannel *channel;
  TpHandleRepoIface *contact_repo;
  TpHandle handle;
  GError *error = NULL;

  if (nh_magic == SIP_NH_EXPIRED)
    {
      g_message ("incoming INVITE for a destroyed media channel");
      nua_respond (nh, 481, "Call Does Not Exist", TAG_END());
      return;
    }

  if (nh_magic != NULL) {
    /* case 1: we already have a channel for this NH */
    channel = SIP_MEDIA_CHANNEL (nh_magic);
    sip_media_channel_receive_reinvite (channel);
  }
  else {
    /* case 2: we haven't seen this media session before, so we should
     * create a new channel to go with it */

    /* figure out a handle for the identity */

    contact_repo = tp_base_connection_get_handles ((TpBaseConnection *)self,
                                                   TP_HANDLE_TYPE_CONTACT);

    handle = priv_handle_parse_from (sip, priv->sofia_home, contact_repo);

    if (!handle)
      {
        g_message ("incoming INVITE with invalid sender information");
        nua_respond (nh, 400, "Invalid From address", TAG_END());
        return;
      }

    DEBUG("Got incoming invite from <%s>", 
          tp_handle_inspect (contact_repo, handle));

    channel = sip_media_factory_new_channel (
                SIP_MEDIA_FACTORY (priv->media_factory),
                NULL,
                TP_HANDLE_TYPE_NONE,
                0,
                &error);
    if (channel)
      {
        sip_media_channel_receive_invite (channel, nh, handle);
      }
    else
      {
        g_warning ("creation of SIP media channel failed: %s", error->message);
        g_error_free (error);
        nua_respond (nh, 500, sip_500_Internal_server_error, TAG_END());
      }

    tp_handle_unref (contact_repo, handle);
  }
}

static void
priv_i_cancel (nua_t *nua,
               SIPConnection *self,
               nua_handle_t *nh,
               nua_hmagic_t *nh_magic,
               sip_t const *sip,
               tagi_t tags[])
{
  if (nh_magic == NULL)
    {
      g_message ("nua_i_cancel received for an unknown handle");
      /* nua_respond (nh, SIP_500_INTERNAL_SERVER_ERROR, TAG_END()); */
      return;
    }
  if (nh_magic == SIP_NH_EXPIRED)
    {
      g_message ("CANCEL received for a destroyed media channel");
      /* nua_respond (nh, SIP_481_NO_CALL, TAG_END()); */
      return;
    }

  sip_media_channel_peer_cancel (SIP_MEDIA_CHANNEL (nh_magic));

  /* nua_respond (nh, SIP_200_OK, TAG_END()); */
}

static void
priv_i_message (int status,
                char const *phrase,
                nua_t *nua,
                SIPConnection *self,
                nua_handle_t *nh,
                sip_t const *sip,
                tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPTextChannel *channel;
  TpHandleRepoIface *contact_repo;
  TpHandle handle;
  char *text = NULL;

  /* Block anything else except text/plain messages (like isComposings) */
  if (sip->sip_content_type
      && (g_ascii_strcasecmp ("text/plain", sip->sip_content_type->c_type)))
    {
      nua_respond (nh, SIP_415_UNSUPPORTED_MEDIA,
                   SIPTAG_ACCEPT_STR("text/plain"),
                   NUTAG_WITH_THIS(nua),
                   TAG_END());
      return;
    }

  /* If there is some text, assure it's in UTF-8 encoding */
  if (sip->sip_payload && sip->sip_payload->pl_len > 0)
    {
      const char *charset = NULL;
      if (sip->sip_content_type && sip->sip_content_type->c_params != 0)
        {
          charset = msg_params_find (sip->sip_content_type->c_params, "charset=");
        }

      /* Default charset is UTF-8, we only need to convert if it's a different one */
      if (charset && g_ascii_strcasecmp (charset, "UTF-8"))
        {
          GError *error;
          gsize in_len, out_len;
          text = g_convert (sip->sip_payload->pl_data, sip->sip_payload->pl_len,
              "UTF-8", charset, &in_len, &out_len, &error);

          if (text == NULL)
            {
              nua_respond (nh, 500, error->message,
                           NUTAG_WITH_THIS(nua),
                           TAG_END());
              g_error_free (error);
              return;
            }
          if (in_len != sip->sip_payload->pl_len)
            {
              nua_respond (nh, 400, "Incomplete character sequence at the "
                                    "end of the message body",
                           NUTAG_WITH_THIS(nua),
                           TAG_END());
              goto end;
            }
        }
      else
        {
          if (!g_utf8_validate (sip->sip_payload->pl_data,
                                sip->sip_payload->pl_len,
                                NULL))
            {
              nua_respond (nh, 400, "Invalid characters in the message body",
                           NUTAG_WITH_THIS(nua),
                           TAG_END());
              return;
            }
          text = g_strndup (sip->sip_payload->pl_data, sip->sip_payload->pl_len);
        }
    }
  else
    {
      text = g_strdup ("");
    }

  contact_repo = tp_base_connection_get_handles (
      (TpBaseConnection *)self, TP_HANDLE_TYPE_CONTACT);

  handle = priv_handle_parse_from (sip, priv->sofia_home, contact_repo);

  if (handle)
    {
      DEBUG("Got incoming message from <%s>", 
	    tp_handle_inspect (contact_repo, handle));

      channel = sip_text_factory_lookup_channel (priv->text_factory, handle);

      if (!channel)
        {
          channel = sip_text_factory_new_channel (priv->text_factory, handle,
              NULL);
          g_assert (channel != NULL);
        }

      sip_text_channel_receive (channel, handle, text);

      tp_handle_unref (contact_repo, handle);

      nua_respond (nh, SIP_200_OK, NUTAG_WITH_THIS(nua), TAG_END());
    }
  else
    {
      g_warning ("Incoming message has invalid sender information");
      nua_respond (nh, 400, "Invalid From address",
                   NUTAG_WITH_THIS(nua),
                   TAG_END());
    }

end:
  g_free (text);
}

static void
priv_i_state (int status,
              char const *phrase,
              nua_t *nua,
              SIPConnection *self,
              nua_handle_t *nh,
              nua_hmagic_t *nh_magic,
              sip_t const *sip,
              tagi_t tags[])
{
  const sdp_session_t *r_sdp = NULL;
  int offer_recv = 0;
  int answer_recv = 0;
  int ss_state = nua_callstate_init;
  SIPMediaChannel *channel;

  DEBUG("nua_i_state: %03d %s", status, phrase);

  if (nh_magic == NULL)
    {
      g_warning ("nua_i_state received for an unknown handle %p, ignored", nh);
      return;
    }

  tl_gets(tags,
          NUTAG_CALLSTATE_REF(ss_state),
          NUTAG_OFFER_RECV_REF(offer_recv),
          NUTAG_ANSWER_RECV_REF(answer_recv),
          SOATAG_REMOTE_SDP_REF(r_sdp),
          TAG_END());

  if (nh_magic == SIP_NH_EXPIRED)
    {
      g_message ("call state change %03d '%s' received for a "
          "destroyed media channel (handle %p), BYE off", status, phrase, nh);
      if (ss_state != nua_callstate_terminated)
        nua_bye (nh, TAG_END());
      /* Destroy it? */
      return;
    }

  channel = nh_magic;

  if (r_sdp)
    {
      g_return_if_fail(answer_recv || offer_recv);
      if (!sip_media_channel_set_remote_media (channel, r_sdp))
        sip_media_channel_close (channel);
    }

  DEBUG("call with handle %p is %s", nh, nua_callstate_name (ss_state));

  switch ((enum nua_callstate)ss_state) {
  case nua_callstate_received:
  case nua_callstate_early:
    break;
    
  case nua_callstate_completing:
    /* In auto-ack mode, we don't need to call nua_ack(), see NUTAG_AUTOACK() */
    break;

  case nua_callstate_ready:
    sip_media_channel_ready (channel);
    break;

  case nua_callstate_terminated:
    sip_media_channel_terminated (channel);
    break;

  default:
    break;
  }
}

static inline const char *
classify_nh_magic (nua_hmagic_t *nh_magic)
{
  if (nh_magic == NULL)
    {
      return "no channel yet";
    }
  if (nh_magic == SIP_NH_EXPIRED)
    {
      return "channel has been destroyed";
    }
  return "SIPMediaChannel";
}

/**
 * Callback for events delivered by the SIP stack.
 *
 * See libsofia-sip-ua/nua/nua.h documentation.
 */
void
sip_connection_sofia_callback(nua_event_t event,
			      int status,
			      char const *phrase,
			      nua_t *nua,
			      SIPConnectionSofia *state,
			      nua_handle_t *nh,
			      nua_hmagic_t *nh_magic,
			      sip_t const *sip,
			      tagi_t tags[])
{
  SIPConnection *self;

  g_return_if_fail (state);

  if (event == nua_r_shutdown)
    {
      priv_r_shutdown (status, phrase, nua, state);
      return;
    }
  else if (event == nua_r_unregister)
    {
      priv_r_unregister (status, phrase, nh);
      return;
    }

  self = state->conn;
  if (self == NULL)
    {
      g_warning ("post-shutdown event received for a connection: NUA at %p, event #%d '%s', %d '%s'",
                 nua, event, nua_event_name (event), status, phrase);
      return;
    }

  DEBUG("enter: NUA at %p (conn %p), event #%d '%s', %d '%s'", nua, self,
      event, nua_event_name (event), status, phrase);
  DEBUG ("Connection refcount is %d", ((GObject *)self)->ref_count);

  switch (event) {
    
    /* incoming requests
     * ------------------------- */

  case nua_i_fork:
    /* self_i_fork(status, phrase, nua, self, nh, sip, tags); */
    break;
    
  case nua_i_invite:
    priv_i_invite (status, phrase, nua, self, nh, nh_magic, sip, tags);
    break;

  case nua_i_state:
    priv_i_state (status, phrase, nua, self, nh, nh_magic, sip, tags);
    break;

  case nua_i_bye:
    /* self_i_bye(nua, self, nh, sip, tags); */
    break;

  case nua_i_message:
    priv_i_message(status, phrase, nua, self, nh, sip, tags);
    break;

  case nua_i_refer:
    /* self_i_refer(nua, self, nh, sip, tags); */
    break;

  case nua_i_notify:
    /* self_i_notify(nua, self, nh, sip, tags); */
    break;

  case nua_i_cancel:
    priv_i_cancel (nua, self, nh, nh_magic, sip, tags);
    break;

  case nua_i_error:
    /* self_i_error(nua, self, nh, status, phrase, tags); */
    break;

  case nua_i_active:
  case nua_i_ack:
  case nua_i_terminated:
    /* ignore these */
    break;

    /* responses to our requests 
     * ------------------------- */

  case nua_r_register:
    priv_r_register (status, phrase, nua, self, nh, sip, tags);
    break;
    
  case nua_r_invite:
    priv_r_invite(status, phrase, nua, self, nh, nh_magic, sip, tags);
    break;

  case nua_r_bye:
    /* self_r_bye(status, phrase, nua, self, nh, sip, tags); */
    break;

  case nua_r_message:
    priv_r_message(status, phrase, nua, self, nh, sip, tags);
    break;

  case nua_r_cancel:
    /* priv_r_cancel (status, phrase, nua, self, nh, sip, tags); */
    break;

  case nua_r_refer:
    /* self_r_refer(status, phrase, nua, self, nh, sip, tags); */
    break;

  case nua_r_subscribe:
    /* self_r_subscribe(status, phrase, nua, self, nh,  sip, tags); */
    break;

  case nua_r_unsubscribe:
    /* self_r_unsubscribe(status, phrase, nua, self, nh, sip, tags); */
    break;

  case nua_r_publish:
    /* self_r_publish(status, phrase, nua, self, nh, sip, tags); */
    break;
    
  case nua_r_notify:
    /* self_r_notify(status, phrase, nua, self, nh, sip, tags); */
    break;

  case nua_r_get_params:
    priv_r_get_params(status, phrase, nua, self, nh, sip, tags);
    break;
     
  default:
    g_message ("sip-connection: unknown event #%d '%s', status %03d '%s' "
        " (nh=%p)", event, nua_event_name(event), status, phrase, nh);

    if (nh_magic == SIP_NH_EXPIRED)
      {
        /* note: unknown handle, not associated to any existing 
         *       call, message, registration, etc, so it can
         *       be safely destroyed */
        g_message ("NOTE: destroying NUA handle %p (its associated "
            "channel has already gone away)", nh);
        nua_handle_ref (nh);
        nua_handle_destroy (nh);
      }

    break;
  }

  DEBUG ("exit");
}


#if 0
/* XXX: these methods have not yet been ported to the new NUA API */

static void
cb_subscribe_answered(NuaGlib* obj, NuaGlibOp *op, int status, const char*message, gpointer data)
{
  SIPConnection *sipconn = (SIPConnection *)data;

  if (sipconn);

  g_message ("Subscribe answered: %d with status %d with message %s",
             nua_glib_op_method_type(op),
             status, message);

  /* XXX -- mela: emit a signal to our client */
}

static void 
cb_incoming_notify(NuaGlib *sofia_nua_glib, NuaGlibOp *op, const char *event,
		   const char *content_type, const char *message, gpointer data)
{
  SIPConnection *self = (SIPConnection *) data;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPTextChannel *channel;
  TpHandle handle;

  handle = 1;
  channel = NULL;
  if (priv);

}

static void cb_call_terminated(NuaGlib *sofia_nua_glib, NuaGlibOp *op, int status, gpointer data)
{
  SIPConnection *self = (SIPConnection *) data;

  DEBUG("enter");

  /* as we only support one media channel at a time, terminate all */
  sip_conn_close_media_channels (self);
}

#endif
