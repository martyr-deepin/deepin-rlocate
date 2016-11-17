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

#ifndef __CMDS_H
#define __CMDS_H

/* Shared commands */
struct cmd_data_s {
	int updatedb;
	char **search_str;
	char *updatedb_conf;
	int exit_but_nice;
};

void usage(struct g_data_s *g_data);
void free_cmd_data(struct cmd_data_s *cmd_data);
struct cmd_data_s * parse_cmds(struct g_data_s *g_data, int argc, char **argv);
int parse_fs_exclude(struct g_data_s *g_data, char *data_str);
int parse_exclude(struct g_data_s *g_data, char *estr);

#endif
