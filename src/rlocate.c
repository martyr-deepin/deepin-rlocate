/*****************************************************************************
 * Real-Time Locate
 *
 * Copyright (c) 2004,2005 Rasto Levrinc
 *
 * Real-Time Locate: http://rlocate.sourceforge.net/
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <fnmatch.h>
//#define __USE_GNU
#include <search.h>
#include <limits.h>
#include <ctype.h>
#include <signal.h>
#include <paths.h>

#include "slocate.h"
#include "rlocate.h"
#include "utils.h"
#include "pidfile.h"
/* GLOBALS */
#define MIN_BLK 4096
#define SLOC_ESC -0x80

#define RLOCATEPROC  PROCDIR"/rlocate"

#define SLOC_UID 0
/* MAX_UPDATEDB_COUNT specifies a count after which a full database update will
 * be performed */
#define MAX_UPDATEDB_COUNT 10;

/* extern variables are defined in slocate.c */
extern int QUIET;
extern int encode(struct g_data_s *g_data, FILE *fd, char *path, struct enc_data_s *enc_data);
extern int get_short(char **fp);
extern char *set_path_head(struct g_data_s *g_data, char *path_head, int code_num, char *prev_code_str);

/* global variables STR, CASESTR, GLOBFLAG and PREG are set in rlocate_init()*/
static char *STR; 
static char *CASESTR;
static int GLOBFLAG;

static char *OUTPUT; 		     /* output database */
static char *STARTINGPATH; /* starting path with mountpoint converted to major
				                               minor number */
static unsigned char UPDATEDB_COUNT; /* this value will be decremented when 
                                        updatedb is run. If it reaches zero full 
                                        update will be run */

static char *PidFile = _PATH_VARRUN "rlocated.pid";

static void *paths_tree_root;          // root of the tree, that contains paths
static char *tmp_output_diff = NULL;   // temp output diff database

static char *PROGNAME;

static int LOCK_FD;
static char *LOCK_FILE;

typedef struct paths_list { // list, that contains paths that were added to  
        char *path;         // the filesystem
        struct paths_list *next;
} Paths_list;

Paths_list *paths_list_current; // pointer to the current path
Paths_list *paths_list_root;    // pointer to the root of the list of paths

/*
 * xmalloc() allocate n bytes with malloc and exit if there is an error.
 */
void *xmalloc(const unsigned n) {
        void *p;
        p = malloc(n);
        if (p) return p;
        fprintf(stderr, "%s: xmalloc: malloc %s\n", PROGNAME, strerror(errno));
        exit(1);
}

/*
 * xstrdup() return copy of string and exit if there is an error.
 */
void *xstrdup(const char *s) {
	void *p;
	p = strdup(s);
	if (p) return p;
	fprintf(stderr, "%s: xstrdup: %s\n", PROGNAME, strerror(errno));
	exit(1);
}

/*
 * write_to_fd writes() string to the fd file descriptor. fd must be open.
 */
void write_to_fd(const int fd, const char *filename, const char *string ) {
        int len = strlen(string);
        if (write(fd, string, len) != len)
                fprintf(stderr, "%s: write_to_fd: write error %s: %s\n", 
				PROGNAME, filename, strerror(errno) );
}

/*
 * get_diff_db_name() returns a dbname with ".diff" attached.
 */

char* get_diff_db_name(const char *dbname)
{
        int db_len = strlen(dbname);
        char *diff_dbname = (char *)xmalloc(db_len + 6 );
        strcpy(diff_dbname, dbname);
        strcpy(diff_dbname + db_len, ".diff");
        return diff_dbname;
}

/*
 * get_tmp_db_name() returns a dbname with ".tmp" attached.
 */
char *get_tmp_db_name(const char *dbname)
{
        int db_len = strlen(dbname);
        char *tmp_dbname = (char *)xmalloc(db_len + 5);
        strcpy(tmp_dbname, dbname);
        strcpy(tmp_dbname + db_len, ".tmp");
        return tmp_dbname;
}

/*
 * run_rlocated() run rlocated with --noloop option if user is root.
 */
