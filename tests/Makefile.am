TESTS = 
TWISTED_TESTS = \
    test-register.py \
    test-register-fail.py \
    test-handle-normalisation.py

_check-twisted:
	$(MAKE) check TESTS="$(TWISTED_TESTS)" TESTS_ENVIRONMENT=python

check-twisted:
	chmod u+x *.sh
	sh run-with-tmp-session-bus.sh $(MAKE) _check-twisted

exec-with-log.sh: exec-with-log.sh.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

%.conf: %.conf.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

%.service: %.service.in
	@sed -e "s|[@]abs_top_builddir[@]|@abs_top_builddir@|g" $< > $@

# D-Bus service file for testing
service_in_files = org.freedesktop.Telepathy.ConnectionManager.sofiasip.service.in
service_files = $(service_in_files:.service.in=.service)

# D-Bus config file for testing
conf_in_files = run-with-tmp-session-bus.conf.in
conf_files = $(conf_in_files:.conf.in=.conf)

BUILT_SOURCES = $(service_files) $(conf_files) exec-with-log.sh

EXTRA_DIST = $(TWISTED_TESTS) $(service_in_files) $(conf_in_files) exec-with-log.sh.in

CLEANFILES = $(BUILT_SOURCES) sofiasip-[1-9]*.log
