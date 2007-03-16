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
#include "sip-connection-helpers.h"
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

static void
priv_disconnect (SIPConnection *self, TpConnectionStatusReason reason)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  TpBaseConnection *base = (TpBaseConnection *)self;

  tp_base_connection_change_status (base, TP_CONNECTION_STATUS_DISCONNECTED,
      reason);
  if (priv->register_op != NULL)
    {
      nua_handle_destroy (priv->register_op);
      priv->register_op = NULL;
    }
}

static gboolean
priv_authenticate (nua_handle_t *nh,
                   const sip_t *sip,
                   const char *user,
                   const char *password)
{
  sip_www_authenticate_t const *wa = sip->sip_www_authenticate;
  sip_proxy_authenticate_t const *pa = sip->sip_proxy_authenticate;
  const char *realm = NULL;
  const char *method = NULL;

  DEBUG("enter");

  if (password == NULL)
    password = "";

  /* step: figure out the realm of the challenge */
  if (wa) {
    realm = msg_params_find(wa->au_params, "realm=");  
    method = wa->au_scheme;
  }
  else if (pa) {
    realm = msg_params_find(pa->au_params, "realm=");  
    method = pa->au_scheme;
  }

  /* step: if all info is available, add an authorization response */
  if (user && realm && method) {
    gchar *auth;

    if (realm[0] == '"')
      auth = g_strdup_printf ("%s:%s:%s:%s", 
			      method, realm, user, password);
    else
      auth = g_strdup_printf ("%s:\"%s\":%s:%s", 
			      method, realm, user, password);

    g_message ("sofiasip: %s authenticating user='%s' realm='%s' nh='%p'",
	       wa ? "server" : "proxy", user, realm, nh);

    nua_authenticate(nh, NUTAG_AUTH(auth), TAG_END());

    g_free (auth);

    return TRUE;
  } else {
    g_warning ("sofiasip: authentication data are incomplete");
    return FALSE;
  }
}

/**
 * Handles authentication challenge for REGISTER operations.
 */
static gboolean
priv_handle_auth_register (SIPConnection *self,
                           nua_handle_t *nh,
                           const sip_t *sip)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  sip_from_t const *sipfrom = sip->sip_from;
  sip_from_t const *sipto = sip->sip_to;
  const char *user =  NULL;

  /* use the userpart in "From" header */
  if (sipfrom && sipfrom->a_url)
    user = sipfrom->a_url->url_user;

  /* alternatively use the userpart in "To" header */
  if (!user && sipto && sipto->a_url)
    user = sipto->a_url->url_user;

  if (!user)
    {
      g_warning ("could not determine user name for registration");
      return FALSE;
    }

  return priv_authenticate (nh, sip, user, priv->password);
}

/**
 * Handles authentication challenges for non-REGISTER operations.
 */
static gboolean
priv_handle_auth_extra (SIPConnection *self,
                        nua_handle_t *nh,
                        const sip_t *sip)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);

  if (priv->extra_auth_user == NULL) {
    g_message ("no credentials for non-REGISTER authentication");
    return FALSE;
  }

  /* XXX: no security checks are made on the origin of the challenge.
   * Sending a digest of the single password to anybody who asks for it
   * is a mild security risk. Exposing the single username to an unintended
   * recipient may be an even bigger issue. */
  /* Shall we gleefully substitute the registration password and username
   * if the extra credentials are not given? */

  return priv_authenticate (nh, sip,
                            priv->extra_auth_user, priv->extra_auth_password);
}

static void
priv_emit_remote_error (SIPConnection *self,
			nua_handle_t *nh,
			TpHandle handle,
			int status,
			char const *phrase)
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPMediaChannel *channel = sip_media_factory_get_only_channel (
      priv->media_factory);

  if (channel == NULL)
    {
      if (status != 487)
        g_message ("error response %03d received for a destroyed media channel", status);
      return;
    }

  /* XXX: more checks on the handle belonging to channel membership */
  g_return_if_fail (handle > 0);

  sip_media_channel_peer_error (channel, status, phrase);
}

