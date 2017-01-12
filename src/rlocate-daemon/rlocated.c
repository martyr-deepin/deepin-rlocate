/*
 * rlocated.c
 *
 * Copyright (C) 2004 Rasto Levrinc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* rlocated is a daemon that reads rlocate dev file and copies it to the
 * rlocate db file every INTERVAL seconds.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "../pidfile.h"
#include <string.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>
#include <grp.h>
#include <config.h>
#include <syslog.h>
#include <limits.h>
#include <ctype.h>
//#define __USE_GNU
#include <search.h>
#include <sys/types.h>

#include <fts.h>

#define RL_VERSION "rlocate daemon " VERSION "\n"

#define ROOT_UID 0

static struct hsearch_data DEV_TO_MOUNTP;

typedef struct mount_list {
	char *dev_file;
	struct mount_list *next;
} Mount_list;

Mount_list *mount_list_head = NULL;
char *LVM_MINOR_TO_DEV[256];
int LVM_MINOR_TO_DEV_LEN;

static char *OUTPUT          = NULL; // can be set from command line
static int  OUTPUT_OPTION    = 0; // 1 if --output option was specified
static char *MODULE_VERSION  = NULL; // is set in parse_proc
static char **EXCLUDE_DIR    = NULL; // is set in parse_proc
static char *STARTING_PATH   = NULL; // is set in parse_proc
static int  COUNTDOWN        = 0; // used for testing, always set it to 0
static char *RLOCATEDEV      = DEVDIR"/rlocate";
static char *RLOCATEPROC     = PROCDIR"/rlocate";
static char *RLOCATE_DIFF_DB = NULL;

#define MB  (1024 * 1024);

static char *PidFile = _PATH_VARRUN "rlocated.pid";
static int  NO_DAEMON = 0; /* don't run in daemon mode */
static int  NO_LOOP   = 0; /* don't run in loop */
static int  INTERVAL  = 2; /* read every 2 seconds */
static int  THRESHOLD = 200 * MB; /* default diff size threshold is 200MB */
static FILE *fd_dev;        /* file handle for dev file */
static FILE *fd_db;         /* file handle for db file  */
static int  fd_proc;       /* file handle for proc file  */
static int  fd_cfg;        /* file handle for module config file  */

static char buf[BUFSIZ];   /* buffer for copy from dev file to db file */
static int RELOAD_CONFIG;      /* if set to 1, the config will be reloaded */
static char *PROGNAME;

long long get_file_size(const char* path) {
    struct stat buf;
    if (stat(path, &buf) == 0) {
        return buf.st_size;
    }
    return 0;
}


/*
 * print_log()
 */
void print_log(int level, char *format_str, ...) {
        va_list ap;
        va_start(ap, format_str);
        if (NO_DAEMON) {
                fprintf(stderr, "%s: ", PROGNAME);
                if (level == LOG_WARNING)
                        fprintf(stderr, "WARNING: ");
                vfprintf(stderr, format_str, ap);
                fputc('\n', stderr); fflush(stderr);
        } else {
                vsyslog(level, format_str, ap );
        }
        va_end(ap);
}

/*
 * clean_up
 */
static void clean_up()
{
        print_log(LOG_INFO, "rlocated daemon terminated");
        exit(1);
}

/*
 * xmalloc() allocate n bytes with malloc.
 */
void *xmalloc(const unsigned n) {
	void *p;
	p = malloc(n);
	if (p) return p;
	print_log(LOG_ERR, "malloc: %s\n", strerror(errno));
	clean_up();
	exit(1);
}

/*
 * xstrdup() return copy of string.
 */
void *xstrdup(const char *s) {
        void *p;
        p = strdup(s);
        if (p) return p;
        print_log(LOG_ERR, "strdup: %s\n", strerror(errno));
        clean_up();
        exit(1);
}

/*
 * stop_daemon
 */
void stop_daemon(sig)
        int sig;
{
        clean_up();
        return;
}

/*
 * reload_config
 */
