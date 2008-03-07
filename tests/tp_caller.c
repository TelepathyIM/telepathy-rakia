/*
 * tp_test.c - telepathy-sofiasip test utility (modified from
 * libtelepathy's proto.c)
 *
 * Copyright (C) 2005-2006 Nokia Corporation.
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

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <dbus/dbus-glib.h>
#include <glib.h>
#include <libtelepathy/tp-conn.h>
#include <libtelepathy/tp-connmgr.h>
#include <libtelepathy/tp-chan.h>
#include <libtelepathy/tp-chan-gen.h>
#include <libtelepathy/tp-ch-gen.h>
#include <libtelepathy/tp-chan-iface-group-gen.h>
#include <libtelepathy/tp-chan-type-text-gen.h>
#include <libtelepathy/tp-chan-type-streamed-media-gen.h>
#include <libtelepathy/tp-props-iface.h>
#include <libtelepathy/tp-constants.h>
#include <libtelepathy/tp-interfaces.h>

/*
 * Test connection manager and account
 */
#define CONNMGR_NAME "sofiasip"
#define CONNMGR_BUS "org.freedesktop.Telepathy.ConnectionManager.sofiasip"
#define CONNMGR_PATH "/org/freedesktop/Telepathy/ConnectionManager/sofiasip"
#define PROTOCOL "sip"

enum {
  STATE_START = 0,
  STATE_CHAN,
  STATE_STREAMS,
  STATE_RUNNING,
  STATE_COMPLETED,
};

static gboolean global_connected = FALSE;
static guint global_remote_handle = 0;
static GMainLoop *global_mainloop = NULL;
static DBusGProxy *global_streamengine = NULL;
static DBusGConnection *global_dbus_connection = NULL;
static gint  global_exit_request = 0;
static char* global_conn_path = NULL;
static char* global_chan_path = NULL;
static guint global_chan_handle = 0;
static guint global_call_state = STATE_START;

static void newconnection_handler(DBusGProxy *proxy, const char *s1,
				  const char *o, const char *s2,
				  gpointer user_data);

static void new_channel_handler(DBusGProxy *proxy, const char *object_path,
				const char *channel_type, guint handle_type,
				guint handle, gboolean suppress_handle,
				gpointer user_data);

static gboolean status_changed_cb(DBusGProxy *proxy,
				  guint status, guint reason,
				  gpointer user_data);

static void request_handles_reply_cb(DBusGProxy *proxy, GArray *OUT_arg2, GError *error, gpointer userdata);

static void request_chan_cb(DBusGProxy *proxy,
			    gchar *channel_path,
			    GError *error,
			    gpointer data);

static void request_streams_cb(DBusGProxy *proxy,
			       GPtrArray *streams,
			       GError *error,
			       gpointer data);

static void tpcaller_signal_handler(int signo);

static void check_conn_properties(TpConn *conn);

