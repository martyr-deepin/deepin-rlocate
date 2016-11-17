/*****************************************************************************
 *    Secure Locate
 *    Copyright (c) 2005, 2006 Kevin Lindsay
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
 *****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <fnmatch.h>

#include "slocate.h"
#include "utils.h"

#ifdef RAND_MAX
# undef RAND_MAX
#endif

#define RAND_MAX 9999

/* Duplicate string and make all characters lowercase */
char * tolower_strdup(char *str)
{
	char *nocase_str = NULL;
	char *ptr = NULL;
	
	if (!(nocase_str = strdup(str)))
		return NULL;

	ptr = nocase_str;
	
	for (ptr = nocase_str; *ptr; ptr++)
	    *ptr = tolower(*ptr);

	return nocase_str;
}

/* Check if the current path matches the search criteria */
int match(struct g_data_s *g_data, char *full_path, char *search_str, int globflag)
{
	int foundit = 0;
	int ret = 0;
	int nmatch = 32;
	regmatch_t pmatch[32];
#ifndef FNM_CASEFOLD
	char *nocase_str = NULL;
	char *nocase_path = NULL;
#endif

	/* If searching with regular expressions */
	if (g_data->regexp_data) {
		foundit = ! regexec(g_data->regexp_data->preg, full_path, nmatch, pmatch, 0);
	/* Case sensitive search */
	} else if (search_str && !g_data->nocase) {
		if (globflag) {
			foundit = fnmatch(search_str, full_path, 0);
						
			if (foundit == 0)
			    foundit = 1;
			else if (foundit == FNM_NOMATCH)
			    foundit = 0;
			else {
				report_error(g_data, FATAL, "match: fnmatch: unknown error.\n");
				ret = -1;
				goto EXIT;
			}
		} else
		    foundit=(strstr(full_path, search_str) != NULL);
	/* Case insensitive search */
	} else if (search_str && g_data->nocase) {
#ifndef FNM_CASEFOLD
		if (!(nocase_str = tolower_strdup(search_str))) {
			report_error(g_data, FATAL, "match: not FNM_CASEFOLD: tolower_strdup: nocase_str: Returned NULL: %s\n", strerror(errno));
			ret = -1;
			goto EXIT;
		}

		if (!(nocase_path = tolower_strdup(full_path))) {
			report_error(g_data, FATAL, "match: not FNM_CASEFOLD: tolower_strdup: nocase_path: Returned NULL: %s\n", strerror(errno));
			ret = -1;
			goto EXIT;
		}

		if (globflag) {
			foundit = fnmatch(nocase_str,nocase_path, 0);
			
			if (foundit == 0)
			    foundit = 1;
			else if (foundit == FNM_NOMATCH)
			    foundit = 0;
			else {
				report_error(g_data, FATAL, "match: not FNM_CASEFOLD: fnmatch: nocase: unknown error.\n");
				ret = -1;
				goto EXIT;
			}
			
		}
		else if (strstr(nocase_path, nocase_str))
		    foundit = 1;

#else /* FNM_CASEFOLD */
		if (globflag) {
			foundit = fnmatch(search_str, full_path, FNM_CASEFOLD);
			
			if (foundit == 0)
			    foundit = 1;
			else if (foundit == FNM_NOMATCH)
			    foundit = 0;
			else {
				report_error(g_data, FATAL, "match: FNM_CASEFOLD: fnmatch: nocase: unknown error.\n");
				ret = -1;
				goto EXIT;
			}
			
		} else
		    foundit = (strcasestr(search_str, search_str) != NULL);

#endif /* FNM_CASEFOLD */
	} 

	if (foundit)
	    ret = 1;		

EXIT:
	if (nocase_str)
	    free(nocase_str);
	nocase_str = NULL;
	
	if (nocase_path)
	    free(nocase_path);
	
	nocase_path = NULL;
	
	return ret;
}

/* Match exclude
 * 
 * 1  == match
 * 0  == no match
 * -1 == error
 */
int match_exclude(struct g_data_s *g_data, char *path) {
	int i;

	if (!g_data->exclude || !path)
	    return 0;

	/* Compare inode numbers to check if the exclude path matches */
	for (i = 0; g_data->exclude[i]; i++) {		
		if (strcmp(path, g_data->exclude[i]) == 0) {
			if (g_data->VERBOSE)
			    printf("Excluding: %s\n", path);
			return 1;
		}
	}

	return 0;
}

/* strndup() seems to be a GNU thing */
char *sl_strndup(const char *str, size_t size) 
{
	char *new_str = NULL;
	
	if (!str || size < 0)
	    return NULL;
	
	if (strlen(str) < size)
	    return NULL;

	if (!(new_str = malloc(sizeof(char)*(size+1))))
	    return NULL;

	*new_str = 0;
	new_str = strncpy(new_str, str, size);
	*(new_str+size) = 0;

	return new_str;	
}

