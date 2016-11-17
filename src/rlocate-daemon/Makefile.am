RLOCATE = @RLOCATE@

rlocate=`echo rlocate|sed '$(transform)'`
rlocated=`echo rlocated|sed '$(transform)'`

RLOCATE_DB_DIR = $(DESTDIR)$(localstatedir)/$(rlocate)
RLOCATE_DB = $(RLOCATE_DB_DIR)/$(rlocate).db
MODULE_CFG = $(RLOCATE_DB_DIR)/module.cfg
DEVDIR = $(devdir)
PROCDIR = $(procdir)

AM_CPPFLAGS = -DRLOCATE_DB=\"$(RLOCATE_DB)\" \
              -DMODULE_CFG=\"$(MODULE_CFG)\" \
	      -DRLOCATE_GRP=\"$(rlocate_group)\" \
           -DDEVDIR=\"$(DEVDIR)\" \
           -DPROCDIR=\"$(PROCDIR)\"

sbin_PROGRAMS = rlocated
rlocated_SOURCES = ../pidfile.h ../pidfile.c rlocated.c

INSTALL = install -c
AM_CFLAGS =
