#include "extensions.h"

#include <telepathy-glib/channel.h>
#include <telepathy-glib/proxy-subclass.h>

#include "_gen/signals-marshal.h"

/* include auto-generated stubs for client-specific code */
#include "_gen/cli-channel-body.h"
#include "_gen/register-dbus-glib-marshallers-body.h"

void
sip_cli_init (void)
{
  _sip_ext_register_dbus_glib_marshallers ();

  tp_proxy_or_subclass_hook_on_interface_add (TP_TYPE_CHANNEL,
      sip_cli_channel_add_signals);
}
