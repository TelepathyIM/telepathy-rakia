NULL = 

TWISTED_TESTS = \
	cm/protocol.py \
	test-register.py \
	test-register-fail.py \
	test-register-sasl.py \
	test-debug.py \
	test-handle-normalisation.py \
	test-message.py \
	test-self-alias.py \
	text/initiate-requestotron.py \
	voip/calltest.py \
	voip/ringing-queued.py \
	voip/requestable-classes.py \
	voip/direction-change.py \
	voip/add-remove-content.py \
	$(NULL)

check-local: check-coding-style check-twisted

CHECK_TWISTED_SLEEP=0

check-twisted: $(BUILT_SOURCES)
	$(MAKE) -C tools
	if test "x$(CHECK_TWISTED_SLEEP)" = x0; then \
		rakia_test_sleep= ; \
	else \
		rakia_test_sleep=--sleep=$(CHECK_TWISTED_SLEEP); \
	fi; \
	RAKIA_TEST_UNINSTALLED=1 \
	  RAKIA_ABS_TOP_SRCDIR=@abs_top_srcdir@ \
	  RAKIA_ABS_TOP_BUILDDIR=@abs_top_builddir@ \
	  RAKIA_TEST_SLEEP=$$rakia_test_sleep \
	  ./run-test.sh "$(TWISTED_TESTS)"

if ENABLE_DEBUG
DEBUGGING_PYBOOL = True
else
DEBUGGING_PYBOOL = False
endif

config.py: Makefile
	$(AM_V_GEN) { \
		echo "PACKAGE_STRING = \"$(PACKAGE_STRING)\""; \
		echo "DEBUGGING = $(DEBUGGING_PYBOOL)"; \
	} > $@

rakia-twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

BUILT_SOURCES = \
	config.py \
	rakia-twisted-tests.list \
	run-test.sh \
	$(NULL)

# We don't really use rakiatestsdir yet - we only support uninstalled testing
# so far - but I'm substituting it to keep the script more similar to Gabble's.
# ${pkglibexecdir}/tests is what GNOME's InstalledTests goal recommends.
#
# Similarly, Gabble supports TEST_PYTHON differing from PYTHON for historical
# reasons, but we don't do that here.
run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]rakiatestsdir[@]!${pkglibexecdir}/tests!' \
			-e 's![@]TEST_PYTHON[@]!$(PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

EXTRA_DIST = \
	$(TWISTED_TESTS) \
	constants.py \
	run-test.sh.in \
	sofiatest.py \
	servicetest.py \
	voip/voip_test.py

CLEANFILES = \
	$(BUILT_SOURCES) \
	rakia-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools
