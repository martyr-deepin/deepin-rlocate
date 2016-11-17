/*****************************************************************************
 * Secure Locate v3.1                                                        *
 * Author: Kevin Lindsay                                                     *
 * Copyright (c) 2005, 2006 Kevin Lindsay                                    *
 *                                                                           *
 * v3.0 was a complete redesign and rewrite.                                 *
 *									     *
 * Secure Locate: http://slocate.trakker.ca/                                 *
 *                                                                           *
 * Report any Bugs to: slocate@trakker.ca                                    *
 *                                                                           *
 *****************************************************************************/

/*****************************************************************************
 *                                                                            
 * Secure Locate -- search database for filenames that match patterns without 
 *                  showing files that the user using slocate does not have   
 *                  access to.                                                
 *                                                                            
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <fts.h>

/* Local includes */
#include "slocate.h"
#include "utils.h"
#include "cmds.h"
#include "conf.h"
#include "rlocate.h"

/* Init Input DB variable */
char **init_input_db(struct g_data_s *g_data, int len)
{
	int i = 0;
	int cur_len = 0;
	int ret = 0;
	
	if (len < 0) {
		if (!report_error(g_data, FATAL, "init_input_db: Initialization length must be 0 or greater.\n"))
		    goto EXIT;
	}
	if (!g_data->input_db) {
		if (!(g_data->input_db = malloc(sizeof(char *)*(len+1)))) {
			if (!report_error(g_data, FATAL, "init_input_db: malloc: %s\n", strerror(errno)))
			    goto EXIT;
		}
	} else {
		cur_len = 0;
		while (g_data->input_db[cur_len])
		    cur_len += 1;

		if (!(g_data->input_db = realloc(g_data->input_db, sizeof(char *)*(cur_len+len+1)))) {
			if (!report_error(g_data, FATAL, "init_input_db: realloc: %s\n", strerror(errno)))
			    goto EXIT;
		}
	}

	/* Initialize new space */
	for (i = cur_len; i <= cur_len+len; i += 1)
	    g_data->input_db[i] = NULL;

	ret = 1;
EXIT:
	if (!ret && g_data->input_db) {
		free(g_data->input_db);
		g_data->input_db = NULL;
	}
	
	return g_data->input_db;
}

/* Free global data */
void free_global_data(struct g_data_s * g_data)
{
	int i = 0;
	
	if (!g_data)
	    return;
	if (g_data->progname)
	    free(g_data->progname);
	if (g_data->index_path)
	    free(g_data->index_path);
	if (g_data->output_db)
	    free(g_data->output_db);
	if (g_data->input_db) {
		for (i = 0; g_data->input_db[i]; i += 1)
		    free(g_data->input_db[i]);

		free(g_data->input_db);
	}	
	if (g_data->exclude) {
		for (i = 0; g_data->exclude[i]; i++)
		    free(g_data->exclude[i]);

		free(g_data->exclude);
	}
	if (g_data->regexp_data) {
		if (g_data->regexp_data->pattern)
		    free(g_data->regexp_data->pattern);
		if (g_data->regexp_data->preg) {
			regfree(g_data->regexp_data->preg); 
			free(g_data->regexp_data->preg);
		}
		free(g_data->regexp_data);
	}
	free(g_data);
	
	return;
}

/** For defaults see init_global_data() function below **/

/* Initialize global data */
struct g_data_s * init_global_data(char **argv)
{
	struct g_data_s *g_data = NULL;
	char *progname = NULL;
	char *ptr = NULL;
	int ret = 0;

	/* Get the name of the program as it was executed */
	progname = ((ptr = strrchr(argv[0],'/')) ? ptr+1 : *argv);

	if (!(g_data = malloc(sizeof(struct g_data_s)))) {
		fprintf(stderr, "%s: fatal: init_global_data: malloc: %s\n", progname, strerror(errno));
		goto EXIT;
	}

	/* Global Data Defaults */
	if (!(g_data->progname = strdup(progname))) {
		fprintf(stderr, "%s: fatal: init_global_data: strdup: %s\n", progname, strerror(errno));
		goto EXIT;
	}

	g_data->QUIET = FALSE;
	g_data->VERBOSE = FALSE;
	g_data->slevel = VERIFY_ACCESS;
	g_data->nocase = 0;
	g_data->index_path = strdup("/");
	g_data->uid = getuid();
	g_data->gid = getgid();
	g_data->input_db = NULL;
	g_data->output_db = NULL;	
	g_data->exclude = NULL;
	g_data->regexp_data = NULL;
	g_data->queries = -1;
	g_data->SLOCATE_GID = get_gid(g_data, DB_GROUP, &ret);
	g_data->FULL_UPDATE = 0;
	g_data->FAST_UPDATE = 0;
	g_data->INITDIFFDB  = 0;