void run_rlocated(struct g_data_s *g_data)
{
        pid_t pid;

        if (g_data->uid == SLOC_UID) {
                if ( (pid = fork()) < 0 )
                        fprintf(stderr, "%s: run_rlocated: fork error\n", 
				PROGNAME);
                else if (pid == 0) {
                        if (execlp(RLOCATED_CMD, "rlocated", "--noloop", NULL)<0) 
                                fprintf(stderr, 
					"%s: run_rlocated: execlp error\n", 
					PROGNAME);
                }
                if (waitpid(pid, NULL, 0) < 0)
                        fprintf(stderr, "%s: run_rlocated: waitpid error\n",
					PROGNAME);
        }
}

/*
 * get_exclude_dir_string()
 */
char *get_exclude_dir_string(char **exclude_dir) {
	int string_len = 0;
	int i = 0;
	char *exclude_dir_string;
	for (i = 0; exclude_dir[i]; i++)
		string_len += 2 + strlen(exclude_dir[i]);

	exclude_dir_string = (char *)xmalloc(string_len + 1);
	*exclude_dir_string = '\0';
	i = 0;
	strcat(exclude_dir_string, "*");
	for (i = 0; exclude_dir[i]; i++) {
		if (i > 0)
			strcat(exclude_dir_string, "**");
		strcat(exclude_dir_string, exclude_dir[i]);
	}
	strcat(exclude_dir_string, "*");
	return exclude_dir_string;
}

/*
 * generate_module_cfg() genarates module.cfg file, that is used to initialize
 * /proc/rlocate after reboot.
 */
void generate_module_cfg(struct g_data_s *g_data) {
        int fd_cfg;
        char updatedb_count_str[20];
	char *exclude_dir_string;

        sprintf(updatedb_count_str, "\nupdatedb:%i\n", UPDATEDB_COUNT);
        if ( (fd_cfg = open(MODULE_CFG,O_WRONLY|O_CREAT|O_TRUNC, 00600))<0 ) {
                fprintf(stderr, "%s: generate_module_cfg: open: could not "
				"open %s: %s\n", PROGNAME, MODULE_CFG, 
				strerror(errno));
        } else {
                write_to_fd(fd_cfg, MODULE_CFG, 
			    "# This file is automatically generated -- "
			    "please do not edit.\nexcludedir:");
                if (g_data->exclude != NULL) {
			exclude_dir_string = 
				get_exclude_dir_string(g_data->exclude);
                        write_to_fd(fd_cfg, MODULE_CFG, exclude_dir_string);
			free(exclude_dir_string);
		}
                write_to_fd(fd_cfg, MODULE_CFG, "\nstartingpath:");
                if (STARTINGPATH)
                        write_to_fd(fd_cfg, MODULE_CFG, STARTINGPATH);
                write_to_fd(fd_cfg, MODULE_CFG, "\noutput:");
                if (g_data->output_db)
                        write_to_fd(fd_cfg, MODULE_CFG, g_data->output_db);
                write_to_fd(fd_cfg, MODULE_CFG, updatedb_count_str);
                if ( close(fd_cfg) < 0 )
                        fprintf(stderr, "%s: generate_module_cfg: close: "
                                      	"can't close %s: %s\n", 
                                      	PROGNAME, MODULE_CFG, strerror(errno) );
        }
}

/* 
 * update_proc_info() update module /proc/rlocate info 
 */
int update_proc_info(struct g_data_s *g_data) {
        int fd_db;
	char *exclude_dir_string;
        if ( (fd_db = open(RLOCATEPROC, O_WRONLY, 0)) < 0 ) {
                fprintf(stderr, "%s: update_proc_info: cannot open "RLOCATEPROC
			      	" for writing: %s. Module not loaded?\n",
                              	PROGNAME, strerror(errno));
                return 1;
        }
        if (g_data->exclude) {
		exclude_dir_string = get_exclude_dir_string(g_data->exclude);
                write_to_fd(fd_db, RLOCATEPROC, "excludedir:");
                write_to_fd(fd_db, RLOCATEPROC, exclude_dir_string);
		free(exclude_dir_string);
        }
        if (STARTINGPATH != NULL) {
                write_to_fd(fd_db, RLOCATEPROC, "\nstartingpath:");
                write_to_fd(fd_db, RLOCATEPROC, STARTINGPATH);
        }
        if (OUTPUT != NULL) {
                write_to_fd(fd_db, RLOCATEPROC, "\noutput:");
                write_to_fd(fd_db, RLOCATEPROC, OUTPUT);
        }
        write_to_fd(fd_db, RLOCATEPROC, "\nactivated: 1");
        if ( close(fd_db) < 0 )
                fprintf(stderr, "%s: update_proc_info: close: can't close "
			      	RLOCATEPROC": %s\n", PROGNAME, strerror(errno));
        return 0;
}

