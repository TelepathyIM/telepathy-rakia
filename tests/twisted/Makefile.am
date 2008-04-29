TWISTED_TESTS = \
	test-register.py \
	test-register-fail.py \
	test-handle-normalisation.py \
	test-message.py

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted

check-local: check-coding-style
	$(MAKE) -C tools
	sh $(srcdir)/tools/with-session-bus.sh --config-file=tools/tmp-session-bus.conf -- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" TESTS_ENVIRONMENT="PYTHONPATH=@abs_top_srcdir@/tests/twisted $(PYTHON)"

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	sofiatest.py \
	servicetest.py

CLEANFILES = sofiasip-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
