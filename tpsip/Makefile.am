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

AM_CFLAGS = $(ERROR_CFLAGS) $(COVERAGE_CFLAGS)

tpsip_includedir = $(includedir)/telepathy-sofiasip-0.7/tpsip

tpsip_include_HEADERS = \
	base-connection.h \
	connection-aliasing.h
	sofia-decls.h \
	codec-param-formats.h \
	event-target.h \
	handles.h \
	debug.h \
	media-manager.h \
	text-manager.h \
	util.h

BUILT_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

libtpsip_la_SOURCES = \
	base-connection.c \
	base-connection-sofia.c \
	connection-aliasing.c \
	codec-param-formats.c \
	event-target.c \
	handles.c \
	debug.c \
	media-channel.h \
	media-channel.c \
	media-manager.c \
	media-session.h \
	media-session.c \
	media-stream.h \
	media-stream.c \
	text-channel.h \
	text-channel.c \
	text-manager.c \
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