/*
 * write_updatedb_count() writes UPDATEDB_COUNT to the /proc/rlocate
 */
void write_updatedb_count() {
        char updatedb_count_str[20];
        int fd_db;

        sprintf(updatedb_count_str, "\nupdatedb:%i\n", UPDATEDB_COUNT);
        if ( (fd_db = open(RLOCATEPROC, O_WRONLY, 0)) < 0 ) {
                fprintf(stderr, "%s: write_updatedb_count: cannot open "
				RLOCATEPROC" :%s, module not loaded?\n", 
				PROGNAME, strerror(errno));
        } else {
                write_to_fd(fd_db, RLOCATEPROC, updatedb_count_str);
                if ( close(fd_db) < 0 )
                        fprintf(stderr, "%s: write_updatedb_count: close: "
					"can't close "RLOCATEPROC": %s\n", 
				      	PROGNAME, strerror(errno) );
        }
}

/*
 * set_updatedb_count() parses /proc/rlocate and sets UPDATEDB_COUNT
 */
void set_updatedb_count(struct g_data_s *g_data) {
        char fbuf[14];
        char **endptr = NULL;
        UPDATEDB_COUNT = 0;
	FILE *fd_proc;
        
        /* read updatedb_count decrement it if is greater than 0, 
         * set FULL_UPDATE to 1 if --fast-update option was not
         * specified. */
	if ( (fd_proc = fopen(RLOCATEPROC, "r")) != NULL) {
		while ( (fgets(fbuf, 14, fd_proc)) != NULL) {
			if (!strncmp(fbuf, "updatedb: ", 10)) {
                		UPDATEDB_COUNT = (unsigned char)strtol(fbuf+10, endptr, 10);
			}
		}
		fclose(fd_proc);
	}
        if (UPDATEDB_COUNT > 0) 
                UPDATEDB_COUNT--;
        else {
                if (!g_data->FAST_UPDATE)
                        g_data->FULL_UPDATE = 1;
        }
}



/*
 * rlocate_ftscompare()
 */
int rlocate_ftscompare(const FTSENT **e1, const FTSENT **e2) {
        return strcmp((*e1)->fts_accpath, (*e2)->fts_accpath);
}

/*
 * get_lock_name()
 *
 * Return name of the lock file.
 */
char* get_lock_name(const char *dbname)
{
        int db_len = strlen(dbname);
        char *lock_name = (char *)xmalloc(db_len + 6 );
        strcpy(lock_name, dbname);
        strcpy(lock_name + db_len, ".lock");
        return lock_name;
}

/*
 * rlocate_lock()
 *
 * Create lock file, one per database, if it does not exist and lock it.
 * If lock already exists or in case of error return 0.
 */
int rlocate_lock(struct g_data_s *g_data)
{
	struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };

	PROGNAME = g_data->progname;
        LOCK_FILE = get_lock_name(g_data->output_db);

        fl.l_pid = getpid();
        if ( (LOCK_FD = open(LOCK_FILE, O_CREAT|O_WRONLY)) < 0 ) {
		printf("error: cannot open lock file: %s: %s\n", 
			LOCK_FILE, strerror(errno));
		LOCK_FD = -1;
		return 0;
	}
	if (fcntl(LOCK_FD, F_SETLK, &fl) == -1) {
		fprintf(stderr, "%s: rlocate_lock: updatedb already running" 
				" on %s: %s\n", PROGNAME, LOCK_FILE, 
						strerror(errno));
		if (close(LOCK_FD) < 0 )
			fprintf(stderr, "%s: rlocate_lock: close: "
					"cannot close %s: %s\n", 
					PROGNAME, LOCK_FILE,
					strerror(errno) );
		LOCK_FD = -1;
		return 0;
	} else
		return 1; // got lock
}

/*
 * rlocate_unlock()
 *
 * Unlock, close and unlink lock file.
 */
