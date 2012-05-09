#
# Makefile.am for telepathy-rakia/rakia
#
# Copyright (C) 2008 Nokia Corporation
# Contact: Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
# Licensed under LGPL. See file COPYING.
#

noinst_LTLIBRARIES = librakia.la

AM_CPPFLAGS = @GLIB_CFLAGS@ @TELEPATHY_GLIB_CFLAGS@ @SOFIA_SIP_UA_CFLAGS@ \
	      -I$(top_builddir) -I$(top_srcdir)

AM_CFLAGS = $(ERROR_CFLAGS) $(COVERAGE_CFLAGS)
AM_LDFLAGS = $(ERROR_LDFLAGS)

rakia_includedir = $(includedir)/telepathy-rakia-0.7/rakia

rakia_include_HEADERS = \
	base-connection.h \
	connection-aliasing.h \
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

librakia_la_SOURCES = \
	base-connection.c \
	base-connection-sofia.c \
	connection-aliasing.c \	
	call-channel.h \
	call-channel.c \
	call-content.h \
	call-content.c \
	call-stream.h \
	call-stream.c \
	codec-param-formats.c \
	event-target.c \
	handles.c \
	debug.c \
	media-manager.c \
	sip-media.c \
	sip-media.h \
	sip-session.c \
	sip-session.h \
	text-channel.h \
	text-channel.c \
	text-manager.c \
	util.c

nodist_librakia_la_SOURCES = \
	signals-marshal.h \
	signals-marshal.c

EXTRA_DIST = signals-marshal.list

CLEANFILES = $(BUILT_SOURCES)

CLEANFILES += *.gcno

signals-marshal.c: ${srcdir}/signals-marshal.list
	glib-genmarshal --body --prefix=_rakia_marshal $< >$@ || rm -f $@

signals-marshal.h: ${srcdir}/signals-marshal.list
	glib-genmarshal --header --prefix=_rakia_marshal $< >$@ || rm -f $@
