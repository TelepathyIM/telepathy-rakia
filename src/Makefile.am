#
# Makefile.am for telepathy-rakia/src
#
# Copyright (C) 2006-2009 Nokia Corporation
# Contact: Mikhail Zabaluev <mikhail.zabaluev@nokia.com>
# Licensed under LGPL. See file COPYING.
#

# ----------------------------------------------------------------------
# Automake options

managerdir = $(datadir)/telepathy-1/managers

# ----------------------------------------------------------------------
# Headers and libraries

AM_CPPFLAGS = $(DBUS_CFLAGS) $(GLIB_CFLAGS) $(SOFIA_SIP_UA_CFLAGS) \
	$(TELEPATHY_GLIB_CFLAGS) $(IPHB_CFLAGS) \
	-I$(top_builddir) -I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"rakia\"
AM_CFLAGS = $(ERROR_CFLAGS) $(COVERAGE_CFLAGS)
AM_LDFLAGS = $(ERROR_LDFLAGS)
ALL_LIBS = $(DBUS_LIBS) $(GLIB_LIBS) $(SOFIA_SIP_UA_LIBS) \
	$(TELEPATHY_GLIB_LIBS) $(IPHB_LIBS)

# ----------------------------------------------------------------------
# Build targets

libexec_PROGRAMS = telepathy-rakia-1
manager_DATA = rakia.manager
noinst_PROGRAMS = write-mgr-file
noinst_LTLIBRARIES = librakia-convenience.la

# ----------------------------------------------------------------------
# Tests

# ----------------------------------------------------------------------
# Rules for building the targets

# rules for makeing the glib enum objects
%-enumtypes.h: %.h
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define RAKIA_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@ || rm -f $@

%-enumtypes.c: %.h
	glib-mkenums \
	--fhead "#include <$*-enumtypes.h>\n#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@ || rm -f $@

rakia.manager: write-mgr-file
	if ./write-mgr-file > $@.tmp; then \
		mv -f $@.tmp $@;\
	else \
		rm -f $@.tmp; \
	fi

BUILT_SOURCES = \
		sip-connection-enumtypes.h \
		sip-connection-enumtypes.c

librakia_convenience_la_SOURCES = \
    sip-connection.h \
    sip-connection.c \
    sip-connection-manager.h \
    sip-connection-manager.c \
    protocol.h \
    protocol.c \
    sip-connection-helpers.h \
    sip-connection-helpers.c \
    sip-connection-private.h

nodist_librakia_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

librakia_convenience_la_LIBADD = \
    $(top_builddir)/rakia/librakia.la \
    $(NULL)

telepathy_rakia_1_SOURCES = \
    telepathy-rakia.c 

telepathy_rakia_1_LDADD = librakia-convenience.la $(ALL_LIBS)

write_mgr_file_SOURCES = \
    write-mgr-file.c 

write_mgr_file_LDADD = librakia-convenience.la $(ALL_LIBS)

# ----------------------------------------------------------------------
# Install and distribution rules

# Correctly clean the generated headers, but keep the xml description
CLEANFILES = $(BUILT_SOURCES) $(manager_DATA)
CLEANFILES += *.gcda *.gcno