void rlocate_unlock()
{
	struct flock fl = { F_UNLCK, SEEK_SET, 0, 0, 0 };
	if (LOCK_FD != -1) {
        	fl.l_pid = getpid();
		if (fcntl(LOCK_FD, F_SETLK, &fl) == -1)
			fprintf(stderr, "%s: rlocate_unlock: unlock failed" 
					" on %s: %s\n", 
					PROGNAME, LOCK_FILE, strerror(errno));
		if (close(LOCK_FD) < 0 )
			fprintf(stderr, "%s: rlocate_unlock: close: "
					"cannot close %s: %s\n", PROGNAME, 
					LOCK_FILE, strerror(errno) );
                if (unlink(LOCK_FILE) < 0)
                        fprintf(stderr, "%s: rlocate_unlock: unlink: "
                                      	"cannot unlink %s: %s\n", PROGNAME,
				      	LOCK_FILE, strerror(errno) );
		LOCK_FD = -1;
	}
	free(LOCK_FILE);
}

/*
 * rlocate_start_updatedb() is called before original updatedb creates its 
 * database. It moves the rlocate diff database to temp rlocate diff db.
 */
void rlocate_start_updatedb(struct g_data_s *g_data) {
        int fd_db;
        char *output_diff;
        struct flock fl = { F_WRLCK, SEEK_SET, 0, 0, 0 };
	char *output = g_data->output_db;
	pid_t daemon_pid;
	STARTINGPATH = g_data->index_path;
	OUTPUT = make_absolute_path(g_data, output);
	
	// initialize diff db always when default database is created.
	if (!strcmp(g_data->output_db, DEFAULT_DB))
		g_data->INITDIFFDB = 1;

        tmp_output_diff = NULL;
        // update proc and module.cfg only for user root and if INITDIFFDB is 1 
        if (g_data->uid != SLOC_UID || g_data->INITDIFFDB == 0) {
                return;
        }

	/* update module /proc info */
        if (update_proc_info(g_data)) {
                return;
        }
        /* reload rlocated daemon */
        daemon_pid = read_pid(PidFile);
        if (daemon_pid>0) 
        	kill(daemon_pid, 1);

        set_updatedb_count(g_data);

        /*rename rlocate diff db to temp rlocate diff db. If it's locked wait.*/
        output_diff = get_diff_db_name(output);

        if ( (fd_db = open(output_diff, O_WRONLY)) < 0 ) {
	        if (g_data->VERBOSE)
		        fprintf(stderr, "%s: rlocate_start_updatedb: open: "
					"could not open rlocate diff "
					"database: %s: %s\n", PROGNAME,
				      output_diff, strerror(errno));
	} else {
                fl.l_pid = getpid();
                if (fcntl(fd_db, F_SETLKW, &fl) < 0) {
	                fprintf(stderr, "%s: rlocate_start_updatedb: fcntl: "
					"can't get a lock on %s: %s\n", 
				      	PROGNAME, output_diff, strerror(errno));
                } else {
                        tmp_output_diff = get_tmp_db_name(output_diff);
	                /* rename releases the lock */
	                if ( rename(output_diff, tmp_output_diff) < 0 ) {
	                        fprintf(stderr, "%s: rlocate_start_updatedb: "
						"rename: can't rename to "
						"%s: %s\n", PROGNAME,
					      		    tmp_output_diff, 
							    strerror(errno));
                                free(tmp_output_diff);
	                        tmp_output_diff = NULL;
                        }
                }
                if ( close(fd_db) < 0 )
                        fprintf(stderr, "%s: rlocate_start_updatedb: close: "
					"can't close %s: %s\n", PROGNAME, 
					output_diff, strerror(errno) );
        }
        free(output_diff);
}

/*
 * rlocate_end_updatedb() is called after original updatedb creates its 
 * diff database. It removes the temp rlocate diff database.
 */
