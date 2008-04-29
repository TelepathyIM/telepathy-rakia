if WANT_TWISTED_TESTS
CHECKTWISTED = twisted
endif
SUBDIRS = $(CHECKTWISTED)

noinst_PROGRAMS = \
	tp_caller

AM_CFLAGS = $(ERROR_CFLAGS) @DBUS_CFLAGS@ @GLIB_CFLAGS@ @LOUDMOUTH_CFLAGS@ \
    @COVERAGE_CFLAGS@ @TP_GLIB_CFLAGS@ \
    -I $(top_srcdir)/src -I $(top_builddir)/src
ALL_LIBS = @DBUS_LIBS@ @GLIB_LIBS@ @LOUDMOUTH_LIBS@ @TP_GLIB_LIBS@

check-valgrind:
	G_SLICE=always-malloc \
	G_DEBUG=gc-friendly \
	$(MAKE) \
		TESTS_ENVIRONMENT="\
		libtool --mode=execute valgrind \
			--leak-check=full \
			--show-reachable=no \
			--error-exitcode=1" \
		check

check_c_sources = \
	tp_caller.c

include $(top_srcdir)/tools/check-coding-style.mk
