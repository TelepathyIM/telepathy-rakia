AC_PREREQ([2.60])

m4_define([THIS_PACKAGE],[telepathy-rakia])
m4_define([VERSION_MAJOR],[0])
m4_define([VERSION_MINOR],[8])
m4_define([VERSION_MICRO],[0])
m4_define([VERSION_NANO],[1])

m4_define([BASE_VERSION],[VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO])
m4_define([THIS_VERSION],
          [m4_if(VERSION_NANO, 0, [BASE_VERSION], [BASE_VERSION.VERSION_NANO])])

AC_INIT(THIS_PACKAGE, THIS_VERSION)
AC_CONFIG_MACRO_DIR([m4])

AS_VERSION(THIS_PACKAGE, TELEPATHY_SIP_VERSION,
           VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, VERSION_NANO,
           IS_RELEASE="yes", IS_RELEASE="no")

AM_INIT_AUTOMAKE([1.9 -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AC_DISABLE_SHARED

AC_CONFIG_HEADER([config.h])

dnl check for tools
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_MKDIR_P

AC_PROG_LIBTOOL

COMPILER_OPTIMISATIONS
COMPILER_COVERAGE

AS_IF([test "x$IS_RELEASE" = xyes],
[ # version x.y.z - "official release",
  # disable extra checks by default
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--enable-fatal-warnings],
                    [make various warnings fatal])],
    [],
    [enable_fatal_warnings=no])
],
[ # tp-glib is version x.y.z.1 - development snapshot,
  # enable extra checks by default
  AC_ARG_ENABLE([fatal-warnings],
    [AC_HELP_STRING([--disable-fatal-warnings],
                    [make various warnings non-fatal])],
    [],
    [enable_fatal_warnings=yes])
])

TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test "x$enable_fatal_warnings" = xyes],
  [all \
   extra \
   declaration-after-statement \
   shadow \
   missing-prototypes \
   nested-externs \
   pointer-arith \
   sign-compare \
   strict-prototypes \
   format-security \
   init-self],
  [missing-field-initializers \
   unused-parameter \
   deprecated-declarations])
AC_SUBST([ERROR_CFLAGS])

# these aren't really error flags but they serve a similar purpose for us -
# making the toolchain stricter
if test "x$enable_fatal_warnings" = xyes; then
  TP_ADD_LINKER_FLAG([ERROR_LDFLAGS], [-Wl,--no-copy-dt-needed-entries])
fi

AC_ARG_ENABLE(debug,
  AS_HELP_STRING([--disable-debug],[omit debug code]),
    enable_debug=$enableval, enable_debug=yes )

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi
AM_CONDITIONAL([ENABLE_DEBUG], [test "x$enable_debug" = xyes])

AC_C_INLINE

dnl GTK docs
GTK_DOC_CHECK

PKG_CHECK_MODULES([GLIB], [gobject-2.0 >= 2.30, gio-2.0 >= 2.30])
GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST([GLIB_GENMARSHAL])

dnl MIN_REQUIRED must stay 2.30 because of GValueArray
AC_DEFINE([GLIB_VERSION_MIN_REQUIRED], [GLIB_VERSION_2_30], [Ignore post 2.30 deprecations])
AC_DEFINE([GLIB_VERSION_MAX_ALLOWED], [GLIB_VERSION_2_30], [Prevent post 2.30 APIs])

PKG_CHECK_MODULES([DBUS], [dbus-1 >= 0.60, dbus-glib-1 >= 0.60])
PKG_CHECK_MODULES([SOFIA_SIP_UA], [sofia-sip-ua-glib >= 1.12.11])
PKG_CHECK_MODULES([TELEPATHY_GLIB], [telepathy-glib >= 0.17.7])

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
  rakia/Makefile
  extensions/Makefile
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