static void priv_r_invite(int status, char const *phrase, 
			  nua_t *nua, SIPConnection *self,
			  nua_handle_t *nh, nua_hmagic_t *hmagic, sip_t const *sip,
			  tagi_t tags[])
{
  DEBUG("enter");

  g_message ("sofiasip: outbound INVITE: %03d %s", status, phrase);
   
  if (status >= 300) {
    if (status == 401 || status == 407) {
      if (priv_handle_auth_extra (self, nh, sip))
        return;
    }

    /* redirects (3xx responses) are not handled properly */
    /* smcv-FIXME: need to work out which channel we're dealing with here */
    priv_emit_remote_error (self, nh, GPOINTER_TO_UINT(hmagic), status, phrase);
  }
}

static void
priv_r_register (int status,
                 char const *phrase, 
                 nua_t *nua,
                 SIPConnection *self,
                 nua_handle_t *nh,
                 nua_hmagic_t *hmagic,
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

  if (status == 401 || status == 407) {
    if (!priv_handle_auth_register (self, nh, sip)) {
      g_message ("sofiasip: REGISTER failed, insufficient/wrong credentials, disconnecting.");
      priv_disconnect (self, TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
    }
  }
  else if (status == 403) {
    g_message ("sofiasip: REGISTER failed, wrong credentials, disconnecting.");
    priv_disconnect (self, TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
  }
  else if (status >= 300) {
    g_message ("sofiasip: REGISTER failed, disconnecting.");
    priv_disconnect (self, TP_CONNECTION_STATUS_REASON_NETWORK_ERROR);
  }
  else if (status == 200) {
    g_message ("sofiasip: succesfully registered %s to network", priv->address);
    tp_base_connection_change_status (base, TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_REQUESTED);
  }
}

static void
priv_r_unregister (int status,
                   char const *phrase,
                   nua_t *nua,
                   SIPConnection *self,
                   nua_handle_t *nh,
                   nua_hmagic_t *hmagic,
                   sip_t const *sip,
                   tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  nua_handle_t *register_op;

  g_message ("sofiasip: un-REGISTER: %03d %s", status, phrase);

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

  register_op = priv->register_op;
  priv->register_op = NULL;
  if (register_op)
    nua_handle_destroy (register_op);
}

static void priv_r_shutdown(int status,
                            char const *phrase, 
                            nua_t *nua,
                            SIPConnection *self,
                            nua_handle_t *nh,
                            nua_hmagic_t *op,
                            sip_t const *sip,
                            tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  gint old_state;

  g_message("sofiasip: nua_shutdown: %03d %s", status, phrase);

  if (status < 200)
    return;

  old_state = priv->sofia_shutdown;
  priv->sofia_shutdown = SIP_NUA_SHUTDOWN_DONE;

  if (old_state == SIP_NUA_SHUTDOWN_STARTED)
    tp_base_connection_finish_shutdown ((TpBaseConnection *)self);
}

static void priv_r_get_params(int status, char const *phrase, 
			      nua_t *nua, SIPConnection *self,
			      nua_handle_t *nh, nua_hmagic_t *op, sip_t const *sip,
			      tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  sip_from_t const *from = NULL;

  g_message("sofiasip: nua_r_get_params: %03d %s", status, phrase);

  if (status < 200)
    return;

  /* note: print contents of all tags to stdout */
  tl_print(stdout, "tp-sofiasip stack parameters:\n", tags);

  tl_gets(tags, SIPTAG_FROM_REF(from), TAG_END());

  if (from) {
    char const *new_address = 
      sip_header_as_string(priv->sofia_home, (sip_header_t *)from);
    if (new_address) {
      g_message ("Updating the public SIP address to %s.\n", new_address);
      g_free (priv->address);
      priv->address = g_strdup(new_address);
    }      
  }
}

static gboolean priv_parse_sip_to(sip_t const *sip, su_home_t *home,
				  const gchar **to_str,
				  gchar **to_url_str)
{
  if (sip && sip->sip_to) {
    *to_str = sip->sip_to->a_display;
    *to_url_str = url_as_string(home, sip->sip_to->a_url);

    return TRUE;
  }

  return FALSE;
}

static gboolean priv_parse_sip_from (sip_t const *sip, su_home_t *home, const gchar **from_str, gchar **from_url_str, const gchar **subject_str)
{
  if (sip && sip->sip_from) {
    *from_str = sip->sip_from->a_display;
    *from_url_str = url_as_string(home, sip->sip_from->a_url);
    *subject_str = sip->sip_subject ? sip->sip_subject->g_string : "";

    return TRUE;
  }

  return FALSE;
}


static TpHandle
priv_parse_handle (SIPConnection *conn,
                   nua_handle_t *nh,
                   TpHandle ihandle,
                   const gchar *from_url_str)
{
  TpBaseConnection *base = (TpBaseConnection *)conn;
  TpHandle ohandle = ihandle;
  const gchar *handle_identity = NULL;

  /* step: check whether this is a known identity */
  if (ihandle > 0)
    handle_identity = tp_handle_inspect (
        base->handles[TP_HANDLE_TYPE_CONTACT], ihandle);

  if (handle_identity == NULL) {
    ohandle = tp_handle_request (
        base->handles[TP_HANDLE_TYPE_CONTACT], from_url_str, TRUE);
  }

  nua_handle_bind (nh, GUINT_TO_POINTER (ohandle));

  g_assert (ohandle > 0);

  return ohandle;
}


static void priv_r_message(int status, char const *phrase, nua_t *nua,
			   SIPConnection *self, nua_handle_t *nh,
			   nua_hmagic_t *op, sip_t const *sip,
			   tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  SIPTextChannel *channel;
  TpHandle handle = GPOINTER_TO_UINT (op);
  const gchar *to_str;
  gchar *to_url_str;
  su_home_t *home = sip_conn_sofia_home (self);

  g_message("sofiasip: nua_r_message: %03d %s", status, phrase);

  if (status == 401 || status == 407)
    if (priv_handle_auth_extra (self, nh, sip))
      return;

  if (!priv_parse_sip_to(sip, home, &to_str, &to_url_str))
    return;

  if (status == 200)
    g_message("Message delivered for %s <%s>", 
	      to_str, to_url_str);

  handle = priv_parse_handle (self, nh, handle, to_url_str);
  channel = sip_text_factory_lookup_channel (priv->text_factory, handle);

  if (channel && status >= 200)
    sip_text_channel_emit_message_status(channel, nh, status);
}


static void priv_i_invite(int status, char const *phrase, 
			  nua_t *nua, SIPConnection *self,
			  nua_handle_t *nh, nua_hmagic_t *op, sip_t const *sip,
			  tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  TpHandle handle = GPOINTER_TO_UINT (op);
  SIPMediaChannel *channel = sip_media_factory_get_only_channel (
      priv->media_factory);
  su_home_t *home = sip_conn_sofia_home (self);
  const gchar *from_str, *subject_str;
  gchar *from_url_str = NULL;

  if (priv_parse_sip_from (sip, home, &from_str, &from_url_str, &subject_str)) {

    g_message("Got incoming invite from %s <%s> on topic '%s'", 
	      from_str, from_url_str, subject_str);

    if (handle == 0) {

      /* case: a new handle */

      if (channel == NULL) {

	/* case 1: ready to establish a media session */

	/* Accordingly to lassis, NewChannel has to be emitted
	 * with the null handle for incoming calls */
	channel = sip_media_factory_new_channel (
            SIP_MEDIA_FACTORY (priv->media_factory), 0, nh, NULL);
	if (channel) {
	  /* figure out a new handle for the identity */
	  handle = priv_parse_handle (self, nh, handle, from_url_str);

	  sip_media_channel_respond_to_invite(channel, 
					      handle, 
					      subject_str,  
					      from_url_str);
	}					      
	else
	  g_message ("Creation of SIP media channel failed");
      }
      else {
	/* case 2: already have a media channel, report we are
	   busy */
	nua_respond (nh, 480, sip_480_Temporarily_unavailable, TAG_END());
      }
    }
    else {
      /* note: re-INVITEs are handled in priv_i_state() */
      g_warning ("Got a re-INVITE for handle %u", handle);
    }
  }
  else
    g_warning ("Unable to parse headers in incoming invite");  

  su_free (home, from_url_str);
}

static void priv_i_message(int status, char const *phrase, 
			   nua_t *nua, SIPConnection *self,
			   nua_handle_t *nh, nua_hmagic_t *op, sip_t const *sip,
			   tagi_t tags[])
{
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  TpChannelIface *channel;
  TpHandle handle = GPOINTER_TO_UINT (op);
  GString *message;
  const gchar *from_str, *subject_str;
  gchar *from_url_str;
  su_home_t *home = sip_conn_sofia_home (self);

  /* Block anything else except text/plain messages (like isComposings) */
  if (sip->sip_content_type && (strcmp("text/plain", sip->sip_content_type->c_type)))
    return;

  if (priv_parse_sip_from (sip, home, &from_str, &from_url_str, &subject_str)) {

    g_message("Got incoming message from %s <%s> on topic '%s'", 
	      from_str, from_url_str, subject_str);

    handle = priv_parse_handle (self, nh, handle, from_url_str);

    channel = (TpChannelIface *)sip_text_factory_lookup_channel (
        priv->text_factory, handle);

    if (!channel)
      {
        channel = (TpChannelIface *)sip_text_factory_new_channel (
            priv->text_factory, handle, NULL);
        g_assert (channel != NULL);
      }

    if (sip->sip_payload && sip->sip_payload->pl_len > 0)
      message = g_string_new_len(sip->sip_payload->pl_data, sip->sip_payload->pl_len);
    else 
      message = g_string_new ("");

    sip_text_channel_receive(SIP_TEXT_CHANNEL (channel), handle, from_str,
        from_url_str, subject_str, message->str);

    g_string_free (message, TRUE);
    su_free (home, from_url_str);
  }
  else
    g_warning ("Unable to parse headers in incoming message.");
}

static void priv_i_state(int status, char const *phrase, 
			 nua_t *nua, SIPConnection *self,
			 nua_handle_t *nh, nua_hmagic_t *op, sip_t const *sip,
			 tagi_t tags[])
{
  TpBaseConnection *base = (TpBaseConnection *)self;
  SIPConnectionPrivate *priv = SIP_CONNECTION_GET_PRIVATE (self);
  TpHandle handle = GPOINTER_TO_UINT (op);
  char const *l_sdp = NULL, *r_sdp = NULL;
  int offer_recv = 0, answer_recv = 0, offer_sent = 0, answer_sent = 0;
  int ss_state = nua_callstate_init;
  SIPMediaChannel *channel = sip_media_factory_get_only_channel (
      priv->media_factory);

  tl_gets(tags, 
	  NUTAG_CALLSTATE_REF(ss_state),
	  NUTAG_OFFER_RECV_REF(offer_recv),
	  NUTAG_ANSWER_RECV_REF(answer_recv),
	  NUTAG_OFFER_SENT_REF(offer_sent),
	  NUTAG_ANSWER_SENT_REF(answer_sent),
	  SOATAG_LOCAL_SDP_STR_REF(l_sdp),
	  SOATAG_REMOTE_SDP_STR_REF(r_sdp),
	  TAG_END());

  g_message("sofiasip: nua_state_changed: %03d %s", status, phrase);

  if (l_sdp) {
    g_return_if_fail(answer_sent || offer_sent);
  }
  
  if (r_sdp) {
    g_return_if_fail(answer_recv || offer_recv);
    if (channel && r_sdp) {
      int res = sip_media_channel_set_remote_info (channel, r_sdp);
      if (res < 0)
        {
          sip_media_channel_close (channel);
        }
    }
  }

  switch ((enum nua_callstate)ss_state) {
  case nua_callstate_received:
    /* In auto-alert mode, we don't need to call nua_respond(), see NUTAG_AUTOALERT() */
    nua_respond(nh, SIP_180_RINGING, TAG_END());
    break;

  case nua_callstate_early:
    /* nua_respond(nh, SIP_200_OK, TAG_END()); */
    
  case nua_callstate_completing:
    /* In auto-ack mode, we don't need to call nua_ack(), see NUTAG_AUTOACK() */
    break;

  case nua_callstate_ready:
    /* XXX -> note: only print if state has changed */
    g_message ("sofiasip: call to %s is active => '%s'", 
	       tp_handle_inspect (base->handles[TP_HANDLE_TYPE_CONTACT],
                 handle),
	       nua_callstate_name (ss_state));
    break;

  case nua_callstate_terminated:
    if (nh) {
      /* smcv-FIXME: need to work out which channel we're dealing with here */
      SIPMediaChannel *chan = sip_media_factory_get_only_channel (
          priv->media_factory);
      g_message ("sofiasip: call to %s is terminated", 
		 handle > 0 ? 
		 tp_handle_inspect (base->handles[TP_HANDLE_TYPE_CONTACT],
                   handle)
                 : "<unknown>");
      if (chan)
        sip_media_channel_close (chan);
      nua_handle_destroy (nh);
    }
    break;

  default:
    break;
  }
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
			      SIPConnection *self,
			      nua_handle_t *nh,
			      nua_hmagic_t *op,
			      sip_t const *sip,
			      tagi_t tags[])
{
  TpHandle handle = GPOINTER_TO_UINT (op);
  TpBaseConnection *base = (TpBaseConnection *)self;
  SIPConnectionPrivate *priv;

  DEBUG("enter: NUA at %p (conn %p), event #%d %s, %d %s", nua, self, event,
      nua_event_name (event), status, phrase);
  DEBUG ("Connection refcount is %d", ((GObject *)self)->ref_count);
  
  g_return_if_fail (self);
  priv = SIP_CONNECTION_GET_PRIVATE (self);
  g_return_if_fail (priv);

  switch (event) {
    
    /* incoming requests
     * ------------------------- */

  case nua_i_fork:
    /* self_i_fork(status, phrase, nua, self, nh, op, sip, tags); */
    break;
    
  case nua_i_invite:
    priv_i_invite (status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_i_state:
    priv_i_state (status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_i_bye:
    /* self_i_bye(nua, self, nh, op, sip, tags); */
    break;

  case nua_i_message:
    priv_i_message(status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_i_refer:
    /* self_i_refer(nua, self, nh, op, sip, tags); */
    break;

  case nua_i_notify:
    /* self_i_notify(nua, self, nh, op, sip, tags); */
    break;

  case nua_i_cancel:
    /* self_i_cancel(nua, self, nh, op, sip, tags); */
    break;

  case nua_i_error:
    /* self_i_error(nua, self, nh, op, status, phrase, tags); */
    break;

  case nua_i_active:
  case nua_i_ack:
  case nua_i_terminated:
    /* ignore these */
    break;

    /* responses to our requests 
     * ------------------------- */

  case nua_r_shutdown:    
    priv_r_shutdown (status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_r_register:
    priv_r_register (status, phrase, nua, self, nh, op, sip, tags);
    break;
    
  case nua_r_unregister:
    priv_r_unregister (status, phrase, nua, self, nh, op, sip, tags);
    break;
    
  case nua_r_invite:
    priv_r_invite(status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_r_bye:
    /* self_r_bye(status, phrase, nua, self, nh, op, sip, tags); */
    break;

  case nua_r_message:
    priv_r_message(status, phrase, nua, self, nh, op, sip, tags);
    break;

  case nua_r_refer:
    /* self_r_refer(status, phrase, nua, self, nh, op, sip, tags); */
    break;

  case nua_r_subscribe:
    /* self_r_subscribe(status, phrase, nua, self, nh, op, sip, tags); */
    break;

  case nua_r_unsubscribe:
    /* self_r_unsubscribe(status, phrase, nua, self, nh, op, sip, tags); */
    break;

  case nua_r_publish:
    /* self_r_publish(status, phrase, nua, self, nh, op, sip, tags); */
    break;
    
  case nua_r_notify:
    /* self_r_notify(status, phrase, nua, self, nh, op, sip, tags); */
    break;

  case nua_r_get_params:
    priv_r_get_params(status, phrase, nua, self, nh, op, sip, tags);
    break;
     
  default:
    if (status > 100)
      g_message ("sip-connection: unknown event '%s' (%d): %03d %s handle=%u", 
		 nua_event_name(event), event, status, phrase, handle);
    else
      g_message ("sip-connection: unknown event %d handle=%u", event, handle);

    if (handle > 0 &&
	!tp_handle_is_valid (base->handles[TP_HANDLE_TYPE_CONTACT],
          handle, NULL)) {
      /* note: unknown handle, not associated to any existing 
       *       call, message, registration, etc, so it can
       *       be safely destroyed */
      g_message ("NOTE: destroying handle %p (%u).", nh, handle);
      nua_handle_destroy(nh);
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
