#
# Makefile.am for telepathy-sofiasip/src
#
# Copyright (C) 2006 Nokia Corporation
# Contact: Kai Vehmanen <first.surname@nokia.com>
# Licensed under LGPL. See file COPYING.
#

# ----------------------------------------------------------------------
# Automake options

managerdir = $(datadir)/telepathy/managers

# ----------------------------------------------------------------------
# Headers and libraries

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @SOFIA_SIP_UA_CFLAGS@ \
	    @TELEPATHY_GLIB_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @SOFIA_SIP_UA_LIBS@ @TELEPATHY_GLIB_LIBS@

# ----------------------------------------------------------------------
# Build targets

bin_PROGRAMS = telepathy-sofiasip
manager_DATA = sofiasip.manager
noinst_PROGRAMS = write-mgr-file
noinst_LTLIBRARIES = libtpsip-convenience.la

# ----------------------------------------------------------------------
# Tests

# ----------------------------------------------------------------------
# Rules for building the targets

signals-marshal.c: ${srcdir}/signals-marshal.list
	glib-genmarshal --body --prefix=_tpsip_marshal $< >$@

signals-marshal.h: ${srcdir}/signals-marshal.list
	glib-genmarshal --header --prefix=_tpsip_marshal $< >$@

# rules for makeing the glib enum objects
%-enumtypes.h: %.h
	glib-mkenums \
	--fhead "#ifndef __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n#define __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define $(shell echo $* | tr [:lower:]- [:upper:]_ | sed 's/_.*//')_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __$(shell echo $* | tr [:lower:]- [:upper:]_)_ENUM_TYPES_H__ */" \
	$< > $@

%-enumtypes.c: %.h
	glib-mkenums \
	--fhead "#include <$*.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$< > $@

sofiasip.manager: write-mgr-file
	if ./write-mgr-file > $@.tmp; then \
		mv -f $@.tmp $@;\
	else \
		rm -f $@.tmp; \
	fi

BUILT_SOURCES = \
		signals-marshal.h \
		signals-marshal.c \
		sip-connection-enumtypes.h \
		sip-connection-enumtypes.c

libtpsip_convenience_la_SOURCES = \
    sip-text-channel.h \
    sip-text-channel.c \
    sip-media-channel.h \
    sip-media-channel.c \
    sip-media-session.h \
    sip-media-session.c \
    sip-media-stream.h \
    sip-media-stream.c \
    sip-connection.h \
    sip-connection.c \
    sip-connection-manager.h \
    sip-connection-manager.c \
    debug.h \
    debug.c \
    media-factory.h \
    media-factory.c \
    text-factory.h \
    text-factory.c \
    sip-connection-helpers.h \
    sip-connection-helpers.c \
    sip-connection-private.h \
    sip-connection-sofia.h \
    sip-connection-sofia.c \
    telepathy-helpers.h \
    telepathy-helpers.c

nodist_libtpsip_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

libtpsip_convenience_la_LIBADD = \
    @TELEPATHY_GLIB_LIBS@

telepathy_sofiasip_SOURCES = \
    telepathy-sofiasip.c 

telepathy_sofiasip_LDADD = libtpsip-convenience.la

write_mgr_file_SOURCES = \
    write-mgr-file.c 

write_mgr_file_LDADD = libtpsip-convenience.la

# ----------------------------------------------------------------------
# Install and distribution rules

EXTRA_DIST = signals-marshal.list

# Correctly clean the generated headers, but keep the xml description
CLEANFILES = $(BUILT_SOURCES) $(manager_DATA)