	if (!ret)
	    goto EXIT;	

EXIT:
	if (!ret && g_data) {
		free_global_data(g_data);
		g_data = NULL;
	}
	
	return g_data;
}

/* Incremental Encoding algorithm */
int encode(struct g_data_s *g_data, FILE *fd, char *path, struct enc_data_s *enc_data)
{
	short code_len = 0;
	short code_num = 0;
	char *ptr1 = NULL;
	char *ptr2 = NULL;
	char *code_line = NULL;
	int ret = 0;

	if (!path) {
		if (!report_error(g_data, FATAL, "encode: 'char *path' is NULL\n"))
		    goto EXIT;
	}
	if (g_data->VERBOSE)
	    fprintf(stdout, "%s\n", path);       
	/* Match number string */
	ptr1 = path;
	code_len = 0;
	if (enc_data->prev_line) {
		ptr2 = enc_data->prev_line;
		while (*ptr1 != '\0' && *ptr2 != '\0' && *ptr1 == *ptr2) {
			ptr1 += 1;
			ptr2 += 1;
			code_len += 1;
		}
	}
	code_num = code_len - enc_data->prev_len;
	enc_data->prev_len = code_len;	
	code_line = ptr1;
	if (code_num < -127 || code_num > 127) {
		if (fputc((char)SLOC_ESC, fd) == EOF) {
			if (!report_error(g_data, FATAL, "encode: fputc(): SLOC_ESC: %s\n", strerror(errno)))
			    goto EXIT;
		}
		if (fputc(code_num >> 8, fd) == EOF) {
			if (!report_error(g_data, FATAL, "encode: fputc(): SLOC_ESC: code_num >> 8: %s\n", strerror(errno)))
			    goto EXIT;
		}
		if (fputc(code_num, fd) == EOF) {
			if (!report_error(g_data, FATAL, "encode: fputc(): SLOC_ESC: code_num: %s\n", strerror(errno)))
			    goto EXIT;
		}
	} else {
		if (fputc(code_num, fd) == EOF) {
			if (!report_error(g_data, FATAL, "encode: fputc(): code_num: %s\n", strerror(errno)))
			    goto EXIT;
		}
	}
	if (fputs(code_line, fd) == EOF) {
		if (!report_error(g_data, FATAL, "encode: fprintf(): code_line: %s\n", strerror(errno)))
		    goto EXIT;
	}
	if (fputc('\0', fd) == EOF) {
		if (!report_error(g_data, FATAL, "encode: fputc(): '\0': %s\n", strerror(errno)))
		    goto EXIT;
	}

	if (enc_data->prev_line)
	    free(enc_data->prev_line);

	if (!(enc_data->prev_line = strdup(path))) {
		if (!report_error(g_data, FATAL, "encode: strdup(): %s\n", strerror(errno)))
		    goto EXIT;
	}
	ret = 1;
EXIT:
	return ret;
}

