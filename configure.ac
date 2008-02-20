AC_PREREQ([2.59])

m4_define([THIS_PACKAGE],[telepathy-sofiasip])
m4_define([VERSION_MAJOR],[0])
m4_define([VERSION_MINOR],[5])
m4_define([VERSION_MICRO],[4])
m4_define([VERSION_NANO],[1])

AC_INIT(THIS_PACKAGE,VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO.VERSION_NANO)
AC_CONFIG_MACRO_DIR([m4])

AS_VERSION(THIS_PACKAGE, TELEPATHY_SIP_VERSION,
           VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_NANO,
           WERROR="no", WERROR="yes")

AM_INIT_AUTOMAKE([1.9 -Wno-portability])

AM_PROG_LIBTOOL
AM_CONFIG_HEADER(config.h)

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_MKDIR_P

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, WALL="yes", WALL="no")

if test "x$WALL" = "xyes"; then
  ERROR_CFLAGS="-Wall"

  if test "x$WERROR" = "xyes"; then
    AS_COMPILER_FLAG(-Werror,ERROR_CFLAGS="$ERROR_CFLAGS -Werror",ERROR_CFLAGS="$ERROR_CFLAGS")
  fi
fi

AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[omit debug code]),
    enable_debug=$enableval, enable_debug=yes )

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

AC_C_INLINE

AC_ARG_ENABLE(syslog,
  AC_HELP_STRING([--enable-syslog],[use syslog for Sofia log messages]),
  [enable_syslog=$enableval],
  [enable_syslog=no]
)
if test x$enable_syslog = xyes; then
  AC_DEFINE(ENABLE_SYSLOG, [], [Enable syslog handler for Sofia log messages])
fi

AC_ARG_ENABLE(coverage,
  AC_HELP_STRING([--enable-coverage],[compile with coverage info]),
  [enable_coverage=$enableval],
  [enable_coverage=no]
)
if test x$enable_coverage = xyes; then
  COVERAGE_CFLAGS="-g -fprofile-arcs -ftest-coverage"
fi
AC_SUBST(COVERAGE_CFLAGS)

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, gobject-2.0 >= 2.4, have_glib=yes, have_glib=no)

if test x$have_glib = xno ; then
    AC_MSG_ERROR([GLib development libraries not found])
fi

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_GENMARSHAL)

dnl Check for DBus
PKG_CHECK_MODULES(DBUS, [dbus-1 >= 0.60, dbus-glib-1 >= 0.60], have_dbus=yes, have_dbus=no)

AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

AS_AC_EXPAND(DATADIR, $datadir)
DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
AC_SUBST(DBUS_SERVICES_DIR)
AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

dnl Check for sofia
PKG_CHECK_MODULES(SOFIA_SIP_UA, sofia-sip-ua-glib >= 1.12.7)
AC_SUBST(SOFIA_SIP_UA_LIBS)
AC_SUBST(SOFIA_SIP_UA_CFLAGS)
AC_SUBST(SOFIA_SIP_UA_VERSION)

dnl Check for telepathy-glib
PKG_CHECK_MODULES(TELEPATHY_GLIB, [telepathy-glib >= 0.7.3])
AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi
PYTHON=
AC_CHECK_PROGS([PYTHON], [python2.3 python2.4 python2.5 python])
if test -z "$PYTHON"; then
  AC_MSG_ERROR([Python is required to compile this package])
fi

AC_OUTPUT( Makefile \
extensions/Makefile \
src/Makefile \
m4/Makefile \
data/Makefile \
tests/Makefile \
tools/Makefile \
docs/Makefile
)