void rlocate_end_updatedb(struct g_data_s *g_data) {
        pid_t daemon_pid;
        /* remove temp rlocate diff database if it was created */
        if (tmp_output_diff != NULL) {
                if (unlink(tmp_output_diff) < 0)
                        fprintf(stderr, "%s: rlocate_end_updatedb: unlink: "
                                      	"can't unlink %s: %s\n", PROGNAME,
				      	tmp_output_diff, strerror(errno) );
                free(tmp_output_diff);
        }
        if (g_data->uid == SLOC_UID && g_data->INITDIFFDB == 1) {
                if (g_data->FULL_UPDATE) 
                        UPDATEDB_COUNT = MAX_UPDATEDB_COUNT; 
                /* write updated updatedb_count to the /proc/rlocate */
                write_updatedb_count();

                /* generate module config */
                generate_module_cfg(g_data);

                /* reload rlocated daemon */
                daemon_pid = read_pid(PidFile);
                if (daemon_pid>0) 
                        kill(daemon_pid, 1);

                free(OUTPUT);
        }
}

/*
 * path_strcmp() compares two paths like strings except '/', which will come
 * before any other character, so that sort result is the same as from fts.
 */
int path_strcmp(const char *string1, const char *string2)
{
        register signed char res;
        while (1) {
                if ((res = *string1 - *string2) != 0 || !*string1) {
                        if (*string1==*string2)
                                break;
                        if (*string1=='/' && *string2)
                                res = -1;
                        else if (*string2=='/' && *string1)
                                res = 1;
                        break;
                }
                string1++;
                string2++;
        }
        return res;
}


/*
 * path_compare() compares two paths
 */
int path_compare(const void *string1, const void *string2)
{
        return path_strcmp((const char *) string1,
                        (const char *) string2);
}

/*
 * make_path() make copy of the path and add leading '/'.
 */
char *make_path(const char *path)
{
        char *pathcopy;
        pathcopy = (char *)xmalloc(strlen(path)+2);
        strcpy(pathcopy+1, path);
        *pathcopy = '/';
        return pathcopy;
}
/*
 * check_path() returns 1 if the path matches the pattern. Path is
 * without leading '/'
 */
int check_path(struct g_data_s *g_data, const char *path)
{
        int foundit = 0;
        //char *casecodedpath = NULL;
        int nmatch = 32;
        regmatch_t pmatch[32];
        //char *cp = NULL;
        char *codedpath;
        codedpath = make_path(path);
	if (g_data->regexp_data) {
	        foundit = !regexec(g_data->regexp_data->preg ,codedpath,nmatch,pmatch,0);
	} else if (g_data->nocase) {
#ifdef FNM_CASEFOLD /* i suppose i also have strcasestr */
	        if (GLOBFLAG)
		        foundit =! fnmatch(STR,codedpath,FNM_CASEFOLD);
		else
		        foundit = (strcasestr(codedpath,STR) != NULL);
#else /* FNM_CASEFOLD */
                casecodedpath = xstrdup(codedpath);

		for (cp = casecodedpath; *cp; cp++)
		        *cp = tolower(*cp);
			
		if (GLOBFLAG)
		        foundit =! fnmatch(CASESTR,casecodedpath,0);
		else
		        foundit = (strstr(casecodedpath,CASESTR) != NULL);

		free(casecodedpath);
#endif /* FNM_CASEFOLD */
	
	} else {
		if (GLOBFLAG)
	                foundit =! fnmatch(STR,codedpath,0);
		else
		        foundit=(strstr(codedpath,STR) != NULL);			
        }
        free(codedpath);
        return foundit;
}

/* 
 * print_path() checks if path is accesible and prints it, if it is.
 */
void print_path(struct g_data_s *g_data, const char *path)
{
        char *pathcopy = make_path(path);
	if ( verify_access(pathcopy)) { 
		if (g_data->queries > 0)
			g_data->queries--;
        	printf("/%s\n", path);
	}
	free(pathcopy);
}

/* 
 * create_paths_list() is called from twalk and it creates the list of
 * paths, that were added to the filesystem.
 */
void create_paths_list(void *node, VISIT order, int level) 
{
        Paths_list *f;
        if (order == postorder || order == leaf) {
                f = (Paths_list*)malloc(sizeof(Paths_list));
        	if (!f) {
        		fprintf(stderr, "%s: create_paths_list: malloc: %s\n", 
					PROGNAME, strerror(errno));
			exit(1);
		}
                f->path = *(char **)node;
                f->next = NULL;
                if (paths_list_current == NULL)
                        paths_list_root = f;
                else 
                        paths_list_current->next = f;
                paths_list_current = f;
        }
}


