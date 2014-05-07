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
	CHECK_TWISTED_UNINSTALLED=1 \
	  G_TEST_SRCDIR=@abs_srcdir@ \
	  G_TEST_BUILDDIR=@abs_builddir@ \
	  CHECK_TWISTED_SLEEP=$$rakia_test_sleep \
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

twisted-tests.list: Makefile
	$(AM_V_GEN)echo $(TWISTED_TESTS) > $@

built_test_data = \
	config.py \
	twisted-tests.list \
	$(NULL)

built_test_scripts = \
	run-test.sh \
	$(NULL)

BUILT_SOURCES = \
	$(built_test_data) \
	$(built_test_scripts) \
	$(NULL)

if ENABLE_INSTALLED_TESTS
testsdir = ${datadir}/telepathy-rakia-1-tests
twistedtestsdir = ${testsdir}/twisted
nobase_dist_twistedtests_DATA = $(dist_test_data)
twistedtests_DATA = $(built_test_data)
twistedtests_SCRIPTS = $(built_test_scripts)
endif

run-test.sh: run-test.sh.in Makefile
	$(AM_V_GEN)sed \
			-e 's![@]twistedtestsdir[@]!${twistedtestsdir}!' \
			-e 's![@]TEST_PYTHON[@]!$(PYTHON)!' \
			< $< > $@.tmp && \
		chmod +x $@.tmp && \
		mv $@.tmp $@

dist_test_data = \
	$(TWISTED_TESTS) \
	constants.py \
	sofiatest.py \
	servicetest.py \
	voip/voip_test.py \
	$(NULL)

EXTRA_DIST = \
	$(dist_test_data) \
	run-test.sh.in \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	rakia-[1-9]*.log \
	*.pyc \
	*/*.pyc \
	$(NULL)

check_misc_sources = $(TESTS)

include $(top_srcdir)/tools/check-coding-style.mk

SUBDIRS = tools

%.test: Makefile
	@$(MKDIR_P) $(dir $*)
	$(AM_V_GEN)( echo '[Test]'; \
		echo 'Exec=${twistedtestsdir}/run-test.sh $*.py'; \
		echo 'Type=session'; \
		echo 'Output=TAP' ) > $@.tmp
	@mv $@.tmp $@

insttestdir = ${datadir}/installed-tests/telepathy-rakia-1

if ENABLE_INSTALLED_TESTS
nobase_nodist_insttest_DATA = \
	$(patsubst %.py,%.test,$(TWISTED_TESTS)) \
	$(NULL)
endif
