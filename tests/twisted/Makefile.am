NULL = 

TWISTED_TESTS = \
	cm/protocol.py \
	test-register.py \
	test-register-fail.py \
	test-register-sasl.py \
	test-handle-normalisation.py \
	test-message.py \
	test-self-alias.py \
	text/initiate-requestotron.py \
	voip/incoming-basics.py \
	voip/outgoing-basics.py \
	voip/voip_test.py \
	$(NULL)

TESTS =

TESTS_ENVIRONMENT = \
	PYTHONPATH=@abs_top_srcdir@/tests/twisted

check-local: check-coding-style check-twisted

CHECK_TWISTED_SLEEP=0

check-twisted:
	$(MAKE) -C tools
	rm -f tools/sofiasip-testing.log
	sh $(srcdir)/tools/with-session-bus.sh \
		--config-file=tools/tmp-session-bus.conf \
		--sleep=$(CHECK_TWISTED_SLEEP) \
		-- $(MAKE) check-TESTS \
		TESTS="$(TWISTED_TESTS)" \
		TESTS_ENVIRONMENT="$(TESTS_ENVIRONMENT) $(PYTHON)"

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	constants.py \
	sofiatest.py \
	servicetest.py

CLEANFILES = sofiasip-[1-9]*.log *.pyc */*.pyc

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
