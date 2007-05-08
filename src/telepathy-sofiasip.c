/*
 * sip-connection.c - Source for SIPConnection
 * Copyright (C) 2005 Collabora Ltd.
 * Copyright (C) 2005,2006 Nokia Corporation
 *   @author Kai Vehmanen <first.surname@nokia.com>
 *   @author Martti Mela <first.surname@nokia.com>
 *   @author Mikhail Zabaluev <first.surname@nokia.com>
 *
 * Based on telepathy-gabble implementation (gabble-connection).
 *   @author See gabble.c
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

#include "debug.h"

#include "sip-connection-manager.h"
#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

static TpBaseConnectionManager *
construct_cm (void)
{
  return (TpBaseConnectionManager *)g_object_new (
      SIP_TYPE_CONNECTION_MANAGER, NULL);
}

int
main (int argc, char** argv)
{
#ifdef ENABLE_DEBUG
  sip_debug_set_flags_from_env ();

  if (g_getenv ("SOFIASIP_PERSIST"))
    {
      sip_debug_set_flags (0xffff);
      tp_debug_set_all_flags ();
    }
#endif

  return tp_run_connection_manager ("telepathy-sofiasip", VERSION,
      construct_cm, argc, argv);
}
