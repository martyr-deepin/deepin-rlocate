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

/* updatedb.conf parsing */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#define __USE_GNU
#include <string.h>

#include "slocate.h"
#include "utils.h"
#include "cmds.h"


char * get_next_char(char ch, char *ptr)
{
	while (*ptr && *ptr != ch && *ptr != '\n')
	    ptr++;

	if (*ptr != ch)
	    return NULL;

	return ptr;
}

char * get_value(struct g_data_s *g_data, char *key, char *conf_data)
{
	char *ptr = NULL;
	char *val = NULL;
	char *quote_ptr = NULL;
	char *end_ptr = NULL;
	int i;
	int key_len = strlen(key);

	ptr = conf_data;
	
	/* Find a valid key */
	while ((ptr = strstr(ptr, key))) {
		
		/* Make sure we have a proper key */
		if (strlen(ptr) >= key_len) {
			if (!isspace(ptr[key_len]) && ptr[key_len] != '=') {
				ptr++;
				continue;
			}
		}

		/* Check if we are not commented out */
		for (i = 0; *(ptr-i) && (ptr-i) != conf_data; i++) {
			if (*(ptr-i) == '\n')
			    break;
			if (*(ptr-i) == '#')
			    break;
		}

		if (*(ptr-i) == '\n' || (ptr-i) == conf_data)
		    break;
		
		ptr++;		
	}

	if (!ptr)
	    goto EXIT;

	/* Parse the valid key */
	if (!(ptr = get_next_char('=', ptr))) {
		report_error(g_data, WARNING, "%s: syntax error at '%s': Could not find '=' character.\n", UPDATEDB_FILE, key);
		goto EXIT;
	}

	if (!(quote_ptr = get_next_char('"', ptr))) {
		ptr++;
		while (*ptr && isspace(*ptr) && *ptr != '\n')
		    ptr++;

		if (!*ptr || *ptr == '\n') {
			report_error(g_data, WARNING, "%s: syntax error at '%s': No value found.\n", UPDATEDB_FILE, key);
			goto EXIT;
		}
	} else
	    ptr = quote_ptr+1;

	end_ptr = ptr;

	if (quote_ptr) {
		if (!(end_ptr = get_next_char('"', end_ptr))) {
			report_error(g_data, WARNING, "%s: syntax error at '%s': Missing second \".\n", UPDATEDB_FILE, key);
			goto EXIT;
		}
	} else {
		while (*end_ptr && !isspace(*end_ptr) && *end_ptr != '\n' )
		    end_ptr++;
	}
	
	if (!(end_ptr-ptr)) {
		report_error(g_data, WARNING, "%s: syntax error at '%s': No value found.\n", UPDATEDB_FILE, key);
		goto EXIT;
	}

	if (!(val = strndup(ptr, end_ptr-ptr)))
	    goto EXIT;

EXIT:

	return val;
}

int parse_prune(struct g_data_s *g_data, char *key, char *conf_data, const char *delim, char **data_str)
{
	char *val = NULL;
	char *ptr = NULL;
	char *end_ptr = NULL;
	int ret = 0;

	if (!(val = get_value(g_data, key, conf_data)))
	    goto EXIT;

	if (!(*data_str = malloc(sizeof(char) * (strlen(val)+1)))) {
		report_error(g_data, FATAL, "parse_prune: data_str: malloc: %s\n", strerror(errno));
		goto EXIT;
	}
	**data_str = 0;

	ptr = val;

	/* replace all spaces with ','s, but only the
	   spaces between words and one space only */
	while (*ptr) {
		while (*ptr && isspace(*ptr))
		    ptr++;

		if (!*ptr)
		    break;

		end_ptr = ptr;

		while (*end_ptr && !isspace(*end_ptr))
		    end_ptr++;

		if (ptr != val)
		    *data_str = strcat(*data_str, delim);
		*data_str = strncat(*data_str, ptr, end_ptr-ptr);

		ptr = end_ptr;
	}

	ret = 1;

EXIT:

	return ret;	
}

int parse_PRUNEFS(struct g_data_s *g_data, char *conf_data)
{
	char *fs_str = NULL;
	int ret = 0;

	if (!parse_prune(g_data, "PRUNEFS", conf_data, ",", &fs_str))
	    goto EXIT;
	
	if (!parse_fs_exclude(g_data, fs_str))
	    goto EXIT;

	ret = 1;

EXIT:
	
	if (fs_str) {
		free(fs_str);
		fs_str = NULL;
	}

	return ret;
}

int parse_PRUNEPATHS(struct g_data_s *g_data, char *conf_data)
{
	char *path_str = NULL;
	int ret = 0;

	if (!parse_prune(g_data, "PRUNEPATHS", conf_data, ",", &path_str))
	    goto EXIT;

	if (!parse_exclude(g_data, path_str))
	    goto EXIT;


	ret = 1;
EXIT:
	if (path_str) {
		free(path_str);
		path_str = NULL;
	}

	return ret;
}

int parse_updatedb(struct g_data_s *g_data, char *conf_file)
{
	char *conf_data = NULL;
	int res = 0;
	int load_ret = 0;

	if (conf_file)
	    load_ret = load_file(g_data, conf_file, &conf_data);
	else
	    load_ret = load_file(g_data, UPDATEDB_FILE, &conf_data);

	if (!load_ret)
	    goto EXIT;
		    
	if (!parse_PRUNEFS(g_data, conf_data))
	    goto EXIT;
	
	if (!parse_PRUNEPATHS(g_data, conf_data))
	    goto EXIT;

	res = 1;
EXIT:
	if (conf_data) {
		free(conf_data);
		conf_data = NULL;
	}

	return res;
}
