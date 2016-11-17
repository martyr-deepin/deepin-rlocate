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

#ifndef __UTILS_H
#define __UTILS_H

/* Report Error */

#define FATAL 0
#define WARNING 1

int report_error(struct g_data_s *g_data, int STATUS, const char *format, ...);

/****************/

char * tolower_strdup(char *str);
int match(struct g_data_s *g_data, char *full_path, char *search_str, int globflag);
int match_exclude(struct g_data_s *gdata, char *path);
char *sl_strndup(const char *str, size_t size);
char * make_absolute_path(struct g_data_s *g_data, char *path);
char * get_temp_file(struct g_data_s *g_data);
int access_path(char *path);
unsigned short get_gid(struct g_data_s *g_data, const char *group, int *ret);
int load_file(struct g_data_s *g_data, char *filename, char **file_data);
int verify_access(const char *path);

#endif
