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
	    @TELEPATHY_GLIB_CFLAGS@ @COVERAGE_CFLAGS@ \
	    -I$(top_builddir) -I$(top_srcdir)
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @SOFIA_SIP_UA_LIBS@ @TELEPATHY_GLIB_LIBS@

# ----------------------------------------------------------------------
# Build targets

libexec_PROGRAMS = telepathy-sofiasip
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
		tpsip-connection-enumtypes.h \
		tpsip-connection-enumtypes.c

libtpsip_convenience_la_SOURCES = \
    tpsip-connection.h \
    tpsip-connection.c \
    tpsip-connection-helpers.h \
    tpsip-connection-helpers.c \
    tpsip-connection-manager.h \
    tpsip-connection-manager.c \
    tpsip-connection-private.h \
    tpsip-connection-sofia.h \
    tpsip-connection-sofia.c \
    tpsip-media-channel.h \
    tpsip-media-channel.c \
    tpsip-media-factory.h \
    tpsip-media-factory.c \
    tpsip-media-session.h \
    tpsip-media-session.c \
    tpsip-media-stream.h \
    tpsip-media-stream.c \
    tpsip-text-channel.h \
    tpsip-text-channel.c \
    tpsip-text-factory.h \
    tpsip-text-factory.c \
    tpsip-sofia-decls.h \
    tpsip-debug.h \
    tpsip-debug.c

nodist_libtpsip_convenience_la_SOURCES = \
    $(BUILT_SOURCES)

libtpsip_convenience_la_LIBADD = \
    $(top_builddir)/extensions/libtpsip-extensions.la

telepathy_sofiasip_SOURCES = \
    telepathy-sofiasip.c 

telepathy_sofiasip_LDADD = libtpsip-convenience.la $(ALL_LIBS)

write_mgr_file_SOURCES = \
    write-mgr-file.c 

write_mgr_file_LDADD = libtpsip-convenience.la $(ALL_LIBS)

# ----------------------------------------------------------------------
# Install and distribution rules

EXTRA_DIST = signals-marshal.list

# Correctly clean the generated headers, but keep the xml description
CLEANFILES = $(BUILT_SOURCES) $(manager_DATA)
