## Process this file with automake to produce Makefile.in

## this is so that Automake includes the C compiling definitions, and
## includes the source files in the distribution.
EXTRA_PROGRAMS = automake_dummy
automake_dummy_SOURCES = rlocate.c
EXTRA_DIST = Makefile.kernel
## there is no *just* object file support in automake.  This is close enough
module_DATA = rlocate.o

# where the kernel sources are located
KERNEL_LOCATION=@kerneldir@

RLOCATE_DEVDIR = `cd $(srcdir) && pwd` # so that make dist works
# some magic for using linux kernel settings
# when compiling module(s)
KBUILD_VERBOSE = 1
if RLOCATE_UPDATES
D_RLOCATE_UPDATES=-DRLOCATE_UPDATES
endif
RLOCATE_EXTRA_CFLAGS = -DIRCTL_DEV_MAJOR=$(rlocate_major) \
		       -DRL_VERSION=\"$(VERSION)\" \
		       -DEXPORT_SYMTAB $(DEFS) \
		       $(D_RLOCATE_UPDATES) \
		       $(DEFAULT_INCLUDES) -I $(srcdir)/ \
		       -I $(KERNEL_LOCATION)/include/
export RLOCATE_EXTRA_CFLAGS KERNEL_LOCATION module_DATA

$(module_DATA): $(automake_dummy_SOURCES) $(top_srcdir)/config.h 
	mv Makefile Makefile.automake
	cp $(srcdir)/Makefile.kernel $(srcdir)/Makefile
	$(MAKE) -C $(KERNEL_LOCATION) SUBDIRS=$(RLOCATE_DEVDIR) modules \
		KBUILD_VERBOSE=$(KBUILD_VERBOSE)
	mv Makefile.automake Makefile
install-moduleDATA: $(module_DATA)
	$(mkinstalldirs) $(DESTDIR)$(moduledir)
	@list='$(module_DATA:.o=.@kernelext@)'; for p in $$list; do \
	  if test -f "$$p"; then d=; else d="$(srcdir)/"; fi; \
	  f="`echo $$p | sed -e 's|^.*/||'`"; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(moduledir)/$$f"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(moduledir)/$$f; \
	done

uninstall-moduleDATA:
	@list='$(module_DATA:.o=.@kernelext@)'; for p in $$list; do \
	  f="`echo $$p | sed -e 's|^.*/||'`"; \
	  echo " rm -f $(DESTDIR)$(moduledir)/$$f"; \
	  rm -f $(DESTDIR)$(moduledir)/$$f; \
	done

#if SANDBOXED
#else
#install-exec-local: mkdev
#uninstall-local: rmdev
#endif

mkdev:
	test -c $(DESTDIR)$(devdir)/rlocate || ($(mkinstalldirs) $(DESTDIR)$(devdir) && @mknod@ $(DESTDIR)$(devdir)/rlocate c @rlocate_major@ 0)

rmdev:
	-test -c $(DESTDIR)$(devdir)/rlocate && $(RM) $(DESTDIR)$(devdir)/rlocate

if SANDBOXED
else
install-data-local: install-moduleDATA
	-@depmod@ -a
endif

CLEANFILES = $(module_DATA) .*.cmd $(module_DATA:.o=.mod.c) $(module_DATA:.o=.@kernelext@) *~
