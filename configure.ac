AC_PREREQ([2.59])

m4_define([THIS_PACKAGE],[telepathy-sofiasip])
m4_define([VERSION_MAJOR],[0])
m4_define([VERSION_MINOR],[6])
m4_define([VERSION_MICRO],[7])
m4_define([VERSION_NANO],[1])

m4_define([BASE_VERSION],[VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO])
m4_define([THIS_VERSION],
          [m4_if(VERSION_NANO, 0, [BASE_VERSION], [BASE_VERSION.VERSION_NANO])])

AC_INIT(THIS_PACKAGE, THIS_VERSION)
AC_CONFIG_MACRO_DIR([m4])

AS_VERSION(THIS_PACKAGE, TELEPATHY_SIP_VERSION,
           VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_NANO,
           WERROR="no", WERROR="yes")

AM_INIT_AUTOMAKE([1.9 -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AC_DISABLE_SHARED

AC_CONFIG_HEADER([config.h])

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_MKDIR_P

AC_PROG_LIBTOOL

COMPILER_OPTIMISATIONS
COMPILER_COVERAGE

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
  AS_HELP_STRING([--disable-debug],[omit debug code]),
    enable_debug=$enableval, enable_debug=yes )

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

AC_C_INLINE

dnl GTK docs
GTK_DOC_CHECK

dnl Check for Glib 
PKG_CHECK_MODULES(GLIB, gobject-2.0 >= 2.16, have_glib=yes, have_glib=no)

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

dnl Check for sofia
PKG_CHECK_MODULES(SOFIA_SIP_UA, sofia-sip-ua-glib >= 1.12.11)
AC_SUBST(SOFIA_SIP_UA_LIBS)
AC_SUBST(SOFIA_SIP_UA_CFLAGS)
AC_SUBST(SOFIA_SIP_UA_VERSION)

dnl Check for telepathy-glib
PKG_CHECK_MODULES(TELEPATHY_GLIB, [telepathy-glib >= 0.8])
AC_SUBST(TELEPATHY_GLIB_CFLAGS)
AC_SUBST(TELEPATHY_GLIB_LIBS)

dnl Check for optional IP heartbeat support
AC_ARG_WITH(iphb,
  AS_HELP_STRING([--with-iphb],[use IP heartbeat support in Maemo]),
  [],
  [with_iphb=no]
)
if test "x$with_iphb" != xno; then
  PKG_CHECK_MODULES(IPHB, [libiphb >= 0.61.31],
    [
      AC_DEFINE([HAVE_LIBIPHB], [], [IP heartbeat library is available])
    ]
  )
else
  IPHB_CFLAGS=
  IPHB_LIBS=
fi
AC_SUBST(IPHB_CFLAGS)
AC_SUBST(IPHB_LIBS)

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi
AM_PATH_PYTHON([2.3],[],
  [AC_MSG_ERROR([Python is required to compile this package])]
)

dnl Check for twisted python for tests
AC_MSG_CHECKING([for TwistedPython with SIP protocol support])
if $PYTHON -c "import twisted.protocols.sip, twisted.internet.reactor" >/dev/null 2>&1; then
  AC_MSG_RESULT([yes])
  AM_CONDITIONAL([WANT_TWISTED_TESTS], true)
else
  AC_MSG_RESULT([no])
  AM_CONDITIONAL([WANT_TWISTED_TESTS], false)
fi

AC_CONFIG_FILES([
  Makefile
  tpsip/Makefile
  tpsip-extensions/Makefile
  src/Makefile
  m4/Makefile
  data/Makefile
  tests/Makefile
  tests/twisted/Makefile
  tests/twisted/tools/Makefile
  tools/Makefile
  docs/Makefile
])
AC_OUTPUT
