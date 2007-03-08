if HAVE_LIBTELEPATHY
tp_tests = tp_caller
endif

noinst_PROGRAMS = $(tp_tests)

tp_caller_SOURCES = \
    tp_caller.c
tp_caller_LDADD = \
    $(LIBTELEPATHY_LIBS)
tp_caller_CFLAGS = \
    $(LIBTELEPATHY_CFLAGS)

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @SOFIA_SIP_UA_CFLAGS@ \
	    -I $(top_srcdir)/src -I $(top_builddir)/src \
	    @TELEPATHY_GLIB_CFLAGS@
AM_LDFLAGS = @DBUS_LIBS@ @GLIB_LIBS@ @SOFIA_SIP_UA_LIBS@