void reload_config(sig)
        int sig;
{
        print_log(LOG_INFO, "reloading config");
        RELOAD_CONFIG = 1;
        return;
}
/*
 * Usage
 */

void usage()
{
        printf("%s\n"
               "Copyright (c) 2004 Rasto Levrinc\n\n"
               "usage: %s [-n] [-o <file>] [--output=<file>]\n\n"
               "   Options:\n"
               "   -n --nodaemon        - Don't run in daemon mode.\n"
               "   -l --noloop          - Don't run in loop.\n"
               "   -i <seconds>\n"
               "   --interval=<seconds> - Refresh every n seconds.\n"
               "   --threshold=<MB>     - the diff db threshold size in MB.\n"
               "   -o <file>\n"
               "   --output=<file>      - Specifies the rlocate database.\n"
               "   -h --help            - Display this help.\n"
               "   -V --version         - Display version.\n"
               "\n",
               RL_VERSION, PROGNAME);
               exit(0);
}

/*
 * cfg_to_proc() is used, so that no error is reported twice. After
 * every error it sleeps for 60 seconds.
 */
int cfg_to_proc(const int type)
{
        static int last_error = 0;
        static int error = 0;
        error++;
        if (type == 0)
                return 0;
        else if (type == -1) {
                error = 0;
                last_error = 0;
                return 0;
        }
        if (fd_cfg>0) {
                if (close(fd_cfg) < 0)
                        print_log(LOG_WARNING, "cannot close %s: %s",
                                  MODULE_CFG, strerror(errno) );
        }
        if (fd_proc>0) {
                if (close(fd_proc) < 0)
                        print_log(LOG_WARNING, "cannot close %s: %s",
                                  RLOCATEPROC, strerror(errno) );
        }
        if ( last_error == error) {
                error = 0;
                sleep(60);
                return 0;
        } else {
                last_error = error;
                error = 0;
                return 1;
        }
}

int cfg_to_proc_error(void)
{
        return cfg_to_proc(1);
}

void cfg_to_proc_ok(void)
{
        cfg_to_proc(0);
}

void cfg_to_proc_done(void)

{
        cfg_to_proc(-1);
}

/*
 * init_module() copies content from module config file to proc file and
 * activate module. If something of that fails retry every minute.
 */
void init_module(void)
{
        struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };
        auto ssize_t n;

        while (1) {
                if ( (fd_cfg = open(MODULE_CFG, O_RDONLY)) < 0 ) {
                        if (cfg_to_proc_error())
                                print_log(LOG_WARNING,
                                       "updatedb must be run first, waiting...");
                        continue;
                } else
                        cfg_to_proc_ok();

                if ( (fd_proc = open(RLOCATEPROC, O_WRONLY, 0)) < 0 ) {
                        if (cfg_to_proc_error())
                                print_log(LOG_WARNING,
                                      "rlocate module is not loaded, waiting...");

                        continue;
                } else
                        cfg_to_proc_ok();

                fl.l_type = F_RDLCK;
                if (fcntl(fd_cfg, F_SETLKW, &fl) < 0) {
                        if (cfg_to_proc_error())
                                print_log(LOG_WARNING,
                                          "fcntl lock error: %s",
                                          strerror(errno) );
                        continue;
                } else
                        cfg_to_proc_ok();

                /* copy module cfg file to proc file. */
                while((n = read(fd_cfg, buf, BUFSIZ)) !=0 ) {
                        if (write(fd_proc, buf, n) != n) {
                                if (cfg_to_proc_error())
                                        print_log(LOG_WARNING,
                                                  "write error %s: %s",
                                                  RLOCATEPROC, strerror(errno) );
                        } else {
                                cfg_to_proc_ok();
                        }
                }
                /* activate module */
                if (write(fd_proc, "activated:1", 12) != 12) {
                        if (cfg_to_proc_error())
                                print_log(LOG_WARNING, "write error %s: %s",
                                          RLOCATEPROC, strerror(errno) );
                        continue;
                } else
                        cfg_to_proc_ok();
                break;
        }
        cfg_to_proc_done();
        /* release lock */
        fl.l_type = F_UNLCK;
        if (fcntl(fd_cfg, F_SETLKW, &fl) < 0)
                print_log(LOG_WARNING, "fcntl unlock error: %s",
                          strerror(errno) );

        if (close(fd_cfg) < 0)
                print_log(LOG_WARNING, "cannot close %s: %s", MODULE_CFG,
                                                              strerror(errno) );
        if (close(fd_proc) < 0)
                print_log(LOG_WARNING, "cannot close %s: %s", RLOCATEPROC,
                                                              strerror(errno) );
}

