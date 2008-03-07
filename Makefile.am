#
# Makefile.am for telepathy-sofiasip/src
#
# Copyright (C) 2006,2008 Nokia Corporation
# Contact: Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
# Licensed under LGPL. See file COPYING.
#

# ----------------------------------------------------------------------
# Automake options

ACLOCAL_AMFLAGS = -I m4

# ----------------------------------------------------------------------
# Build targets

SUBDIRS = \
	tools \
	tpsip-extensions \
	data \
	docs \
	m4 \
	src \
	tests

dist-hook:
	chmod ug+w ${distdir}/ChangeLog
	if test -d _darcs; then darcs changes >${distdir}/ChangeLog; fi
