ACLOCAL_AMFLAGS = -I m4

rlocate=`echo rlocate|sed '$(transform)'`
rlocated=`echo rlocated|sed '$(transform)'`
root_uid=0
locate=locate
updatedb=updatedb

RLOCATE_DB_DIR = $(DESTDIR)$(localstatedir)/$(rlocate)
RLOCATE_DB = $(RLOCATE_DB_DIR)/$(rlocate).db
RLOCATE_DB_TMP = $(RLOCATE_DB_DIR)/$(rlocate).db.tmp
MODULE_CFG = $(RLOCATE_DB_DIR)/module.cfg
RLOCATE_GRP = $(rlocate_group)
RLOCATED_CMD = $(DESTDIR)$(sbindir)/$(rlocated)
UPDATEDB_CONF = $(updatedb_conf)
PROCDIR = $(procdir)

AM_CPPFLAGS = -DRLOCATE_DB=\"$(RLOCATE_DB)\" \
           -DRLOCATE_DB_TMP=\"$(RLOCATE_DB_TMP)\" \
           -DRLOCATE_DB_DIR=\"$(RLOCATE_DB_DIR)\" \
           -DMODULE_CFG=\"$(MODULE_CFG)\" \
           -DRLOCATE_GRP=\"$(RLOCATE_GRP)\" \
           -DRLOCATED_CMD=\"$(RLOCATED_CMD)\" \
           -DUPDATEDB_CONF=\"$(UPDATEDB_CONF)\" \
           -DPROCDIR=\"$(PROCDIR)\"

bin_PROGRAMS = rlocate
rlocate_SOURCES = pidfile.h pidfile.c slocate.c slocate.h \
		  rlocate.h rlocate.c cmds.c cmds.h conf.c conf.h utils.c \
	   	  utils.h
SUBDIRS = rlocate-daemon rlocate-scripts
EXTRA_DIST = rlocate.cron rlocate-scripts install-cron.sh.in

INSTALL = install -c
AM_CFLAGS =

install-exec-hook:
	$(INSTALL) -d $(RLOCATE_DB_DIR)
	chown $(root_uid):$(rlocate_group) $(DESTDIR)$(bindir)/$(rlocate)
	chown $(root_uid):$(rlocate_group) $(RLOCATE_DB_DIR)
	chmod 2755 $(DESTDIR)$(bindir)/$(rlocate)
	chmod 0750 $(RLOCATE_DB_DIR)
	-$(LN_S) -f $(rlocate) $(DESTDIR)$(bindir)/$(updatedb)
	-$(LN_S) -f $(rlocate) $(DESTDIR)$(bindir)/$(locate)
if CRON
	$(mkinstalldirs) $(DESTDIR)/etc/cron.daily/
	sed \
	-e "s,@UPDATEDB@,$(bindir)/$(updatedb)," \
	$(srcdir)/rlocate.cron > $(DESTDIR)/etc/cron.daily/$(rlocate)
	chmod 0755 $(DESTDIR)/etc/cron.daily/$(rlocate)
else
	sed \
	-e "s,@UPDATEDB@,$(bindir)/$(updatedb)," \
	$(srcdir)/install-cron.sh.in > $(srcdir)/install-cron.sh
	chmod 0755 $(srcdir)/install-cron.sh
	./install-cron.sh
	
endif
	$(mkinstalldirs) $(RLOCATE_CFG_DIR)/

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/$(locate)
	rm -f $(DESTDIR)$(bindir)/$(updatedb)
if CRON
	rm -f $(DESTDIR)/etc/cron.daily/rlocate
else
	grep -v "rlocate\|updatedb" $(DESTDIR)/etc/crontab >$(DESTDIR)/etc/crontab.$(rlocate)
	mv $(DESTDIR)/etc/crontab.$(rlocate) $(DESTDIR)/etc/crontab
endif
	rm -f $(RLOCATE_DB_DIR)/*
	-rmdir $(RLOCATE_DB_DIR)
