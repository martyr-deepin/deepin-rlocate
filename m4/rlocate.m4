##
## additional m4 macros
##

dnl look for Linux kernel source

AC_DEFUN([AC_PATH_KERNEL_SOURCE],
[
  AC_MSG_CHECKING([for directory with kernel source])
  if test -d "/lib/modules/`uname -r`/build" -o -L "/lib/modules/`uname -r`/build";
  then
    DEFAULT_KERNEL_DIR="/lib/modules/`uname -r`/build"
  else
    DEFAULT_KERNEL_DIR="/usr/src/linux"
  fi
  AC_ARG_WITH(kernel,
    [  --with-kernel=dir       give the directory with kernel sources]
    [                        [/usr/src/linux]],
    kerneldir="$withval",
    kerneldir="$DEFAULT_KERNEL_DIR"
  )
  AC_MSG_RESULT($kerneldir)
  kernelext=ko
  AC_SUBST(kerneldir)
  AC_SUBST(kernelext)

  dnl Check for kernel version...
  AC_MSG_CHECKING(for kernel version)
  if ! test -r $kerneldir/include/linux/version.h; then
    AC_MSG_ERROR([
*** The file $kerneldir/include/linux/version.h does not exist.
*** Please, install the package with full kernel sources for your distribution
*** or use --with-kernel=dir option to specify another directory with kernel
*** sources (default is $DEFAULT_KERNEL_DIR).
    ])
  fi
  KERNEL_INC="-I$kerneldir/include"
  HACK_KERNEL_INC=""
  ac_save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $KERNEL_INC $HACK_KERNEL_INC"
  AC_CACHE_VAL(kaversion,
  [AC_RUN_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <ctype.h>

  #ifdef USE_CONFIG_H
  #include "$kerneldir/include/linux/config.h"
  #else
  #include "$kerneldir/include/linux/autoconf.h"
  #endif
  #include "$kerneldir/include/linux/version.h"
  
  #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
  #include "$kerneldir/include/linux/utsrelease.h"
  #endif
  ]],
  [[
    FILE *f;
    char kversion[128];
    char kpatchlevel[128];
    char ksublevel[128];
    char kextra[128];
    char *ptr, *dptr;
#ifdef UTS_RELEASE
    f=fopen("./conftestdata","w");
    if (f) {
      for (ptr = UTS_RELEASE, dptr = kversion; *ptr != '\0' && isdigit(*ptr); ptr++, dptr++)
      *dptr = *ptr;
      *dptr = '\0';
      if (*ptr == '.')
        ptr++;
      for (dptr = kpatchlevel; *ptr != '\0' && isdigit(*ptr); ptr++, dptr++)
        *dptr = *ptr;
      *dptr = '\0';
      if (*ptr == '.')
        ptr++;
      for (dptr = ksublevel; *ptr != '\0' && isdigit(*ptr); ptr++, dptr++)
        *dptr = *ptr;
      *dptr = '\0';
      for (dptr = kextra; *ptr != '\0'; ptr++, dptr++)
        *dptr = *ptr;
      *dptr = '\0';   
      fprintf(f,"%s:%s:%s:%s\n",kversion,kpatchlevel,ksublevel,kextra);

      fprintf(f, "CONFIG_SECURITY=");
#ifdef CONFIG_SECURITY
      fprintf(f, "1\n");
#else
      fprintf(f, "0\n");
#endif
        
      fprintf(f, "CONFIG_SECURITY_CAPABILITIES=");
#ifdef CONFIG_SECURITY_CAPABILITIES
      fprintf(f, "1\n");
#else
      fprintf(f, "0\n");
#endif

      fprintf(f, "CONFIG_SECURITY_CAPABILITIES_MODULE=");
#ifdef CONFIG_SECURITY_CAPABILITIES_MODULE
      fprintf(f, "1\n");
#else
      fprintf(f, "0\n");
#endif

      fclose(f);
    }
    exit(0);
#else
    exit(1);
#endif
  ]])], [kaversion=`head -n 1 conftestdata`],
        [kaversion=""],
        [kaversion=""])])

  CFLAGS="$ac_save_CFLAGS"
  kversion=`echo $kaversion      | cut -d : -f 1`
  kpatchlevel=`echo $kaversion   | cut -d : -f 2`
  ksublevel=`echo $kaversion     | cut -d : -f 3`
  kextraversion=`echo $kaversion | cut -d : -f 4`
  kversion=`expr $kversion + 0`
  kpatchlevel=`expr $kpatchlevel + 0`
  ksublevel=`expr $ksublevel + 0`
  if test -z "$kversion" || test -z "$kpatchlevel" || test -z "$ksublevel"; then
    AC_MSG_ERROR([*** probably missing $kerneldir/include/linux/version.h])
  fi
  
  kaversion="$kversion.$kpatchlevel.$ksublevel$kextraversion"

  AC_MSG_RESULT($kaversion)

  if test $kpatchlevel -lt 6; then
    AC_MSG_ERROR([*** rlocate needs kernel 2.6 to work])
  fi


  AC_MSG_CHECKING(for security module support)
  CONFIG_SECURITY=`grep 'CONFIG_SECURITY=' conftestdata | cut -d = -f 2`
  CONFIG_SECURITY=`expr $CONFIG_SECURITY + 0`
  if test $CONFIG_SECURITY -eq 0; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([*** CONFIG_SECURITY must be enabled in the kernel config])
  else
    AC_MSG_RESULT(yes)
  fi
  
  AC_MSG_CHECKING(if capabilities are built-in)
  CONFIG_SECURITY_CAPABILITIES=`grep 'CONFIG_SECURITY_CAPABILITIES=' conftestdata | cut -d = -f 2`
  CONFIG_SECURITY_CAPABILITIES=`expr $CONFIG_SECURITY_CAPABILITIES + 0`
  if test $CONFIG_SECURITY_CAPABILITIES -eq 1; then
    AC_MSG_RESULT(yes)
    AC_MSG_ERROR([*** Capabilities must be built as a module or disabled in the kernel config. ])
  else
    AC_MSG_RESULT(no)
  fi
  
  AC_MSG_CHECKING(if capabilities are built as module)
  CONFIG_SECURITY_CAPABILITIES_MODULE=`grep 'CONFIG_SECURITY_CAPABILITIES_MODULE=' conftestdata | cut -d = -f 2`
  CONFIG_SECURITY_CAPABILITIES_MODULE=`expr $CONFIG_SECURITY_CAPABILITIES_MODULE + 0`
  if test $CONFIG_SECURITY_CAPABILITIES_MODULE -eq 1; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi

  AC_SUBST(kaversion)
  AC_SUBST(kversion)
  AC_SUBST(kpatchlevel)
  AC_SUBST(ksublevel)
  AC_SUBST(kextraversion)
]
)