/*
 * get_diff_db_name() returns a dbname with ".diff" attached.
 */
char *get_diff_db_name(const char *dbname)
{
        int db_len = strlen(dbname);
        char *diff_dbname = xmalloc( db_len + 6 );
        strcpy(diff_dbname, dbname);
        strcpy(diff_dbname + db_len, ".diff");
        return diff_dbname;
}

/*
 * destroy_exclude_dir()
 */
void destroy_exclude_dir()
{
	int i;
	if (EXCLUDE_DIR == NULL)
		return;
	for (i = 0; EXCLUDE_DIR[i]; i++)
		free(EXCLUDE_DIR[i]);

	free(EXCLUDE_DIR);
	EXCLUDE_DIR = NULL;
}

/*
 * create_exclude_dir()
 */
void create_exclude_dir(char *exclude_dir_string)
{
	char *startptr = exclude_dir_string;
	char *endptr;
	int err = 0;
	int len = 0;
	char *token;
	int i = 0;
        if (EXCLUDE_DIR != NULL)
        	destroy_exclude_dir();
	if (exclude_dir_string == NULL || *exclude_dir_string == '\0')
		return;
	// check exclude_dir_string
	if (*exclude_dir_string == '*' && exclude_dir_string[strlen(exclude_dir_string) - 1] == '*') {
		while ((token = index(startptr + 1, '*'))) {
			len++;
			if (*(token + 1) == '\0')
				break;
			if (*(token + 1) != '*') {
				err = 1;
				break;
			}
			startptr = token + 2;
		}
	} else {
		err = 1;
	}
	if (err) {
		print_log(LOG_WARNING,
			  "get_exclude_dir: cannot parse exclude dir\n");
		return;
	}
	// create 2d array with exclude dirs
	startptr = exclude_dir_string;
	EXCLUDE_DIR = (char **)malloc(sizeof(char *) * (len + 1));
	while (1) {
		for (endptr = startptr + 1; *endptr != '*' && *endptr != '\0';
		     endptr++);
		*endptr = '\0';
		/* add '/' to the exclude dir */
		EXCLUDE_DIR[i] =(char *)xmalloc(strlen(startptr) + 1);
		strcpy(EXCLUDE_DIR[i] , startptr + 1);
		strcat(EXCLUDE_DIR[i], "/");
		*endptr = '*';
		startptr = endptr + 1;
		i++;
		if (*startptr == '\0')
			break;
	}
	EXCLUDE_DIR[i] = NULL;
}

/*
 * parse_proc() parse /proc/rlocate and set RLOCATE_DIFF_DB, MODULE_VERSION,
 * STARTING_PATH and EXCLUDE_DIR global variables.
 */