/*
 * store_path() adds the file names that were added to the filesystem to the
 * tree, if they match the pattern.
 */
void store_path(struct g_data_s *g_data, const char *path)
{
        char *pathcopy;
        void *node;
        if (! check_path(g_data, path))
                return;
        /* put the path to the tree of added file names. */
        pathcopy = xstrdup(path);
        node = tsearch((void *)pathcopy, &paths_tree_root, 
                               path_compare);
        if (node == NULL) {
                fprintf(stderr, 
			"%s: store_path: tsearch: insufficient memory\n", 
			PROGNAME);
        }
        // free pathcopy if the path was in the tree before
        if (*(char**)node != pathcopy) {
                free(pathcopy);
        }
}

/*
 * rlocate_init() is called from original locate. It reads both rlocate and
 * temp rlocate diff databases and creates the list of paths.
 *  
 */
void rlocate_init(struct g_data_s* g_data, const char *rlocate_db, char *str, 
		  char *casestr, const int globflag)
{
        FILE *fd;
	//char buffer[PATH_MAX+1];
	char *buffer = NULL;
	size_t len = 0;

        char *rlocate_diff_db;
        char *tmp_rlocate_diff_db;
	PROGNAME = g_data->progname;
        STR      = str;
        CASESTR  = casestr;
        GLOBFLAG = globflag;
        //PREG     = preg;

        rlocate_diff_db     = get_diff_db_name(rlocate_db);
        tmp_rlocate_diff_db = get_tmp_db_name(rlocate_diff_db);

	paths_tree_root     = NULL;
	paths_list_current  = NULL; 
	paths_list_root     = NULL; 

        /* start rlocated once if the user is root, so that the database is 
         * up-to-date */
        run_rlocated(g_data);
        /* open and read rlocate diff database */
        if ( (fd = fopen(tmp_rlocate_diff_db, "r")) != NULL ) {
                while ( (getdelim(&buffer, &len, '\0', fd)) != -1 ) {
                        store_path(g_data, buffer);
                }
		if (buffer)
			free(buffer);
		buffer = NULL;
                if (fclose(fd) <0)
                        fprintf(stderr, "%s: rlocate_init: fclose: can't close"
				        " %s: %s\n", PROGNAME, 
					             tmp_rlocate_diff_db,
						     strerror(errno) );
        }
        if ( (fd = fopen(rlocate_diff_db, "r")) != NULL ) {
                while ( (getdelim(&buffer, &len, '\0', fd)) != -1 ) {
                        store_path(g_data, buffer);
                }
		if (buffer)
			free(buffer);

                if (fclose(fd) <0)
                        fprintf(stderr, "%s: rlocate_init: fclose: can't close "
				        "%s: %s\n", PROGNAME, 
					            rlocate_diff_db,
						    strerror(errno) );
        }
        free(rlocate_diff_db);
        free(tmp_rlocate_diff_db);
        twalk(paths_tree_root, (void*)create_paths_list); 
}

/*
 * rlocate_printit() is called from original locate, when it wants to print
 * the path that was found in its database.
 */
void rlocate_printit(struct g_data_s *g_data, const char *codedpath) 
{
        Paths_list *f;
        int str_ret;

	if (g_data->queries == 0)
		return;
	/* print all paths from the list, that are alphabetically before the 
         * codedpath */
        while (paths_list_root != NULL && 
               (str_ret = strcmp(paths_list_root->path, codedpath + 1)) <=0) {
                print_path(g_data, paths_list_root->path);
		if (g_data->queries == 0)
			return;


                f = paths_list_root;
                paths_list_root = paths_list_root->next;
                free(f); /* the path string will be freed, when the tree is 
                            destroyed */
        }
	/* print coded path, if it is not in the tree of added paths */
        if (tfind((void *)codedpath+1, &paths_tree_root, 
                path_compare) == NULL) { // ignore leading '/' in codedpath 
	                print_path(g_data, codedpath + 1);
        }
}

/*
 * free_string() is called from tdestroy
 */
void free_string(void *path)
{
        free((char **)path);
}

/*
 * rlocate_done() is called from original locate. It prints the rest of the 
 * paths in the list and cleans up the memory.
 */
