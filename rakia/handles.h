/*
 * rakia/handle.h - Telepathy SIP handle management
 * Copyright (C) 2011 Nokia Corporation
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

#ifndef RAKIA_HANDLE_H
#define RAKIA_HANDLE_H

#include <telepathy-glib/base-connection.h>
#include <sofia-sip/sip.h>

G_BEGIN_DECLS

TpHandle rakia_handle_ensure (TpBaseConnection *, url_t const *, char const *);
TpHandle rakia_handle_by_requestor (TpBaseConnection *, sip_t const *sip);
char const *rakia_handle_inspect (TpBaseConnection *, TpHandle handle);

gchar * rakia_handle_normalize (TpHandleRepoIface *repo,
    const gchar *sipuri,
    gpointer context,
    GError **error);

gchar *rakia_normalize_contact (const gchar *sipuri,
    const url_t *base_url,
    const gchar *transport,
    GError **error);

/* no longer does anything */
G_DEPRECATED
void rakia_handle_unref (TpBaseConnection *, TpHandle handle);

G_END_DECLS

#endif /* !RAKIA_HANDLE_H */