void parse_proc()
{
	FILE *fd_proc;
        char *endstr;
	if ( (fd_proc = fopen(RLOCATEPROC, "r")) != NULL) {
		while ( (fgets(buf, BUFSIZ, fd_proc)) != NULL) {
			if (!strncmp(buf, "version: ", 9)) {
                        	endstr = strstr(buf + 9, "\n");
                        	*endstr = '\0';
                        	/* MODULE_VERSION can be allocated by reload
                         	* so it will be freed before it will be
                         	* allocated again */
                        	if (MODULE_VERSION != NULL) {
                                	free(MODULE_VERSION);
					MODULE_VERSION = NULL;
				}
                        	MODULE_VERSION = xstrdup( buf + 9 );
			} else if (!strncmp(buf, "excludedir: ", 12)) {
                        	endstr = strstr(buf + 12, "\n");
                        	*endstr = '\0';
                        	create_exclude_dir( buf + 12 );
			} else if (!strncmp(buf, "startingpath: ", 14)) {
                        	endstr = strstr(buf + 14, "\n");
                        	*endstr = '\0';
                        	if (STARTING_PATH != NULL) {
                                	free(STARTING_PATH);
					STARTING_PATH = NULL;
				}
                        	STARTING_PATH = xstrdup( buf + 14 );
			} else if (OUTPUT_OPTION == 0 &&
				   !strncmp(buf, "output: ", 8)) {
				// --option has precedence
                        	endstr = strstr(buf + 8, "\n");
                        	*endstr = '\0';
                        	if (RLOCATE_DIFF_DB != NULL) {
                                	free(RLOCATE_DIFF_DB);
					RLOCATE_DIFF_DB = NULL;
				}
                                RLOCATE_DIFF_DB = get_diff_db_name(buf + 8);
                        }
		}
		fclose(fd_proc);
	}
}

/* Check to see if a path matches an excluded one. */
int match_exclude(char *path) {
	int i;
        int starting_path_len;
        int path_len = strlen(path);
	int excl_dir_len;

	/* check path against STARTING_PATH_ARG */
        if (STARTING_PATH != NULL && *STARTING_PATH != '\0') {
		starting_path_len = strlen(STARTING_PATH);
                if (strncmp(STARTING_PATH, path,
			    (starting_path_len - 1 == path_len ?
			     path_len : starting_path_len)))
               		return 1; // this path is not in starting path
	}
	if (!EXCLUDE_DIR)
		return 1;
	for (i = 0; EXCLUDE_DIR[i]; i++) {
		excl_dir_len = strlen(EXCLUDE_DIR[i]);
		if (strncmp(path, EXCLUDE_DIR[i],
			    (excl_dir_len - 1 == path_len ?
			     path_len : excl_dir_len)) == 0)
			return 1;
	}

        return 0;
}

/*
 * traverse_dir() traverse directory 'dirstr' and put the paths, that match
 * the pattern, to the tree.
 */
void traverse_dir(char *dirstr)
{
        FTSENT *file;
        FTS *dir;
        int i;
        char **dir_array;
	int dirstr_len = strlen(dirstr);

        //dirstr[dirstr_len - 1] = '\0'; // remove '\n'
        dir_array = (char **)xmalloc(sizeof(char **)*2);
        *dir_array = dirstr;
        dir_array[1] = NULL;
        dir = fts_open(dir_array, FTS_PHYSICAL|FTS_NOSTAT, NULL);
        for (i = 0; i > -1; i += 1) {
                file = fts_read(dir);

                if (!file)
                        break;

                if (file->fts_info != FTS_DP && file->fts_info != FTS_NS) {
                        if ((EXCLUDE_DIR != NULL &&
                             !match_exclude(file->fts_path)) ||
                            EXCLUDE_DIR == NULL) {
                                // write to the db without leading '/', with
                                // '\n' at the end of the line.
                                strcpy(dirstr, file->fts_path + 1);
                                dirstr_len = strlen(dirstr);
                                if (fwrite(dirstr, strlen(dirstr) + 1, sizeof(char), fd_db) == EOF)
                                	print_log(LOG_WARNING,
                                               	"write error %s: %s",
                                                RLOCATE_DIFF_DB,
                                                strerror(errno) );
                        } else
                                fts_set(dir,file,FTS_SKIP);
                }
        }
        fts_close(dir);
        free(dir_array);
}



/*
 * update_lvm_array()
 *
 * update LVM_MINOR_TO_DEV array with info from lvdisplay output
 */
