AC_REVISION
dnl Process this file with autoconf to produce a configure script.
AC_INIT([rlocate],[0.5.6],[e9526925@stud3.tuwien.ac.at])
AM_INIT_AUTOMAKE(1.7.1)
AC_CONFIG_SRCDIR([src/slocate.c])
AC_CONFIG_HEADERS([config.h])
#AC_ARG_PROGRAM

#datadir=/var/lib/

dnl Check for programs.

AC_PROG_CC
AM_PROG_CC_C_O
AC_PATH_PROG(mknod, mknod, /bin/mknod)
AC_PATH_PROG(depmod, depmod, /sbin/depmod, $PATH:/sbin)

# Check to see if /etc/cron.daily/ dir exists so that we
# can add the rlocate.cron file to it.
AC_MSG_CHECKING(for '/etc/cron.daily/' directory)
AM_CONDITIONAL(CRON, test -d "/etc/cron.daily")

if [ test -d "/etc/cron.daily" ] ; then
AC_MSG_RESULT(yes)
else
AC_MSG_RESULT(no)
fi


# rlocate
#
AC_ENABLE_UPDATES
AC_PATH_KERNEL_SOURCE
AC_PATH_MODULE
AC_SANDBOXED
AC_MAJOR_NUMBER
AC_RLOCATE_GROUP
AC_UPDATEDB_CONF
AC_PATH_DEV
AC_PATH_STATUS
AC_CHECK_PERL

dnl Checks for libraries.

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_STAT
AC_CHECK_HEADERS(fcntl.h string.h errno.h unistd.h fts.h limits.h)
AC_CHECK_HEADERS(stdlib.h sys/param.h paths.h sys/file.h sys/mount.h grp.h)

AM_CONDITIONAL(SL_FTS, test "$ac_cv_header_fts_h" = "no")

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_OFF_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_TYPE_SIZE_T

dnl Checks for library functions.
AC_FUNC_FNMATCH
AC_PROG_INSTALL
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_REPLACE_FNMATCH
AC_CHECK_FUNCS(getcwd regcomp strdup strerror strstr bzero fchdir memset strchr)
AC_CHECK_FUNCS(strrchr getgrnam)
AC_PROG_INSTALL
AC_PROG_LN_S

AC_CONFIG_FILES([Makefile
          src/Makefile
          src/rlocate-daemon/Makefile
          src/rlocate-module/Makefile
          src/rlocate-scripts/rlocate-checkpoint
          src/rlocate-scripts/Makefile
          doc/Makefile
          doc/man/Makefile
          ])
AC_OUTPUT