/* Create the database */
int create_db(struct g_data_s *g_data)
{
	FILE *fd = NULL;
	FTS *dir = NULL;
	FTSENT *file = NULL;
	char **index_path_list = NULL;
	char *tmp_file = NULL;
	uid_t db_uid = -1;
	gid_t db_gid = -1;
	mode_t db_mode = 0;
	int fd_int = -1;
	int ret = 0;
	int matched = 0;
	struct enc_data_s enc_data;
	
	/* Initialize encode data struct */
	enc_data.prev_line = NULL;
	enc_data.prev_len = 0;
	if (!rlocate_lock(g_data))
		goto EXIT;
	if (strcmp(g_data->output_db, DEFAULT_DB) == 0 && g_data->uid != DB_UID) {
		if (!report_error(g_data, FATAL, "You are not authorized to create a default rlocate database!\n"))
		    goto EXIT;	
	}	
	if (!access_path(g_data->output_db)) {
		if (!report_error(g_data, FATAL, "Unable to create database: %s\n", g_data->output_db))
		    goto EXIT;
	}	
	/* Determine the DB ownership */
	if (strcmp(g_data->output_db, DEFAULT_DB) == 0) {
		db_uid = DB_UID;
		db_gid = g_data->SLOCATE_GID;
		db_mode = DB_MODE;
	} else {
		db_uid = g_data->uid;
		db_gid = g_data->gid;
		/* Leave it to umask */
		db_mode = 0;
	}
	/* Make sure we can access the directory that we want to start
	 * searching on. */
	if (access(g_data->index_path, R_OK | X_OK) != 0) {
		if (!report_error(g_data, FATAL, "Could not access index path '%s': %s\n", g_data->index_path, strerror(errno)))
		    goto EXIT;		
	}	
	if (!(tmp_file = get_temp_file(g_data)))
	    goto EXIT;

	if (!(fd = fopen(tmp_file, "w"))) {
		if (!report_error(g_data, FATAL, "Could not open file for writing: %s: %s\n", tmp_file, strerror(errno)))
		    goto EXIT;
	}
	/* Set the mode so people can't peak by accident */
	if (db_mode) {
		if ((fd_int = fileno(fd)) == -1) {
			if (!report_error(g_data, FATAL, "Could not convert FILE stream into an integer descriptor: %s\n", strerror(errno)))
			    goto EXIT;			
		}
		
		if (fchmod(fd_int, db_mode) == -1) {
			if (!report_error(g_data, FATAL, "Could not change permissions of '%u' on file: %s: %s\n", db_mode, tmp_file, strerror(errno)))
			    goto EXIT;		
		}
	}

	/* Set the security level */
	if (putc((char)g_data->slevel, fd) == EOF) {
		if (!report_error(g_data, FATAL, "create_db: Could not write to database. putc returned EOF.\n"))
		    goto EXIT;		
	}
	/* Remove the leading '/' if not the main root directory */
	if (strlen(g_data->index_path) > 1) {
		if (g_data->index_path[strlen(g_data->index_path)-1] == '/')
		    g_data->index_path[strlen(g_data->index_path)-1] = 0;
	}
	// XXX: TODO: Support for multiple paths to index
	if (!(index_path_list = malloc(sizeof(char **) * 2))) {
		if (!report_error(g_data, FATAL, "create_db: 'index_path_list': malloc: %s\n",strerror(errno)))
		    goto EXIT;
	}

	*index_path_list = g_data->index_path;
	index_path_list[1] = NULL;
	/* Open a handle to fts */
	// XXX: TODO: Support limiting to single device FTS_XDEV
	rlocate_start_updatedb(g_data);
	if (!rlocate_fast_updatedb(g_data, fd, &enc_data)) {
		g_data->FULL_UPDATE = 1;

	if (!(dir = fts_open(index_path_list, FTS_PHYSICAL | FTS_NOSTAT, rlocate_ftscompare))) {
		if (!report_error(g_data, FATAL, "fts_open: %s\n", strerror(errno)))
		    goto EXIT;		
	}
	/* The new FTS() funtionality */
	while ((file = fts_read(dir))) {
		/* fts_read () from glibc fails with EOVERFLOW when fts_pathlen
		 * would overflow the u_short file->fts_pathlen. */
		if (file->fts_info == FTS_DP || file->fts_info == FTS_NS)
		    continue;
		
		matched = 0;
		if (!g_data->exclude || !(matched = match_exclude(g_data, file->fts_path))) {
			if (!encode(g_data, fd, file->fts_path, &enc_data))
			    goto EXIT;
		} else if (matched != -1) {
			fts_set(dir, file, FTS_SKIP);
		} else {
			goto EXIT;
		}
	}
	
	if (fts_close(dir) == -1) {
		if (!report_error(g_data, FATAL, "fts_close(): Could not close fts: %s\n", strerror(errno)))
		    goto EXIT;		
	}	
	} // rlocate_fast_updatedb
	if (fd && fclose(fd) == -1) {
		if (!report_error(g_data, FATAL, "fclose(): Could not close tmp file: %s: %s\n", tmp_file, strerror(errno)))
		    goto EXIT;		
	}
	fd = NULL;
	rlocate_end_updatedb(g_data);
	if (rename(tmp_file, g_data->output_db) == -1) {
		if (!report_error(g_data, FATAL, "create_db(): rename(): Could not rename '%s' to '%s': %s\n", tmp_file, g_data->output_db, strerror(errno)))
		    goto EXIT;		
	}
	/* Only chown database to group 'slocate' if the output database
	 * is the default one. */
	if (strcmp(g_data->output_db, DEFAULT_DB) == 0) {
		if (chown(g_data->output_db, db_uid, db_gid) == -1) {
			if (!report_error(g_data, FATAL, "create_db(): chown(): Could not set '%s' group on file: %s: %schown: %s\n", DB_GROUP, g_data->output_db, strerror(errno)))
			    goto EXIT;			
		}
	}
	
	ret = 1;
EXIT:
	
	if (fd)
	    free(fd);
	fd = NULL;
	if (tmp_file)
	    free(tmp_file);
	tmp_file = NULL;
	if (index_path_list)
	    free(index_path_list);	
	index_path_list = NULL;
	if (enc_data.prev_line)
	    free(enc_data.prev_line);
	enc_data.prev_line = NULL;
	enc_data.prev_len = 0;
	rlocate_unlock();

	return ret;
}


