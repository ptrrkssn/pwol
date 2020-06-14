#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pwol], [1.5.2], [pen@lysator.liu.se], [pwol], [https://github.com/ptrrkssn/pwol])

AC_CONFIG_SRCDIR([pwol.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB([socket], [connect])
AC_CHECK_LIB([nsl], [gethostbyname])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h syslog.h unistd.h sys/ethernet.h net/ethernet.h netinet/ether.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([dup2 memset socket strchr strdup strerror ether_hostton])

AC_CONFIG_FILES([Makefile pwol.1 pwol.conf.5 pkgs/Makefile.port])
AC_OUTPUT