/* Make a path absolute */
char *
make_absolute_path(struct g_data_s *g_data, char *path)
{
	char *new_path = NULL;
	char *cwd = NULL;
	int cwd_size = 0;

	if (!path) {
		report_error(g_data, FATAL, "make_absolute_path(): char *path == NULL\n");
		goto EXIT;
	}

	if (*path == '/')
	    return (strdup(path));
	
	/* Build the current working directory */
	do {
		if (cwd)
		    free(cwd);
		cwd = NULL;
		
		/* Increment in chunks of 128 bytes */
		cwd_size += 128;
		if (!(cwd = malloc(sizeof(char)*cwd_size)))
		    if (!report_error(g_data, FATAL, "make_absolute_path(): malloc(): do {} while: %s\n", strerror(errno)))
			goto EXIT;

	} while(!getcwd(cwd, cwd_size));

	/* +2 because we need an extra '/' */	
	if (!(new_path = malloc(sizeof(char) * (strlen(path)+strlen(cwd)+2)))) {
		if (!report_error(g_data, FATAL, "make_absolute_path(): malloc(): %s\n", strerror(errno)))
		    goto EXIT;
	}

	strcpy(new_path, cwd);
	strcat(new_path, "/");
	strcat(new_path, path);

	free(cwd);
	cwd = NULL;

	return new_path;

EXIT:
	if (cwd)
	    free(cwd);
	cwd = NULL;
	if (new_path)
	    free(new_path);
	new_path = NULL;

	return NULL;
}

/* Get the GID for group slocate */
unsigned short 
    get_gid(struct g_data_s *g_data, const char *group, int *ret)
{
	struct group *grpres = NULL;

	*ret = 1;

	if ((grpres = getgrnam(group)) == NULL) {
		if (!report_error(g_data, WARNING, "Could not find the group: %s in the /etc/group file.\n", DB_GROUP)) {
			*ret = 0; return 255;
		}
		if (!report_error(g_data, FATAL, "This is a result of the group missing or a corrupted group file.\n")) {
			*ret = 0; return 255;
		}
	}

	return (unsigned short) grpres->gr_gid;
}

/* Verify DB
 * For now we just check if there is a '1' or '0' as the first character
 * of the DB. (security level).
 * 
 * Returns:  0 == Invalid
 *          -1 == File does not exist or inaccessible
 *           1 == Valid
 */
int
verify_slocate_db(struct g_data_s *g_data, char *file)
{
	char ch[1];
	struct stat tf_stat;
	int bytes = 0;
	int fd = -1;
	
	if (access(file, W_OK | R_OK) == 0) {
		if (lstat(file, &tf_stat) == -1) {
			if (!report_error(g_data, FATAL, "get_temp_file: fstat(): %s: %s\n", file, strerror(errno)))
			    goto EXIT;
			
		}
		if (tf_stat.st_size == 0)
		    return 0;

		if ((fd = open(file, O_RDONLY)) == -1) {
			if (!report_error(g_data, FATAL, "get_temp_file: open(): %s: %s\n", file, strerror(errno)))
			    goto EXIT;
			
		}

		bytes = read(fd, ch, 1);
		
		if (close(fd) == -1) {
			if (!report_error(g_data, FATAL, "get_temp_file: close(): %s: %s\n", file, strerror(errno)))
			    goto EXIT;
			
		}
		
		if (bytes == 0) {
			if (!report_error(g_data, FATAL, "Could not read from file: %s\n", file))
			    goto EXIT;
			
		}
		
		if (ch[0] != '1' && ch[0] != '0')
		    return 0;
		
	} else
	    return -2;

	return 1;
	
EXIT:
	return -1;
}

/* Get a temp filename which doesn't exist */
char *
get_temp_file(struct g_data_s *g_data)
{
	char *tmp_file = NULL;
	int ret;

	/* .stf == Slocate Temporary File */
	if (!(tmp_file = malloc(sizeof(char) * (strlen(g_data->output_db)+strlen(".stf")+1))))
	    if (!report_error(g_data, FATAL, "get_temp_file: malloc(): %s\n", strerror(errno)))
		goto EXIT;

	strcpy(tmp_file, g_data->output_db);
	strcat(tmp_file, ".stf");

	ret = verify_slocate_db(g_data, tmp_file);
	if (ret == 0) {
		if (!report_error(g_data, FATAL, "The temp file '%s' already exists and does not appear to be a valid slocate database. Please remove before creating the database.\n", tmp_file))
		    goto EXIT;
	} else if (ret == -1) {
		
		goto EXIT;
	}

	return tmp_file;
	
EXIT:
	
	if (!tmp_file)
	    free(tmp_file);
	tmp_file = NULL;
	
	return NULL;	
}