static TpConn *action_login(const char* sip_address, const char* sip_password, const char* sip_proxy)
{
  DBusGConnection *connection;
  TpConn *conn;
  DBusGProxy *streamengine = NULL;
  TpConnMgr *connmgr;
  guint status = 1;
  GError *error = NULL;
  GValue val_acc, val_auth_usr, val_pwd, val_proxy;
  GHashTable *connection_parameters = g_hash_table_new(g_str_hash, NULL);

  connection = dbus_g_bus_get(DBUS_BUS_SESSION, &error);
  if (connection == NULL) {
    g_printerr("Failed to open connection to bus: %s\n",
	       error->message);
    g_error_free(error);
    exit(1);
  }

  printf("connected to DBus with connection %p\n", connection);
  global_dbus_connection = connection;

  /* Create a connection manager object */
  g_print("Attempting to create a connection manager object.\n");
  connmgr = tp_connmgr_new(connection, CONNMGR_BUS, CONNMGR_PATH,
			   TP_IFACE_CONN_MGR_INTERFACE);

  if (connmgr == NULL) {
    g_error("Failed to create a connection manager, skipping manager startup.");
  }
  else {
    g_print("Creating a connection manager succeeded.\n");
  }

  g_print("Attempting to register a signal handler for NewConnection signal.\n");
  dbus_g_proxy_connect_signal(DBUS_G_PROXY(connmgr), "NewConnection",
			      G_CALLBACK(newconnection_handler),
			      NULL, NULL);
	
  /* Setting "g_type" is a hack since GValue is broken */
  val_acc.g_type = 0;
  val_auth_usr.g_type = 0;
  val_proxy.g_type = 0;
  val_pwd.g_type = 0;

  g_value_init(&val_acc, G_TYPE_STRING);
  g_value_init(&val_auth_usr, G_TYPE_STRING);
  g_value_init(&val_proxy, G_TYPE_STRING);
  g_value_init(&val_pwd, G_TYPE_STRING);

  /* Connection parameters are dummy: fill in suitable replacements */

  g_value_set_string(&val_acc, sip_address);
  g_value_set_string(&val_pwd, sip_password);
	
  g_hash_table_insert(connection_parameters, "account",
		      (gpointer) &val_acc);
  g_hash_table_insert(connection_parameters, "password",
		      (gpointer) &val_pwd);
  if (sip_proxy != NULL) {
    g_value_set_string(&val_proxy, sip_proxy);
    g_hash_table_insert(connection_parameters, "proxy",
			(gpointer) &val_proxy);  
  }

  /* Create a new actual connection with the connection manager */

  g_print("Attempting to create a connection object.\n");

  conn = tp_connmgr_new_connection(connmgr,
				   connection_parameters,
				   PROTOCOL);
  g_assert(conn != NULL);

  /* step: connection creation succesful */
  dbus_g_proxy_connect_signal(DBUS_G_PROXY(conn), "NewChannel",
			      G_CALLBACK(new_channel_handler),
			      NULL, NULL);
  g_print("Creation of a connection object succeeded.\n");

  streamengine = dbus_g_proxy_new_for_name (connection,
					    "org.freedesktop.Telepathy.StreamEngine",
					    "/org/freedesktop/Telepathy/StreamEngine",
					    "org.freedesktop.Telepathy.ChannelHandler");

  g_assert(streamengine != NULL);
  global_streamengine = streamengine;

  /* Check if connection is active; if not, add a callback
   * for StatusChange signal */

  if (!tp_conn_get_status(DBUS_G_PROXY(conn), &status, &error) || status != 0) {
    g_print("GetStatus did not work synchronously, trying async version.\n");

    dbus_g_proxy_connect_signal(DBUS_G_PROXY(conn), "StatusChanged",
				G_CALLBACK(status_changed_cb),
				NULL, NULL);
    if (error) 
      g_error_free(error);

    while (global_connected != TRUE && global_exit_request == 0)  {
      g_main_context_iteration(NULL, TRUE);
    }
  } else {
    check_conn_properties(conn);
  }

  g_hash_table_destroy(connection_parameters);

  return conn;
}

static int action_make_call(TpConn *conn, const char* to_uri)
{
  DBusGProxy *streamengine = global_streamengine;
  GError *error = NULL;
  TpChan *channel;
  DBusGProxy *stream = NULL;
  GArray *types = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
  guint mediatype = TP_MEDIA_STREAM_TYPE_AUDIO;
  const char *urilist[2] = { NULL, NULL };
  int result = 0;
  guint old_state = STATE_START;

  urilist[0] = to_uri;

  /* state machine for setting up a call */

  while (global_call_state != STATE_COMPLETED) {

    switch (global_call_state) 
      {
      case STATE_START:
	g_print("Requesting handle for SIP URI %s.\n", to_uri);
	tp_conn_request_handles_async(DBUS_G_PROXY(conn), TP_CONN_HANDLE_TYPE_CONTACT, (const char**)urilist, request_handles_reply_cb, NULL);

	break;

      case STATE_CHAN:

	/* request for a channel for outbound call  */
	g_print("Attempting to make an outbound call to %s (%d).\n", to_uri, global_remote_handle);
	tp_conn_request_channel_async(DBUS_G_PROXY(conn),
				      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
				      TP_CONN_HANDLE_TYPE_CONTACT, global_remote_handle, TRUE,
				      request_chan_cb, NULL);
	
	break;

      case STATE_STREAMS:
	g_debug("Calling HandleChannel on %p, connection='%s', chan='%s'.\n",
		streamengine, global_conn_path, global_chan_path);

	error = NULL;
	
	tp_ch_handle_channel (streamengine, 
			      CONNMGR_BUS,
			      global_conn_path, 
			      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA, 
			      global_chan_path, 
			      TP_CONN_HANDLE_TYPE_CONTACT, 
			      global_remote_handle, 
			      &error);

	if (error) {
	  g_print ("ERROR: %s", error->message);
	  g_error_free (error);
	  g_object_unref (streamengine);
	  result = -1;

	}
	else 
	  g_print ("Succesful HandleChannel with streamengine.\n");

	channel = tp_chan_new (global_dbus_connection,
			       CONNMGR_BUS,
			       global_chan_path,
			       TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
			       TP_CONN_HANDLE_TYPE_CONTACT,
			       global_remote_handle);
 
	g_assert (channel);

	stream = tp_chan_get_interface (channel,
					TELEPATHY_CHAN_IFACE_STREAMED_QUARK);

	if (stream) {
	  g_array_append_val (types, mediatype);
    
	  g_debug("%s: calling RequestStream with types->len=%d", G_STRFUNC, types->len);
    
	  tp_chan_type_streamed_media_request_streams_async (stream,
							     global_chan_handle /*global_remote_handle*/,
							     types,
							     request_streams_cb,
							     NULL);
	}
	break;
	
      case STATE_RUNNING: 
	/* stream ready, call setup completed */
	global_call_state = STATE_COMPLETED;
	break;
      }

    /* run the mainloop */
    while (global_call_state == old_state && 
	   global_exit_request == 0) {
      g_main_context_iteration(NULL, TRUE);
    }

    old_state = global_call_state;

    if (global_exit_request)
      global_call_state = STATE_COMPLETED;

  }

  return result;
}