void rlocate_done(struct g_data_s *g_data)
{
        Paths_list *f;
        // print the rest of the paths
        while ( paths_list_root != NULL ) {
		if (g_data->queries == 0)
			return;
                print_path(g_data, paths_list_root->path);

                f = paths_list_root;
                paths_list_root = paths_list_root->next;
                free(f);
        }
        tdestroy(paths_tree_root, free_string);
}

/*
 * rlocate_updatedb_writeit() is called from rlocate_fast_updatedb() and it 
 * writes one path at a time coded with encode to the tmp database.
 */
void rlocate_fast_updatedb_writeit(struct g_data_s *g_data, const char *codedpath, FILE *fd_tmp, struct enc_data_s *enc_data) 
{
        Paths_list *f;
        int str_ret;
        char *path;
	/* write all paths from the list, that are alphabetically before the 
         * codedpath */
        while (paths_list_root != NULL && 
               (str_ret = strcmp(paths_list_root->path, codedpath+1)) <=0) {
                path = make_path(paths_list_root->path); // add leading '/'
                //encode(fd_tmp, path, "");
                encode(g_data, fd_tmp, path, enc_data);
                free(path);
                f = paths_list_root;
                paths_list_root = paths_list_root->next;
                free(f); /* the path string will be freed, when the tree is 
                            destroyed */
        }
	/* write coded path, if it is not in the tree of added paths */
        if (tfind((void *)codedpath+1, &paths_tree_root, 
                path_compare) == NULL) { // ignore leading '/' in codedpath 
                        encode(g_data, fd_tmp, (char *)codedpath, enc_data);
        }
}


/* 
 * rlocate_fast_updatedb() fast updatedb will be performed everytime except
 * when UPDATEDB_COUNT reaches zero. In that case a full update of the database
 * will be performed in the main.c. Full update will be also performed if
 * eather FULL_UPDATE is set to 1, or the database doesn't exist or user (not
 * default) database is created.
 *
 * Fast update encodes the database mixes it with diff database of added files
 * and stores it back decoded to the rlocate database. Since everytime it's
 * done the rlocate database will contain more and more file names, that were 
 * removed from the file system a full database update is needed after one week
 * or so.
 */
