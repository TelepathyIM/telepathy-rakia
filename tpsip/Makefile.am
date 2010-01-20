#
# Makefile.am for telepathy-sofiasip/tpsip
#
# Copyright (C) 2008 Nokia Corporation
# Contact: Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
# Licensed under LGPL. See file COPYING.
#

noinst_LTLIBRARIES = libtpsip.la

AM_CPPFLAGS = @GLIB_CFLAGS@ @TELEPATHY_GLIB_CFLAGS@ @SOFIA_SIP_UA_CFLAGS@ \
	      -I$(top_builddir) -I$(top_srcdir)

tpsip_includedir = $(includedir)/telepathy-sofiasip-0.6/tpsip

tpsip_include_HEADERS = \
	sofia-decls.h \
	codec-param-formats.h \
	event-target.h \
	util.h

BUILT_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

libtpsip_la_SOURCES = \
	codec-param-formats.c \
	event-target.c \
	util.c

nodist_libtpsip_la_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

EXTRA_DIST = signals-marshal.list

CLEANFILES = $(BUILT_SOURCES)

CLEANFILES += *.gcno

signals-marshal.c: ${srcdir}/signals-marshal.list
	glib-genmarshal --body --prefix=_tpsip_marshal $< >$@ || rm -f $@

signals-marshal.h: ${srcdir}/signals-marshal.list
	glib-genmarshal --header --prefix=_tpsip_marshal $< >$@ || rm -f $@
