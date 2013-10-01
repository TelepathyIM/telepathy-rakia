if WANT_TWISTED_TESTS
CHECKTWISTED = twisted
endif
SUBDIRS = $(CHECKTWISTED)

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

include $(top_srcdir)/tools/check-coding-style.mk