int rlocate_fast_updatedb(struct g_data_s *g_data, FILE *fd_tmp, struct enc_data_s *enc_data)
{
	int fd = -1;
	char ch[1];
	int buf_len;
	signed char buffer[BLOCK_SIZE];
	int ret = 0;
	int code_num = 0;
	char *path_head = NULL;
	char *prev_code_str = NULL;
	char *full_path = NULL;
	char *code_str = NULL;
	int b = 0;
	int b_mark;
	int STATE = DC_CODE;
	int size = 0;
	struct stat db_stat;
	Paths_list *f;
	char *path;
	char *database = g_data->output_db;
	if (g_data->FULL_UPDATE)
		return 0;
        // fast db update only for user root and if INITDIFFDB is 1 
	if (g_data->uid != SLOC_UID || g_data->INITDIFFDB == 0) 
		return 0;
	if (stat(database, &db_stat) == -1)
		return 0;
	if ((fd = open(database,O_RDONLY)) == -1)
		return 0;

	/* slevel */
	buf_len = read(fd, ch, 1);

	g_data->slevel = ch[0];
	b = 0;
	buf_len = read(fd, buffer, BLOCK_SIZE);
	rlocate_init(g_data, database, "", "", 0);
	while (buf_len > 0) {
		code_num = buffer[b];
		/* Escape char, read extra byte */
		if (code_num == SLOC_ESC) {
			b += 1;
			if (b == buf_len) {
				//printf("I 1 - %d\n", BLOCK_SIZE);
				//exit(0);
				buf_len = read(fd, buffer, BLOCK_SIZE);
				b = 0;				
			}
			/* A DC_ESC character indicates that we must read in two bytes
			 * for our code_num due to a long path. */
			code_num = buffer[b];
			b += 1;			
			if (b == buf_len) {
				buf_len = read(fd, buffer, BLOCK_SIZE);
				b = 0;
			}
			code_num = (code_num << 8) | (buffer[b] & 0xff);
		}

		/* Data */
		b += 1;
		if (b == buf_len) {			
			buf_len = read(fd, buffer, BLOCK_SIZE);
			b = 0;			    
		}		
		/* If we are not resuming from an interrupted state then call
		 * this function to construct the beginning of the path to
		 * search */
		if (!(path_head = set_path_head(g_data, path_head, code_num, prev_code_str)) && code_num != 0)
		    goto EXIT;

		/* Mark the current location in the buffer so we can parse out
		 * the string data for the path */
		while(1) {
			b_mark = b;
			while (buffer[b] != '\0' && b < buf_len)
			    b += 1;			
			/* If we are not resuming an interruption then initialize the
			 * code_str variable */
			if (STATE != DC_DATA_INTR) {				
				if (!(code_str = malloc(sizeof(char) * (b-b_mark+1)))) {
					if (!report_error(g_data, FATAL, "search_db: code_str: malloc: %s\n", strerror(errno)))
					    goto EXIT;
				}
				code_str[0] = 0;
			} else {
				size = strlen(code_str)+(b-b_mark)+1;
				if (size < 0)
				    size = 0;
				if (!(code_str = realloc(code_str, sizeof(char) * size))) {
					if (!report_error(g_data, FATAL, "search_db: code_str: realloc: %s\n", strerror(errno)))
					    goto EXIT;
				}
				code_str[size-1] = 0;
				STATE = DC_NONE;
			}

			/* recast, buffer doesn't need to be signed here. */
			code_str = strncat(code_str, ((char *)buffer)+b_mark, b-b_mark);
			/* If we ran into the end of the current buffer, set the state
			 * to DC_DATA_INTR and exit so we can read more data and
			 * return to this state */
			if (b == buf_len && buffer[b-1] != '\0') {
				buf_len = read(fd, buffer, BLOCK_SIZE);
				b = 0;
				STATE = DC_DATA_INTR;
			} else
			    break;
		}
		
		if (!path_head) {
			if (!(full_path = strdup(code_str))) {
				if (!report_error(g_data, FATAL, "search_db: full_path: strdup: %s\n", strerror(errno)))
				    goto EXIT;
			}
		} else {
			if (!(full_path = malloc(sizeof(char) * (strlen(path_head)+strlen(code_str)+1)))) {
				if (!report_error(g_data, FATAL, "search_db: full_path: malloc: %s\n", strerror(errno)))
				    goto EXIT;
			}
			strcpy(full_path, path_head);
			strcat(full_path, code_str);
		}		
		if (prev_code_str) {
			free(prev_code_str);
			prev_code_str = NULL;
		}
				
		/* Save a pointer to the current path, we need it for the next
		 * path we decode */		
		if (!(prev_code_str = strdup(code_str))) {
			if (!report_error(g_data, FATAL, "search_db: prev_code_str: strdup: %s\n", strerror(errno)))			    
			    goto EXIT;
		}		
		if (code_str) {
			free(code_str);
			code_str = NULL;
		}				
		
		/* Search the current path string */
		// if (!search_path(g_data, full_path, search_str, globflag))
		//     goto EXIT;		

		// if (g_data->queries == 0)
		//     break;
		rlocate_fast_updatedb_writeit(g_data, full_path, fd_tmp, enc_data);
		if (full_path) {
			free(full_path);
			full_path = NULL;
		}
		
		b += 1;
		if (b == buf_len) {
			buf_len = read(fd, buffer, BLOCK_SIZE);
			b = 0;
		}
	}

	if (buf_len == -1) {
		if (!report_error(g_data, FATAL, "search_db: read: '%s': %s\n", database, strerror(errno)))
		    goto EXIT;
	}
	
	ret = 1;
EXIT:
	// write the rest of the paths coded with frcode to the tmp database
	while ( paths_list_root != NULL ) {
		path = make_path(paths_list_root->path); // add leading '/'
		encode(g_data, fd_tmp, path, enc_data);
		free(path);
		f = paths_list_root;
		paths_list_root = paths_list_root->next;
		free(f);
	}
	tdestroy(paths_tree_root, free_string);
	if (fd > -1)
	    close(fd);
	if (full_path) {
		free(full_path);
		full_path = NULL;
	}	
	if (code_str) {
		free(code_str);
		code_str = NULL;
	}
	if (prev_code_str) {
		free(prev_code_str);
		prev_code_str = NULL;
	}
	if (path_head) {
		free(path_head);
		path_head = NULL;
	}

	return ret;
}