static void update_lvm_array() {
	FILE *fp;
	char string[PATH_MAX];
	char *p, *pp;
	int kernel_minor;
	fp = popen("lvdisplay -C --separator - --noheadings -o lv_kernel_minor,vg_name,lv_name 2>/dev/null", "r");
	if (fp != NULL) {
		while (fgets(string, PATH_MAX, fp) != NULL) {
			string[strlen(string) - 1] = '\0';
			pp = string;
			p = string;
			while (isspace(*pp)) {pp++;}
			p = pp;
			while (*p != '-') {p++;}
			*p = '\0';
			p++;
			kernel_minor = strtol(pp, (char **)NULL, 10);
			if (LVM_MINOR_TO_DEV[kernel_minor] != NULL) {
				free(LVM_MINOR_TO_DEV[kernel_minor]);
				LVM_MINOR_TO_DEV[kernel_minor] = NULL;
			}
			LVM_MINOR_TO_DEV[kernel_minor] = xstrdup(p);
			if (kernel_minor > LVM_MINOR_TO_DEV_LEN - 1)
				LVM_MINOR_TO_DEV_LEN = kernel_minor + 1;
		}
		if (pclose(fp) == -1) {
		}
	}
}

/*
 * get_lvm_minor()
 *
 * get lvm kernel minor number from device name
 */
static int get_lvm_minor(char *dev_file) {
	int i;
	for (i = 0; i < LVM_MINOR_TO_DEV_LEN; i++) {
		if (LVM_MINOR_TO_DEV[i] == NULL)
			continue;
		if (!strcmp(LVM_MINOR_TO_DEV[i], dev_file))
			return i;
	}
	return -1;
}
/*
 * rlocate_create_mount_hash()
 *
 * create DEV_TO_MOUNTP hash.
 */
void rlocate_create_mount_hash() {
	int i;
	for (i = 0; i<256; i++)
		LVM_MINOR_TO_DEV[i] = NULL;
	LVM_MINOR_TO_DEV_LEN = 0;
        memset( &DEV_TO_MOUNTP, 0, sizeof(DEV_TO_MOUNTP));
        if (hcreate_r(200, &DEV_TO_MOUNTP) == 0)
                print_log(LOG_WARNING, "rlocate_create_mount_hash(): "
				       "hcreate_r: cannot create dev hash\n");
}

/*
 * rlocate_update_mount_hash()
 *
 * update DEV_TO_MOUNTP hash.
 */
void rlocate_update_mount_hash() {
        FILE *fd;
        ENTRY mount_entry, *ret_entry;
        int buffer_len = 1000;
        char buffer[buffer_len];
        char *dev_file;
        char *end_str;
        char *mountpoint, *mp;
	int kernel_minor;
        Mount_list *ml;
        /* read mtab */
	update_lvm_array();
        if ( (fd = fopen("/etc/mtab", "r")) != NULL ) {
                while ( (fgets(buffer, buffer_len, fd)) != NULL ) {
                        if (!strncmp(buffer, "/dev/", 5)) {

                                /* get dev file like hda1 */
                                end_str = strchr(buffer, ' ');
                                *end_str = '\0';
                                dev_file = strdup(buffer + 5);
                                /* get mount point */
                                mp = end_str + 2;
                                end_str = strchr(mp, ' ');
                                *end_str = '\0';
                                mountpoint = (char *)xmalloc(strlen(mp) + 1);
				if (!mountpoint)
					return;
                                strcpy(mountpoint, mp);
				if (!strncmp(dev_file, "mapper/", 7)) {
					kernel_minor = get_lvm_minor(dev_file + 7);
					sprintf(dev_file , "dm-%i", kernel_minor);

				}
                                /* put dev_file and mount point in the hash */
                                mount_entry.key  = dev_file;
                                if( hsearch_r(mount_entry, FIND, &ret_entry,
                                              &DEV_TO_MOUNTP) == 0) {
                                	mount_entry.data = mountpoint;
                                	if( hsearch_r(mount_entry, ENTER,
					    	      &ret_entry,
                                             	      &DEV_TO_MOUNTP) == 0) {
                                        	print_log(LOG_WARNING,
						     "hsearch: hash is full\n");
						break;
					}
                                	/* add mount point to the list */
                                	ml = (Mount_list*)xmalloc(sizeof(Mount_list));
					if (!ml)
						return;
                                	ml->dev_file = dev_file;
                                	if (mount_list_head == NULL)
						ml->next = ml;
					else {
						ml->next=mount_list_head->next;
						mount_list_head->next = ml;
					}
					mount_list_head = ml;
                                } else {
					if ( strcmp(ret_entry->data,
						    mountpoint) ) {
						free(ret_entry->data);
                                		ret_entry->data = mountpoint;
					} else
						free(mountpoint);
					free(dev_file);
				}
                        }
                }
                if (fclose(fd) <0)
                        print_log(LOG_WARNING,
				  "rlocate_update_mount_hash(): fclose:"
				  " can't close /etc/mtab: %s\n",
				  strerror(errno) );
        }

}
/*
 * destroy_lvm_array()
 */
