#
# Makefile.am for telepathy-rakia/src
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
	rakia \
	data \
	docs \
	m4 \
	src \
	tests

include tools/lcov.am
LCOV_CHECK_ARGS = CHECK_TWISTED_SLEEP=6

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

clean-local: lcov-clean

include tools/telepathy.am