int main(int argc, char **argv)
{
  TpConn *conn;
  GError *error = NULL;
  GMainLoop *mainloop;
  char* sip_proxy = getenv("SIP_PROXY");

#ifndef _WIN32
  /* see: http://www.opengroup.org/onlinepubs/007908799/xsh/sigaction.html */
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_handler = tpcaller_signal_handler;
  sigaction(SIGINT, &sigact, NULL); /* ctrl-c */
  sigaction(SIGABRT, &sigact, NULL);
  sigaction(SIGTERM, &sigact, NULL);
#endif

  if (argc < 3) {
    g_message("Usage: tp_caller <sip-aor> <password> [<sip-address-to-call> <sip-outbound-proxy>]");
    exit(1);
  }
	
  g_type_init();
  global_mainloop = mainloop = g_main_loop_new (NULL, FALSE);

  if (sip_proxy == NULL)
    sip_proxy = argc > 4 ? argv[4] : NULL;

  conn = action_login(argv[1], argv[2], sip_proxy);

  if (conn) {
    if (argc > 3 &&
	strncmp(argv[3], "sip:", 4) == 0) {
      /* only call if 3rd param is a valid SIP URI */
      action_make_call(conn, argv[3]);
    };

    g_print("Entering tp_caller mainloop.\n");
    g_main_loop_run (mainloop);

    g_print("Disconnecting from network.\n");

    tp_conn_disconnect (DBUS_G_PROXY(conn), &error);
    if (error)
      g_error_free (error);

    g_object_unref (conn);
    dbus_g_connection_unref (global_dbus_connection);
  }
  else
    g_warning("Unable to login with the requested account.");
    
  g_print("Closing connection to SIP network.\n");

  return 0;
}

static void newconnection_handler(DBusGProxy *proxy, const char *bus,
				  const char *path, const char *proto,
				  gpointer user_data)

{
  g_print("NewConnection callback:\n\tbus=%s\n\tpath=%s\n\tproto=%s\n", bus, path, proto);
  global_conn_path = g_strdup(path);
}

