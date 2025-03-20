AC_INIT([gperftools-demo],[1.0])

AC_CONFIG_SRCDIR([README.adoc])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign nostdinc])

AC_PROG_CXX
AC_LANG([C++])

AX_CXX_COMPILE_STDCXX(20, noext, mandatory)

PKG_CHECK_MODULES([tcmalloc], [libtcmalloc])
PKG_CHECK_MODULES([cpuprofiler], [libprofiler])
PKG_CHECK_MODULES([absl_btree], [absl_btree], [have_abseil=yes], [have_abseil=no])

AM_CONDITIONAL(BUILD_BTREE, [test "x$have_abseil" = xyes])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