void destroy_lvm_array() {
	int i;
	for (i = 0; i < LVM_MINOR_TO_DEV_LEN; i++) {
		if (LVM_MINOR_TO_DEV[i] != NULL) {
			free(LVM_MINOR_TO_DEV[i]);
			LVM_MINOR_TO_DEV[i] = NULL;
		}
	}
}

/*
 * rlocate_destroy_mount_hash()
 */
void rlocate_destroy_mount_hash() {
	Mount_list *ml, *ml_p;
	char *mountpoint;
        ENTRY mount_entry, *me;
	ml = mount_list_head->next;
	while (1) {
                mount_entry.key = ml->dev_file;
                hsearch_r(mount_entry, FIND, &me, &DEV_TO_MOUNTP);
                if ( me ) {
        		mountpoint = (char *)me->data;
			free(mountpoint);
                }
		free(ml->dev_file);
		if (ml == mount_list_head) {
			free(ml);
			break;
		}
		ml_p = ml;
		ml = ml->next;
		free(ml_p);
	}
        hdestroy_r(&DEV_TO_MOUNTP);
	destroy_lvm_array();
}

/*
 * rlocate_get_mountpoint()
 *
 * convert directory path with dev_file passed in the buffer string.
 */
void rlocate_get_mountpoint(char *buffer) {
        ENTRY mount_entry, *me;
        char *mountpoint;
        char *dirstr_part;
        char *p;

        p = index(buffer, ':');
	if (!p)
		p = buffer + strlen(buffer);
	dirstr_part = strdup(p + 1);
	if (!dirstr_part) {
		print_log(LOG_WARNING, "rlocate_get_mountpoint: strdup: %s",
							strerror(errno));
		*buffer = '\0';
		return;
	}
	*p = '\0';
        mount_entry.key = buffer;
        hsearch_r(mount_entry, FIND, &me, &DEV_TO_MOUNTP);
        if ( !me ) {
		*buffer = '\0';
		free(dirstr_part);
		return;
        }
        mountpoint = (char *)me->data;
	if (!strcmp(mountpoint, ""))
		strcpy(buffer, dirstr_part + 1);
	else {
        	strcpy(buffer, mountpoint);
        	strcat(buffer, dirstr_part);
	}
        free(dirstr_part);
}

/*
 * main
 */
