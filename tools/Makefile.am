EXTRA_DIST = \
    c-constants-generator.xsl \
    check-coding-style.mk \
    check-c-style.sh \
    check-misc.sh \
    check-whitespace.sh \
    c-interfaces-generator.xsl \
    doc-generator.xsl \
    glib-client-gen.py \
    glib-client-marshaller-gen.py \
    glib-errors-enum-body.xsl \
    glib-errors-enum-header.xsl \
    glib-interfaces-generator.xsl \
    glib-interfaces-body-generator.xsl \
    glib-ginterface-gen.py \
    glib-gtypes-generator.py \
    glib-signals-marshal-gen.py \
    identity.xsl \
    lcov.am \
    libglibcodegen.py \
    libtpcodegen.py \
    telepathy.am

CLEANFILES = \
    libglibcodegen.pyc libglibcodegen.pyo \
    libtpcodegen.pyc libtpcodegen.pyo \
    $(noinst_SCRIPTS) \
    $(NULL)

all: $(EXTRA_DIST)

glib-client-marshaller-gen.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-ginterface-gen.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-gtypes-generator.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
glib-signals-marshal-gen.py: libglibcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@
libglibcodegen.py: libtpcodegen.py
	$(AM_V_GEN)test -e ${srcdir}/$@ && touch ${srcdir}/$@

TELEPATHY_GLIB_SRCDIR = $(top_srcdir)/../telepathy-glib
maintainer-update-from-telepathy-glib:
	set -e && cd $(srcdir) && \
	for x in $(EXTRA_DIST); do \
		if test -f $(TELEPATHY_GLIB_SRCDIR)/tools/$$x; then \
			cp $(TELEPATHY_GLIB_SRCDIR)/tools/$$x $$x; \
		fi; \
	done
