bin_SCRIPTS = rlocate-checkpoint

rlocate=`echo rlocate|sed '$(transform)'`
rlocated=`echo rlocated|sed '$(transform)'`
root_uid=0

RLOCATE_DB_DIR = $(DESTDIR)$(localstatedir)/$(rlocate)
RLOCATE_DB = $(RLOCATE_DB_DIR)/$(rlocate).db
RLOCATED_CMD = $(DESTDIR)$(sbindir)/$(rlocated)

all-local:
	sed \
	-e "s,@RLOCATE_DB@,$(RLOCATE_DB)," \
	-e "s,@RLOCATED_CMD@,$(RLOCATED_CMD)," \
	$(srcdir)/rlocate-checkpoint > \
		$(srcdir)/rlocate-checkpoint.temp
	cp $(srcdir)/rlocate-checkpoint.temp $(srcdir)/rlocate-checkpoint
	rm $(srcdir)/rlocate-checkpoint.temp

install-exec-hook:
	-/usr/sbin/groupadd $(rlocate_group)
	chown $(root_uid):$(rlocate_group) $(DESTDIR)$(bindir)/rlocate-checkpoint
	chmod 2755 $(DESTDIR)$(bindir)/rlocate-checkpoint

uninstall-hook:
	-/usr/sbin/groupdel $(rlocate_group)