static void handle_incoming_call (DBusGConnection *proxy, const char *chan_path, guint handle)
{

  GError *error = NULL;
  GArray *array;
  TpChan *channel;
  DBusGProxy *chgroup = NULL;
  
  channel = tp_chan_new (proxy,
			 CONNMGR_BUS,
			 chan_path,
			 TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
			 TP_CONN_HANDLE_TYPE_CONTACT,
			 handle);
  g_assert (channel);
      
  chgroup = tp_chan_get_interface (channel,
				   TELEPATHY_CHAN_IFACE_GROUP_QUARK);
  g_assert (chgroup);

  g_print("\tInbound call, passing the media channel to stream engine.\n");

  /* step 1: pass the channel to the stream engine */
      
  tp_ch_handle_channel (global_streamengine, 
			CONNMGR_BUS,
			global_conn_path, 
			TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA, 
			chan_path, 
			TP_CONN_HANDLE_TYPE_CONTACT, 
			handle, 
			&error);

  if (error) {
    g_print ("ERROR: %s", error->message);
    g_error_free (error);
  }

  /* step 2: inform connection manager we accept the session */

  tp_chan_iface_group_get_self_handle (chgroup, &global_chan_handle, &error);
  if (error) {
    g_warning ("cannot get self handle: %s", error->message);
    g_error_free (error), error = NULL;
  }

  g_print("\tAccepting the call (self handle %d).\n", global_chan_handle);

  array = g_array_new (FALSE, FALSE, sizeof (guint));
  g_array_append_val (array, global_chan_handle);

  tp_chan_iface_group_add_members (chgroup, array, "", &error);
  if (error) {
    g_warning ("cannot add member %u to media channel: %s", handle, error->message);
    g_error_free (error), error = NULL;
  }
  
  g_array_free(array, TRUE);
}

static void new_channel_handler(DBusGProxy *proxy, const char *object_path,
				const char *channel_type, guint handle_type,
				guint handle, gboolean suppress_handler,
				gpointer user_data)
{
  g_print("NewChannel callback\n\tpath=%s\n\ttype=%s, handle=%u\n",
	  object_path, channel_type, handle);

  if (strcmp(channel_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA) == 0) {
    global_chan_path = g_strdup(object_path);
    /* note: handle == 0 for inbound calls */
    global_chan_handle = handle;
    if (suppress_handler == 0) {
      /* an inbound session, let's answer it */
      handle_incoming_call (global_dbus_connection, global_chan_path, handle);
    }
  }
}

static void request_chan_cb(DBusGProxy *proxy,
			    gchar *channel_path,
			    GError *error,
			    gpointer data)
{

  g_print("RequestChan callback:\n\tchanpath=%s\n", channel_path);

  if (error != NULL) {
    g_warning("%s: error in callback - %s\n", G_STRFUNC, error->message);
    g_error_free(error);
    global_exit_request = 1;
    return;
  }
  
  if (channel_path == NULL) {
    global_exit_request = 1;
    return;
  }

  global_call_state = STATE_STREAMS;
}

static void request_streams_cb(DBusGProxy *proxy,
			       GPtrArray *streams,
			       GError *error,
			       gpointer data)
{
  g_print("RequestStreams callback with %d streams\n", streams ? streams->len : 0);
  if (error != NULL) {
    g_warning("%s: error in callback - %s\n", G_STRFUNC, error->message);
    g_error_free(error);
    return;
  }

  global_call_state = STATE_RUNNING;
}

static gboolean status_changed_cb(DBusGProxy *proxy,
				  guint status, guint reason,
				  gpointer user_data)
{
  g_print("StatusChanged signal\n");
  if (status == 0) {
    g_print("\tConnected!\n");
    global_connected = TRUE;
    check_conn_properties(TELEPATHY_CONN(proxy));
  }
  else {
    global_connected = FALSE;
  }
  return TRUE;
}

static void request_handles_reply_cb(DBusGProxy *proxy, GArray *handles, GError *error, gpointer userdata)
{
  guint i;

  g_print("RequestHandles callback\n");

  if (error) {
    g_warning("%s: error in callback - %s\n", G_STRFUNC, error->message);
    g_error_free(error);
    global_exit_request = 1;
    return;
  }
    
  for(i = 0; handles && i < handles->len; i++) {
    guint ret_handle = g_array_index(handles, guint, i);

    if (i == 0) {
      global_remote_handle = ret_handle;
    }

    g_print("\tRequested handle received: %u (item %u)\n", ret_handle, i);
  }

  global_call_state = STATE_CHAN;
}

static void tpcaller_signal_handler(int signo)
{
  global_exit_request = 1;

  if (global_mainloop)
    g_main_loop_quit(global_mainloop);
}

static void check_conn_properties(TpConn *conn)
{
  TpPropsIface *conn_props;

  conn_props = TELEPATHY_PROPS_IFACE (tp_conn_get_interface (
        conn, TELEPATHY_PROPS_IFACE_QUARK));
  if (conn_props == NULL) {
    g_warning ("The connection object does not support " TP_IFACE_PROPERTIES);  
  }
}
