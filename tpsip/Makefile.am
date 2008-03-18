#
# Makefile.am for telepathy-sofiasip/tpsip
#
# Copyright (C) 2008 Nokia Corporation
# Contact: Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
# Licensed under LGPL. See file COPYING.
#

noinst_LTLIBRARIES = libtpsip.la

AM_CPPFLAGS = @GLIB_CFLAGS@ @TELEPATHY_GLIB_CFLAGS@ @SOFIA_SIP_UA_CFLAGS@

tpsip_includedir = $(includedir)/telepathy-sofiasip-0.6/tpsip

tpsip_include_HEADERS = \
	event-target.h \
	sofia-decls.h

BUILT_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

libtpsip_la_SOURCES = \
	event-target.c

nodist_libtpsip_la_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

CLEANFILES = $(BUILT_SOURCES)

signals-marshal.c: ${srcdir}/signals-marshal.list
	glib-genmarshal --body --prefix=_tpsip_marshal $< >$@

signals-marshal.h: ${srcdir}/signals-marshal.list
	glib-genmarshal --header --prefix=_tpsip_marshal $< >$@