dnl enable updates in rlocate module

AC_DEFUN([AC_ENABLE_UPDATES],
[
  AC_MSG_CHECKING([whether to enable updates in rlocate module])
  dnl updates are disabled by default
  AC_ARG_ENABLE(updates,
    [  --enable-updates        enable updates in rlocate module],
    [ enable_updates="${enableval}" ],
    [ enable_updates="no"])
  AC_MSG_RESULT($enable_updates)
  AM_CONDITIONAL(RLOCATE_UPDATES, test x$enable_updates = xyes)
]
)

dnl Directory for modules

AC_DEFUN([AC_PATH_MODULE],
[
  AC_MSG_CHECKING([for directory to store the kernel module])
  AC_ARG_WITH(moduledir,
    [  --with-moduledir=/path  give the path for the rlocate kernel module]
    [                        [/lib/modules/<KVER>/misc]],
    moduledir="$withval",
    moduledir="/lib/modules/$kaversion/misc"
  )
  AC_SUBST(moduledir)
  AC_MSG_RESULT($moduledir)
]
)

dnl don't create nor destroy devices when installing and uninstalling

AC_DEFUN([AC_SANDBOXED],
[
  AC_MSG_CHECKING([whether to enable sandboxed])
  AC_ARG_ENABLE(sandboxed,
    [  --enable-sandboxed      don't touch anything out of the install directory],
    [ enable_sandboxed="${enableval}" ],
    [ enable_sandboxed="no"])
  AC_MSG_RESULT($enable_sandboxed)
  AM_CONDITIONAL(SANDBOXED, test x$enable_sandboxed = xyes)
]
)

dnl specify the device major number

AC_DEFUN([AC_MAJOR_NUMBER],
[
  AC_ARG_WITH(major,
    [  --with-major=value      specify the device major for the driver (254)],
    rlocate_major=${withval},
    rlocate_major=254)
  AC_SUBST(rlocate_major)
]
)

dnl rlocate group

AC_DEFUN([AC_RLOCATE_GROUP],
[
  AC_ARG_WITH(rlocate_group,
    [  --with-rlocate-group=GROUP	group name (rlocate)],
    rlocate_group=${withval},
    rlocate_group="rlocate")
  AC_SUBST(rlocate_group)
]
)

dnl where updatedb.conf is located

AC_DEFUN([AC_UPDATEDB_CONF],
[
  AC_ARG_WITH(updatedb_conf,
    [  --with-updatedb-conf=DIR      where updatedb.conf is located (/etc/updatedb.conf)],
    updatedb_conf=${withval},
    updatedb_conf="/etc/updatedb.conf")
  AC_SUBST(updatedb_conf)
]
)


dnl where to install device file

AC_DEFUN([AC_PATH_DEV],
[
  AC_ARG_WITH(devdir,
    [  --with-devdir=DIR       install device files in DIR (/dev)],
    devdir=${withval},
    devdir="/dev")
  AC_SUBST(devdir)
]
)

dnl where rlocate status is located

AC_DEFUN([AC_PATH_STATUS],
[
  AC_ARG_WITH(procdir,
    [  --with-procdir=DIR      where rlocate status file is located (/proc)],
    procdir=${withval},
    procdir="/proc")
  AC_SUBST(procdir)
]
)


dnl check perl

AC_DEFUN([AC_CHECK_PERL],
[
  AC_PATH_PROG(PERL, perl)
  if test -z "$PERL"; then
    AC_MSG_ERROR([perl not found])
  fi
  $PERL -e 'require 5.006;' || {
          AC_MSG_ERROR([perl 5.006 or better is required])
  }
]
)