int main(int argc, char **argv)
{
        int ch; /* option character */
        char *p;
        int this_option_optind;
        int option_index;
        struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };
        int no_module; // 0, if kernel module is not loaded. 1, if it is.
        int change_permissions;
        int num_fds;
        auto int fd;
        auto int fd_db_no;
        char *buffer = NULL;
        size_t len = 0;
        ssize_t read;

	char mode;

        struct group *grpres;

        struct option long_options[] =
        {
                {"help",    0, 0, 'h'},
                {"nodaemon",0, 0, 'n'},
                {"version", 0, 0, 'V'},
                {"output",  1, 0, 'o'},
                {"noloop",  0, 0, 'l'},
                {"interval", 1, 0, 'i'},
                {0, 0, 0, 0}
        };
        uid_t uid = getuid();

        PROGNAME = ((p = strrchr(argv[0], '/')) ? p+1 : *argv);
        /* Parse the command-line */
        while (1) {
                this_option_optind = optind ? optind : 1;
                option_index = 0;
                ch = getopt_long (argc, argv, "hVno:i:t:l",
                                  long_options, &option_index);
                if (ch == -1)
                        break;
                switch(ch) {
                        case 'h':
                                usage();
                                break;
                        case 'n':       /* don't run in daemon mode */
                                NO_DAEMON++;
                                break;
                        case 'l':
                                NO_LOOP++;
                                NO_DAEMON++;
                                break;
                        case 'V':
                                printf("%s", RL_VERSION);
                                exit (1);
                                break;
                        case 'o':
                                if (optarg && strlen(optarg)>0) {
                                        OUTPUT_OPTION = 1;
                                        RLOCATE_DIFF_DB=get_diff_db_name(optarg);
                                }
                                break;
                        case 'i':
                                if (optarg && atoi(optarg)>0) {
                                        INTERVAL = atoi(optarg);
                                }
                                break;
                        case 't':
                                if (optarg && atoi(optarg)>0) {
                                        THRESHOLD = atoi(optarg) * MB;
                                }
                                break;

                        default:
                                return(1);
                                break;
                }
        }
        /* open log */
        if (!NO_DAEMON)
                openlog(PROGNAME, LOG_PID, LOG_DAEMON);

        /* set gid and check if rlocate group exists */
        if ((grpres = getgrnam(RLOCATE_GRP)) == NULL) {
                print_log(LOG_WARNING, "Could not find group %s in the "
                                       "/etc/group file.", RLOCATE_GRP);
                print_log(LOG_WARNING, "This is a result of the group missing "
                                       "or a corrupted group file.");
        } else {
                setgid(grpres->gr_gid);
        }

        if (uid != ROOT_UID) {
                print_log(LOG_ERR, "you are not authorized to run this program");
                exit(1);
        }

        /* check the pid file and daemonize */
        if (!NO_DAEMON) {
                if (!check_pid(PidFile)) {
                        if ( fork() == 0 ) {
                                num_fds = getdtablesize();
                                for (fd = 0; fd < num_fds; ++fd) {
                                        close(fd);
                                }
                                setsid();
                        } else
                                exit(0);
                } else {
                        print_log(LOG_ERR, "already running, exiting...");
                        exit(1);
                }
        }
        if (!NO_LOOP) {
                if (!check_pid(PidFile)) {
                        if (!write_pid(PidFile))
                                clean_up();
                } else {
                        print_log(LOG_ERR, "already running, exiting...");
                        clean_up();
                }
                print_log(LOG_INFO, "rlocated daemon started");
                /* Signals */
                for (ch= 1; ch < NSIG; ++ch)
                        signal(ch, SIG_IGN);
                signal(SIGINT, stop_daemon);
                signal(SIGKILL, stop_daemon);
                signal(SIGTERM, stop_daemon);
                signal(SIGHUP, reload_config);
        }

        fl.l_pid = getpid();
        RELOAD_CONFIG = 1;
        /* the main loop */
        no_module = 1;
        change_permissions = 1;
	rlocate_create_mount_hash();
	rlocate_update_mount_hash();
        while (1) {
                if (no_module && !NO_LOOP) {
                        init_module();
                        print_log(LOG_INFO, "rlocate module is initialized");
                }
                no_module = 0;

                if (RELOAD_CONFIG>0) {
                        RELOAD_CONFIG=0;
                        parse_proc();
                        // compare daemon and module versions
                        if (strcmp(MODULE_VERSION, VERSION)) {
                                print_log(LOG_WARNING, "module version (%s) "
                                          "doesn't match daemon version (%s)",
                                          MODULE_VERSION, VERSION);
                        }
                        change_permissions = 1;
                }

                if (!NO_LOOP)
                        sleep(INTERVAL);
                if (get_file_size(RLOCATE_DIFF_DB) > THRESHOLD) {
                    system("/usr/bin/updatedb");
                }

                /* open db file */
                if ( (fd_db = fopen(RLOCATE_DIFF_DB, "a")) == NULL ) {
                        print_log(LOG_WARNING,
				  "cannot open %s for writing: %s",
                                  RLOCATE_DIFF_DB, strerror(errno));
                        if (NO_LOOP) clean_up();
                        continue;
                }
                if (change_permissions>0) {
                        if (chmod(RLOCATE_DIFF_DB, 00640)) {
                                print_log(LOG_WARNING, "cannot chmod %s: %s",
                                             RLOCATE_DIFF_DB, strerror(errno));
                        }
                        change_permissions = 0;
                }
                fl.l_type = F_WRLCK;
                fd_db_no = fileno(fd_db);
                if (fcntl(fd_db_no, F_SETLKW, &fl) < 0) {
                        print_log(LOG_WARNING, "fcntl lock error: %s",
                                  strerror(errno) );
                        RELOAD_CONFIG = 1;
                        if (NO_LOOP) clean_up();
                        continue;
                }

                /* open dev file */
                if ( (fd_dev = fopen(RLOCATEDEV, "r")) == NULL ) {
                        print_log(LOG_WARNING, "cannot open %s: %s",
                                  RLOCATEDEV, strerror(errno));
                        no_module = 1;
                        if (NO_LOOP) clean_up();
                        continue;
                }
                /* copy all lines from dev file to the diff database. The
                 * lines that start with 'm' are directories, that were
                 * moved, so they are traversed and written to the diff
                 * database.
                 */
                while ( (read = getdelim(&buffer, &len, '\0', fd_dev)) != -1 ) {
			if (*buffer == '1') {
				//reload major minor hash
				rlocate_update_mount_hash();
				continue;
			}
			rlocate_get_mountpoint(buffer + 1);
			mode = *buffer;
			*buffer = '/';
                        if ((EXCLUDE_DIR != NULL &&
                             !match_exclude(buffer)) ||
                            EXCLUDE_DIR == NULL) {
                        	if (mode == 'm')
                                	traverse_dir(buffer);
				else if (*(buffer + 1) != '\0') {
                                /* write without leading '/'. Don't write if
				 * partition was unmounted. */
                                	if (fwrite(buffer + 1, strlen(buffer + 1) + 1, sizeof(char), fd_db) == EOF)
                                       		print_log(LOG_WARNING,
                                                 	"write error %s: %s",
                                                 	RLOCATE_DIFF_DB,
                                                  	strerror(errno) );
				}
                        }
                }
		if (buffer)
			free(buffer);
		buffer = 0;
                if (ferror(fd_dev))
                        print_log(LOG_WARNING, "read error %s: %s",
                                  RLOCATEDEV, strerror(errno) );


                if (fclose(fd_dev) < 0)
                        print_log(LOG_WARNING, "cannot close %s: %s",
                                  RLOCATEDEV, strerror(errno) );
                /* release lock */
                fl.l_type = F_UNLCK;

                if (fcntl(fd_db_no, F_SETLKW, &fl) < 0)
                        print_log(LOG_WARNING, "fcntl unlock error: %s",
                                  strerror(errno) );
                if (fclose(fd_db) < 0)
                        print_log(LOG_WARNING, "cannot close %s: %s",
                                  RLOCATE_DIFF_DB, strerror(errno) );
                if (NO_LOOP) break;

                if (COUNTDOWN != 0 ) {
                        if (COUNTDOWN == 1)
                                NO_LOOP = 1;
                        COUNTDOWN--;
                }
        }
        if (STARTING_PATH != NULL)
                free(STARTING_PATH);
        if (RLOCATE_DIFF_DB != NULL)
                free(RLOCATE_DIFF_DB);
        if (OUTPUT != NULL)
                free(OUTPUT);
        if (MODULE_VERSION != NULL)
                free(MODULE_VERSION);
        if (EXCLUDE_DIR != NULL)
        	destroy_exclude_dir();
	rlocate_destroy_mount_hash();
        return 0;
}