/* Check if a path/file is writeable */
int
access_path(char *path)
{
	char *ptr = NULL;
        int tmp_ch;
	int ret;
	
	if (!path)
	    return 0;	
	
	if (!(ptr = rindex(path, '/')))
	    return 0;
	
	ptr++;
	
	tmp_ch = *ptr;
	*ptr = 0;
	
	ret = access(path, X_OK | W_OK);
	
	*ptr = tmp_ch;
	
	if (ret == 0)
	    return 1;
	
	return 0;
}

/* report an error
 * 
 * int STATUS can equal WARNING == 0
 *                      FATAL   == 1
 * 
 * WARNING status will just return.
 * FATAL status will exit the program with error code 1.
 * 
 * Returns 1 to indicate the program should continue
 *         0 to indicate that the program should exit
 */
int
report_error(struct g_data_s *g_data, int STATUS, const char *format, ...)
{
	
	/* Guess we need no more than 100 bytes. */
	int n;
	unsigned int size = 1024;
	char *str = NULL;
	va_list ap;
	int ret = 1;
	
	/* If QUIET is on and STATUS == 1 (fatal) then exit
	 * else just return */
	if (g_data->QUIET && STATUS == FATAL) {
		ret = 0;
		goto EXIT;
	} else if (g_data->QUIET)	    
	    return 1;
	
	if ((str = malloc(sizeof(char) * size)) == NULL) {
		fprintf(stderr,"%s: report_error: fatal: malloc: %s\n", g_data->progname, strerror(errno));
		ret = 0;
		goto EXIT;
	}

	while (1) {
		/* Try to print in the allocated space. */
		va_start(ap, format);
		n = vsnprintf(str, size, format, ap);
		va_end(ap);

		/* If that worked, print message. */
		if (n > -1 && n < size) {
			if (STATUS == FATAL)
			    fprintf(stderr,"%s: fatal error: %s", g_data->progname, str);
			else if (STATUS == WARNING)
			    fprintf(stderr,"%s: warning: %s", g_data->progname, str);
			else
			    fprintf(stderr,"%s: %s", g_data->progname, str);
			fflush(stderr);
			break;
		}

		/* Else try again with more space. */
		size += 1024;

		if ((str = realloc(str, size)) == NULL) {
			fprintf(stderr,"%s: report_error: fatal: realloc: %s\n",g_data->progname, strerror(errno));
			ret = 0;
			goto EXIT;
		}
	}

	if (STATUS == FATAL) {
		ret = 0;
		goto EXIT;
	}
		

EXIT:
	free(str);
	str = NULL;
	
	return ret;
}

int load_file(struct g_data_s *g_data, char *filename, char **file_data)
{
	int ret = 0;
	int fd = -1;
	struct stat fbuf_stat;
	
	*file_data = NULL;

	if (!filename) {
		report_error(g_data, FATAL, "load_file: filename == NULL.\n");
		goto EXIT;
	}
	
	if ((fd = open(filename, O_RDONLY)) == -1) {
		report_error(g_data, FATAL, "load_file: Could not open file: %s: %s\n", filename, strerror(errno));
		goto EXIT;
	}

	if (fstat(fd, &fbuf_stat) == -1) {
		report_error(g_data, FATAL, "load_file: Could not stat file: %s: %s\n", filename, strerror(errno));
		goto EXIT;
	}
	
	if (!(*file_data = malloc(sizeof(char) * (fbuf_stat.st_size+1)))) {
		report_error(g_data, FATAL, "load_file: *file_data: malloc: %s\n", strerror(errno));
		goto EXIT;
	}

	if (read(fd, *file_data, fbuf_stat.st_size) != fbuf_stat.st_size) {
		report_error(g_data, FATAL, "load_file: read: Failed to read all %d bytes from %s.\n", fbuf_stat.st_size, filename);
		goto EXIT;
	}
	(*file_data)[fbuf_stat.st_size] = 0;

	ret = 1;
	
EXIT:
	
	if (!ret) {
		if (*file_data) {
			free(*file_data);
			*file_data = NULL;
		}
	}
	
	return ret;
}

/* Verify access to the file. access() follows symlinks, so we need
 * to check them separately */
int verify_access(const char *path)
{
	struct stat path_stat;
	int ret = 0;
	char *ptr = NULL;

	if (lstat(path, &path_stat) == -1)
	    goto EXIT;

	if (!S_ISLNK(path_stat.st_mode)) {
		if (access(path, F_OK) != 0)
		    goto EXIT;
	} else if ((ptr = rindex(path, '/'))) {
		*ptr = 0;
		if (access(path, F_OK) == 0)
		    ret = 1;
		*ptr = '/';
		goto EXIT;
	}

	ret = 1;
EXIT:
	return ret;
}
