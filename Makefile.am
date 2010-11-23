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
	tpsip \
	tpsip-extensions \
	data \
	docs \
	m4 \
	src \
	tests

dist-hook:
	chmod u+w ${distdir}/ChangeLog
	if test -d ${top_srcdir}/.git; then \
		git --work-tree=${top_srcdir} log --stat > ${distdir}/ChangeLog || \
		git --work-tree=${top_srcdir} log > ${distdir}/ChangeLog; \
	fi

include tools/lcov.am
LCOV_CHECK_ARGS = CHECK_TWISTED_SLEEP=6

clean-local: lcov-clean

include tools/telepathy.am