/* Set the path head.
 * 
 * Construct the beginning of the full path.
 */
char * set_path_head(struct g_data_s *g_data, char *path_head, int code_num, char *prev_code_str)
{
	int path_len = 0;
	int len = 0;
	int ret = 0;

	if (path_head)
	    path_len = strlen(path_head);

	/* If code_num > 0 then we want to add from the previous line */
	if (code_num > 0) {
		if (!prev_code_str) {
			if (!report_error(g_data, FATAL, "set_path_head: prev_code_str == NULL.\n"))
			    goto EXIT;
		}
		if (!path_head) {
			if (!(path_head = malloc(sizeof(char)))) {
				if (!report_error(g_data, FATAL, "set_path_head: malloc: %s\n", strerror(errno)))
				    goto EXIT;
			}
			path_head[0] = 0;
		}
		if (!(path_head = realloc(path_head, (sizeof(char) * (path_len+code_num+1))))) {
			if (!report_error(g_data, FATAL, "set_path_head: realloc: path_head: %s\n", strerror(errno)))
			    goto EXIT;
		}
		path_head[path_len+code_num] = 0;
		strncat(path_head, prev_code_str, code_num);
	/* If code_num < 0 then we will want to delete from the end of the
	 * previous line */
	} else if (code_num < 0) {
		/* NOTE '+code_num' is done because code_num will be < 0 thus
		 * cancelling the + */
		len = path_len+code_num;
		if (len <= 0) {
			if (!report_error(g_data, FATAL, "set_path_head: path_head len <= 0: %d\n", len))
			    goto EXIT;
		}
		if (!(path_head = realloc(path_head, (sizeof(char) * (len+1))))) {
			if (!report_error(g_data, FATAL, "set_path_head: realloc: %s\n", strerror(errno)))
			    goto EXIT;
		}

		if (len <= path_len)
		    path_head[len] = 0;
	}
	ret = 1;

EXIT:
	if (!ret && path_head) {
		free(path_head);
		path_head = NULL;
	}
	return path_head;
}

int search_path(struct g_data_s *g_data, char *full_path, char *search_str, int globflag)
{
	int ret = 0;
	int match_ret = 0;

	match_ret = match(g_data, full_path, search_str, globflag);
	if (match_ret == 1) {
		if (g_data->slevel == VERIFY_ACCESS && !verify_access(full_path))
		    match_ret = 0;
	} else if (match_ret == -1) {
		goto EXIT;
	}
	if (match_ret == 1) {
		// if (g_data->queries > 0)
		//    g_data->queries -= 1;
		// fprintf(stdout, "%s\n", full_path);
		
		rlocate_printit(g_data, full_path);
	}
	ret = 1;
EXIT:
	return ret;
}

