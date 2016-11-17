#! /bin/sh
#
# rlocate	example file to build /etc/init.d/ scripts.
#		This file should be used to construct scripts for /etc/init.d.
#
#		Written by Miquel van Smoorenburg <miquels@cistron.nl>.
#		Modified for Debian 
#		by Ian Murdock <imurdock@gnu.ai.mit.edu>.
#
# Version:	@(#)skeleton  1.9  26-Feb-2001  miquels@cistron.nl
#

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/sbin/rlocated
NAME=rlocated
DESC="rlocate daemon"

load_modules ()
{
        local MODULES_MISSING=false

        for mod in $*
        do
                modprobe -k $mod 2> /dev/null || MODULES_MISSING=true
        done

        if $MODULES_MISSING; then
                echo "#################################################"
                echo "## I couldn't load the required rlocate module ##"
                echo "## You should install rlocate-source to build  ##"
                echo "## the module                                  ##"
                echo "#################################################"
                START_RLOCATE=false
        fi
}

test -x $DAEMON || exit 0

START_RLOCATE=true
LOAD_MODULES=true

# Include rlocate defaults if available
if [ -f /etc/default/rlocate ] ; then
	. /etc/default/rlocate
fi

set -e

case "$1" in
  start)
        if [ "$LOAD_MODULES" = "true" ] && [ "$START_RLOCATE" = "true" ]; then
                load_modules rlocate
        fi
        if $START_RLOCATE; then
	        echo -n "Starting $DESC: $NAME"
	        if start-stop-daemon --stop --quiet --pidfile /var/run/$NAME.pid \
		        --signal -0 --exec $DAEMON
                then
                        echo " already running."
                else
	                if start-stop-daemon --start --quiet \
                                --pidfile /var/run/$NAME.pid \
		                --exec $DAEMON -- $DAEMON_OPTS
                        then
                                echo "."
                        else
                                echo "$NAME failed to start."
                        fi
                fi
        fi
	;;
  stop)
	echo -n "Stopping $DESC: $NAME"
	if start-stop-daemon --stop --quiet --signal 0 \
                --pidfile /var/run/$NAME.pid \
		--exec $DAEMON
        then
	        start-stop-daemon --stop --quiet --pidfile /var/run/$NAME.pid \
		        --exec $DAEMON
                # Now we wait for it to die
                num=0
                while start-stop-daemon --quiet --stop --signal 0 --exec $DAEMON
                do
                        num=$[$num+1]
                        if [ $num -gt 10 ]
                        then 
                                echo -n " not died" 
                                break 
                        fi 
                        sleep 1 
                done 
                echo "." 
        else 
                echo " not running." 
        fi
	;;
  reload)
        if $START_RLOCATE; then
	        echo "Reloading $DESC configuration files."
	        start-stop-daemon --stop --signal 1 --quiet --pidfile \
		        /var/run/$NAME.pid --exec $DAEMON
        fi
        ;;
  restart|force-reload)
        $0 stop
        $0 start
	;;
  *)
	N=/etc/init.d/$NAME
	echo "Usage: $N {start|stop|restart|reload|force-reload}" >&2
	# echo "Usage: $N {start|stop|restart|force-reload}" >&2
	exit 1
	;;
esac

exit 0