/* Search the database */
int search_db(struct g_data_s *g_data, char *database, char *search_str)
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
	int globflag = 0;
	struct stat db_stat;
	gid_t effective_gid = 0;
	time_t now = 0;

	effective_gid = getegid();

	/* Drop priviledges if the database's group is not slocate */
	if (stat(database, &db_stat) == -1) {
		if (!report_error(g_data, FATAL, "Could not obtain information on database file '%s': %s\n", database, strerror(errno)))
		    goto EXIT;
	}	
	/* If the database's file group is not apart of the 'slocate' group,
	 * drop privileges. When multiple databases are specified, the ones
	 * apart of the 'slocate' group will be searched first before the
	 * privileges are dropped. */
	if (effective_gid == g_data->SLOCATE_GID && db_stat.st_gid != g_data->SLOCATE_GID) {
		if (setgid(g_data->gid) == -1) {
			if (!report_error(g_data, FATAL, "Could not drop privileges."))
			    goto EXIT;
		}
	}
	
	/* Warn if the database is old */
	if (!g_data->QUIET) {
		if ((now = time(&now)) == -1) {
			if (!report_error(g_data, FATAL, "search_db: could not get time: %s\n", strerror(errno)))
			    goto EXIT;
		}
		
		if (now - db_stat.st_mtime > WARN_SECONDS) {
			if (!report_error(g_data, WARNING,"database %s' is more than %s old\n", database, WARN_MESSAGE))
			    goto EXIT;
		}
	}
	if ((fd = open(database, O_RDONLY)) == -1) {
		if (!report_error(g_data, FATAL, "search_db: open: '%s': %s\n", database, strerror(errno)))
		    goto EXIT;
	}

	/* slevel */
	buf_len = read(fd, ch, 1);
	if (buf_len == 0) {
		if (!report_error(g_data, FATAL, "search_db: read: '%s': Database file is empty.\n", database))
		    goto EXIT;
	} else if (buf_len == -1) {
		if (!report_error(g_data, FATAL, "serach_db: read: '%s': %s\n", database, strerror(errno)))
		    goto EXIT;
	}
	
	if (search_str && (strchr(search_str,'*') != NULL || strchr(search_str,'?') ||
			   (strchr(search_str,'[') && strchr(search_str,']')))) {
		char *tmp_str = NULL;
		int ss_len = strlen(search_str);
		globflag = 1;
		/* Wrap search string with '*' wildcard characters
		 * since fnmatch will not match midstring */
		tmp_str = malloc(ss_len+3);
		*tmp_str = '*';
		memcpy(tmp_str+1, search_str, ss_len);
		tmp_str[ss_len+1] = '*';
		tmp_str[ss_len+2] = 0;
		free(search_str);
		search_str = tmp_str;
	}	

	g_data->slevel = ch[0];
	b = 0;
	buf_len = read(fd, buffer, BLOCK_SIZE);
	rlocate_init(g_data, database, search_str, search_str, globflag);
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
		if (!search_path(g_data, full_path, search_str, globflag))
		    goto EXIT;		

		if (g_data->queries == 0)
		    break;
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
	rlocate_done(g_data);
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


/* Main function */
int main(int argc, char **argv)
{
	struct g_data_s *g_data = NULL;
	struct cmd_data_s *cmd_data = NULL;
	int ret = 1;
	int i = 0;
	int s = 0;
	int search_ret = 1;

	if (!(g_data = init_global_data(argv)))
	    goto EXIT;	

	
	/* Parse command line arguments */
	if (!(cmd_data = parse_cmds(g_data, argc, argv)))
	    goto EXIT;

	if (cmd_data->exit_but_nice) {
		ret = 0;
		goto EXIT;
	}	

#if 0
	printf("Q: %d\n", g_data->QUIET);
	printf("V: %d\n", g_data->VERBOSE);
	printf("U: %d\n", cmd_data->updatedb);
	printf("O: %s\n", g_data->output_db);
	printf("Path: %s\n", g_data->index_path);
	for (i = 0; g_data->input_db && g_data->input_db[i]; i++)
	    printf("Input DB: %s\n", g_data->input_db[i]);
	for (i = 0; g_data->exclude && g_data->exclude[i]; i++)
	    printf("Exclude: %s\n", g_data->exclude[i]);
	
	printf("UID:         %d\n", g_data->uid);
	printf("GID:         %d\n", g_data->gid);
	printf("slocate GID: %d\n", g_data->SLOCATE_GID);
#endif

	if (cmd_data->updatedb) {
		/* Drop priviledges since they are not required to
		 * create a database */
		if (setgid(g_data->gid) == -1) {
			if (!report_error(g_data, FATAL, "Could not drop privileges."))
			    goto EXIT;
		}

		if (!g_data->output_db) {
			if (!(g_data->output_db = strdup(DEFAULT_DB))) {
				if (!report_error(g_data, FATAL, "main: strdup: %s\n", strerror(errno)))
				    goto EXIT;
			}
		}
		if (create_db(g_data))
		    ret = 0;
		goto EXIT;
	} else if (argc >= 2) {
		/* Search the database */
		if (!g_data->input_db || !g_data->input_db[0]) {
			g_data->input_db = init_input_db(g_data, 1);
			g_data->input_db[0] = strdup(DEFAULT_DB);
		}

		for (i = 0; g_data->input_db[i]; i += 1) {
			/* Regular expression search */
			if (g_data->regexp_data)
			    search_ret = search_db(g_data, g_data->input_db[i], NULL);
			/* Search each string */
			else {
				search_ret = 1;
				for (s = 0; cmd_data->search_str && cmd_data->search_str[s] && search_ret; s += 1)
				    search_ret = search_db(g_data, g_data->input_db[i], cmd_data->search_str[s]);
			}

			if (!search_ret)
			    goto EXIT;
		}
	} else {
		usage(g_data);
		goto EXIT;
	}

	ret = 0;

EXIT:
	/* Free up memory */
	free_global_data(g_data);
	g_data = NULL;
	free_cmd_data(cmd_data);
	cmd_data = NULL;

	return ret;
}
