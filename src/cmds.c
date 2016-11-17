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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>

#include "slocate.h"
#include "utils.h"
#include "cmds.h"
#include "conf.h"

/* Init Command Data */
struct cmd_data_s *init_cmd_data(struct g_data_s *g_data)
{
	struct cmd_data_s *cmd_data = NULL;
	
	if (!(cmd_data = malloc(sizeof(struct cmd_data_s)))) {
		if (!report_error(g_data, FATAL, "cmd_dat_s: malloc: %s\n", strerror(errno)))
		    goto EXIT;
	}

	/* cmd_data Defaults */
	cmd_data->updatedb = FALSE;
	cmd_data->search_str = NULL;
	cmd_data->updatedb_conf = NULL;
	cmd_data->exit_but_nice = 0;

	return cmd_data;
	
EXIT:
	return NULL;
}

/* Free Command Data */
void free_cmd_data(struct cmd_data_s *cmd_data)
{
	int i;
	
	if (!cmd_data)
	    return;
	
	if (cmd_data->search_str) {
		for (i = 0; cmd_data->search_str[i]; i += 1)
		    free(cmd_data->search_str[i]);

		free(cmd_data->search_str);
	}
	if (cmd_data->updatedb_conf)
	    free(cmd_data->updatedb_conf);

	free(cmd_data);
	cmd_data = NULL;
}

/* Usage 
 * XXX: Review for accuracy
 */
void usage(struct g_data_s *g_data)
{
	int i;

	printf("%s\n"
	       "Copyright (c) 2006 Rasto Levrinc\n\n"
	       "Search:          %s [-qi] [-d <path>] [--database=<path1:path2:...>]\n", SL_VERSION, g_data->progname);
	for (i = 0; i < strlen(g_data->progname)-1; i+=1)
	    printf(" ");	       
	printf("                   <search string>\n"
	       "                 %s [-r <regexp>] [--regexp=<regexp>]\n"
	       "Update database: %s [-qv] [-o <file>] [--output=<file>]\n"
	       "                 %s [-e <dir1,dir2,...>] [-f <fs_type1,...> ] [-l <level>]\n"
	       , g_data->progname, g_data->progname, g_data->progname);
	
	for (i = 0; i < strlen(g_data->progname)-1; i+=1)
	    printf(" ");
	
	printf(
#ifndef __FreeBSD__
	       "                   [-c <file>] <[-U <path>] [-u]> [-I] [--initdiffdb]\n"
	       "                   [--fast-update] [--full-update]\n"
#else
	       "                   <[-U <path>] [-u]>\n"
#endif
	       "General:         %s [-Vh] [--version] [--help]\n\n"
	       "   Options:\n"
	       "   -u                 - Create rlocate database starting at path /.\n"
	       "   -U <dir>           - Create rlocate database starting at path <dir>.\n", g_data->progname);
	
#ifndef __FreeBSD__
	printf("   -c <file>           - Parse original GNU Locate's configuration file\n"
	       "                        when using the -u or -U options.  If 'updatedb' is\n"
	       "                        symbolically linked to the '%s' binary, the\n"
	       "                        original configuration file '/etc/updatedb.conf' will\n"
	       "                        automatically be used.\n", g_data->progname);
#endif

	printf("   -e <dir1,dir2,...> - Exclude directories from the rlocate database when\n"
	       "                        using the -u or -U options.\n"
	       "   -f <fs_type1,...>  - Exclude file system types from the rlocate database\n"
	       "                        when using the -u or -U options. (ie. NFS, etc).\n"                                  
	       "   -l <level>         - Security level. \n"
	       "                           0 turns security checks off. This will make\n"
	       "                             searchs faster.\n"
	       "                           1 turns security checks on. This is the default.\n"
	       "   -q                 - Quiet mode.  Error messages are suppressed.\n"
	       "   -n <num>           - Limit the amount of results shown to <num>.\n"
	       "   -i                 - Does a case insensitive search.\n"
	       "   -r <regexp>\n"
	       "   --regexp=<regexp>  - Search the database using a basic POSIX regular\n"
	       "                        expression.\n"
	       "   -o <file>\n"
	       "   --output=<file>    - Specifies the database to create.\n"
	       "   -d <path>\n"
	       "   --database=<path>  - Specfies the path of databases to search in.\n"
	       "   -I\n"
	       "   --initdiffdb       - Initialize the diff database if user database is\n"
	       "                        created. If default database is created --initdiffdb\n"
	       "                        is implied.\n"
	       "   --fast-update      - Force fast update of the default database.\n"
	       "   --full-update      - Force full update of the default database.\n"
	       "   -h\n"
	       "   --help             - Display this help.\n"
	       "   -v\n"
	       "   --verbose          - Verbose mode. Display files when creating database.\n"
	       "   -V\n"
	       "   --version          - Display version.\n"
	       "\n"
	       "Author: Rasto Levrinc, based on slocate from Kevin Lindsay\n"
	       "Bugs:   e9526925@stud3.tuwien.ac.at\n"
	       "HTTP:   http://rlocate.sourceforge.net/\n"
	       "\n");
}

/* Parse a comma delimited string of files and dirs into an array of pointers.
 *
 * string == "file,dir,...\0"
 */
/* TODO: Clean up returns. use ret. */
int parse_exclude(struct g_data_s *g_data, char *estr)
{
	char *token = NULL;
	char *ptr1 = NULL;
	char *ptr2 = NULL;
	int len = 0;
	
	if (!estr) {
		if (!report_error(g_data, WARNING, "parse_exclude: String passed is NULL.\n"))
		    goto EXIT;

		return 1;
	}
	
	if (strlen(estr) == 0)
	    return 1;
	
	/* Get the array length */
	for (len = 0; g_data->exclude && g_data->exclude[len]; len++);

	ptr1 = estr;
	
	/* Loop until we run out of string to parse.
	 * Loop ends via break; */
	while (1) {
		/* If *ptr2 == 0 then we have already parsed the last token */
		if (ptr2 && *ptr2 == 0)
		    break;

		/* Move start pointer to start of next token.
		 * If ptr2 == NULL then this is the first token and we havn't
		 * parsed it yet. */
		if (ptr2)
		    ptr1 = ptr2+1;

		/* Get first token postion.
		 * If no delimeter, most ptr2 to the end of the string. */
		if (!(ptr2 = index(ptr1, ','))) {
			ptr2 = ptr1;
			while(*ptr2 != 0)
			    ptr2 += 1;
		}

		/* Make token */
		if (!(token = sl_strndup(ptr1, ptr2-ptr1))) {
			if (!report_error(g_data, FATAL, "parse_exclude: sl_strndup: failed.\n"))
			    goto EXIT;
		}
				
		/* Make sure the file exists and is accessible, otherwise ignore it */
		if (access(token, F_OK) != 0) {
			if (errno != EACCES && errno != ENOENT) {
				if (!report_error(g_data, FATAL, "parse_exclude: access: '%s': %s\n", token, strerror(errno)))
				    goto EXIT;
			}

			if (token)
			    free(token);
			token = NULL;

			continue;
		}

		/* Increment the length */
		len += 1;

		/* Allocate memory */
		if (!g_data->exclude) {
			if (!(g_data->exclude = malloc(sizeof(char *) * (len+1)))) {
				if (!report_error(g_data, FATAL, "parse_exclude: malloc: %s\n", strerror(errno)))
				    goto EXIT;
			}
		} else {
			if (!(g_data->exclude = realloc(g_data->exclude, sizeof(char *) * (len+1)))) {
				if (!report_error(g_data, FATAL, "parse_exclude: realloc: %s\n", strerror(errno)))
				    goto EXIT;
			}
		}

		/* Assign the token to the array */
		g_data->exclude[len] = NULL;		
		if (!(g_data->exclude[len-1] = strdup(token))) {
			if (!report_error(g_data, FATAL, "parse_exclude: strdup: %s\n", strerror(errno)))
			    goto EXIT;
		}
		
		if (token)
		    free(token);
		token = NULL;

	}

	return 1;

EXIT:
	if (token)
	    free(token);
	token = NULL;

	return 0;
}

/* Parse Database Paths.
 * 
 * Concatenate all database paths into one string and validate each database.
 *
 */
int parse_userdb(struct g_data_s *g_data, char *dblist)
{
	char *tmp_ptr = NULL;
	struct stat db_stat;
	int last_sgid = 0;
	int ret = 1;
	char *ptr1 = NULL;
	char *ptr2 = NULL;
	int i_pos = 0;
	int list_len = 0;
	gid_t db_gid = -1;
	int ret_val = 0;
	int duplicate = 0;
	int d = 0;

	/* Make sure dblist is not empty */
	if (!dblist || strlen(dblist) == 0) {
		//report_error(g_data, WARNING, "parse_userdb: database string is empty.\n");
		goto EXIT;
	}

	/* Check how many paths are currently in the string. */
	list_len = 1;
	ptr1 = dblist;
	while ((ptr1 = strchr(ptr1+1, ':')))
	    list_len += 1;

	if (!(g_data->input_db = init_input_db(g_data, list_len))) {
		ret = 0;
		goto EXIT;
	}

	/* Find next position in input_db */
	i_pos = 0;
	while (g_data->input_db[i_pos])
	    i_pos += 1;
	
	/* Parse dblist */
	ptr1 = dblist;

	while (ptr1 && *ptr1) {
		if (*ptr1 == ':')
		    ptr1 += 1;

		if (!(ptr2 = strchr(ptr1, ':'))) {
			ptr2 = dblist+strlen(dblist);
		}

		/* Don't worry about blank entries */
		if (ptr2-ptr1 > 0) {
			/* Check for duplicates */
			duplicate = 0;
			for (d = 0; g_data->input_db[d]; d += 1) {
				if (!strncmp(g_data->input_db[d], ptr1, ptr2-ptr1)) {
					duplicate = 1;
					break;
				}
			}
			if (!duplicate) {
				if (!(g_data->input_db[i_pos] = sl_strndup(ptr1, ptr2-ptr1))) {
					ret = 0;
					goto EXIT;
				}			
				i_pos += 1;
			}
		}

		if (*ptr2 == ':') {
			ptr1 = ptr2 + 1;
		} else
		    ptr1 = NULL;
	}
		

	// DDD
	//for (i_pos = 0; g_data->input_db[i_pos]; i_pos += 1) {
	//	printf("IDB: *%s*\n", g_data->input_db[i_pos]);
	//}
	
	db_gid = get_gid(g_data, DB_GROUP, &ret_val);
	if (!ret_val) {
		ret = 0;
		goto EXIT;
	}

	last_sgid = 0;
	/* Sort sgid slocate db's to the top */
	for (i_pos = 0; g_data->input_db[i_pos]; i_pos += 1) {
		
		if (stat(g_data->input_db[i_pos], &db_stat) == -1) {
			report_error(g_data, FATAL, "Could not find user database '%s':  %s\n", g_data->input_db[i_pos], strerror(errno));
			ret = 0;
			goto EXIT;
		}
		
		if (db_stat.st_gid == db_gid) {

			if (i_pos != last_sgid) {
				tmp_ptr = g_data->input_db[last_sgid];
				g_data->input_db[last_sgid] = g_data->input_db[i_pos];
				g_data->input_db[i_pos] = tmp_ptr;
			}
			
			last_sgid += 1;
		}
		
	}
	
	// DDD
	//for (i = 0; g_data->input_db[i]; i++)
	//    printf("SDB: *%s*\n", g_data->input_db[i]);
EXIT:

	return ret;
}

char **parse_search_str(struct g_data_s *g_data, char **argv, int str_pos)
{
	int i = 0;
	int len = 0;
	int o = 0;
	char **search_str = NULL;
	
	i = str_pos;
	len = 0;
	while (argv[i]) {
		len += 1;
		i += 1;
	}
	
	if (i > 0) {
		if (!(search_str = malloc(sizeof(char *)*(len+1)))) {
			report_error(g_data, FATAL, "parse_cmds: search string: malloc: %s\n", strerror(errno));
			goto EXIT;
		}

		for (i = 0; i <= len; i++)
		    search_str[i] = NULL;

		o = str_pos;
		i = 0;
		while (argv[o]) {
			if (!(search_str[i] = strdup(argv[o]))) {
				report_error(g_data, FATAL, "parse_cmds: search string: strdup: %s\n", strerror(errno));
				goto EXIT;
			}
			
			o += 1;
			i += 1;
		}
	}

	return search_str;

EXIT:
	if (search_str)
	    free(search_str);
	search_str = NULL;
	
	return NULL;
}

/* Set the Output DB */
int set_output_db(struct g_data_s *g_data, char *output_db)
{
	int ret = 1;

	if (g_data->output_db) {
		free(g_data->output_db);
		g_data->output_db = NULL;
	}

	if (!(g_data->output_db = make_absolute_path(g_data, output_db))) {
		report_error(g_data, FATAL, "set_output_db: make_absolute_path(): path was returned NULL.\n");
		ret = 0;
		goto EXIT;
	}

EXIT:
	return ret;
}

/* Set the regexp_data */
int set_regexp_data(struct g_data_s *g_data, char *pattern)
{
	int ret = 1;
	
	/* If already set, override with new settings */
	if (g_data->regexp_data) {
		if (g_data->regexp_data->pattern) {
			free(g_data->regexp_data->pattern);
			g_data->regexp_data->pattern = NULL;
		}
		if (g_data->regexp_data->preg) {
			regfree(g_data->regexp_data->preg);
			g_data->regexp_data->preg = NULL;
		}
		free(g_data->regexp_data);
		g_data->regexp_data = NULL;
	}
	
	if (!(g_data->regexp_data = malloc(sizeof(struct regexp_data_s *)))) {
		report_error(g_data, FATAL, "set_regexp_data: regexp_data: malloc: %s\n", strerror(errno));
		ret = 0;
		goto EXIT;
	}
	
	g_data->regexp_data->preg = NULL;
	g_data->regexp_data->pattern = NULL;

	if (!(g_data->regexp_data->pattern = strdup(pattern))) {
		report_error(g_data, FATAL, "set_regexp_data: pattern: strdup: %s\n", strerror(errno));
		ret = 0;
		goto EXIT;
	}

EXIT:
	return ret;
}


/* Parse Dash */
/* ret code: 0 - error
 *           1 - success
 *           2 - success but exit nicely */
int parse_dash(struct g_data_s *g_data, char *option)
{
	char *ptr = NULL;
	int ret = 1;
	/* Upper Case Option */
	char *uc_option = NULL;

	if (!option) {
		report_error(g_data, FATAL, "parse_dash: 'option' variable == NULL.\n");
		ret = 0;
		goto EXIT;
	}

	/* Duplicate option string so we don't harm optarg */
	if (!(uc_option = strdup(option))) {
		report_error(g_data, FATAL, "parse_dash: strdup: %s\n", strerror(errno));
		ret = 0;
		goto EXIT;
	}

	for (ptr = uc_option; *ptr != 0 && *ptr != '='; ptr++)
	    *ptr = toupper(*ptr);

	if (strcmp(uc_option, "HELP") == 0) {
		usage(g_data);
		ret = 2;
		goto EXIT;
	} else if (strcmp(uc_option, "VERSION") == 0) {
		printf("%s\n", SL_VERSION);
		ret = 2;
		goto EXIT;
	} else if (strcmp(uc_option, "VERBOSE") == 0) {
		g_data->VERBOSE = TRUE;
	} else if (strcmp(uc_option, "INITDIFFDB") == 0) {
		g_data->INITDIFFDB = TRUE;
	} else if (strcmp(uc_option, "FAST-UPDATE") == 0) {
                g_data->FAST_UPDATE = TRUE;
        } else if (strcmp(uc_option, "FULL-UPDATE") == 0) {
                g_data->FULL_UPDATE = TRUE;

	}

	if (*ptr == '=') {
		*ptr = '\0';
		ptr++;
		
		if (strcmp(uc_option, "OUTPUT") == 0) {
			if (!set_output_db(g_data, ptr)) {
				ret = 0;
				goto EXIT;
			}
		} else if (strcmp(uc_option,"DATABASE") == 0) {
			if (!parse_userdb(g_data, ptr)) {
				ret = 0;
				goto EXIT;
			}
		} else if (strcmp(uc_option,"REGEXP") == 0) {
			if (!set_regexp_data(g_data, ptr)) {
				ret = 0;
				goto EXIT;
			}
		}
	}

	ptr = NULL;

EXIT:

	if (uc_option)
	    free(uc_option);
	uc_option = NULL;

	return ret;
}

/* Parse File System Type Exclusion */
int parse_fs_exclude(struct g_data_s *g_data, char *data_str)
{
	char *fs_str = NULL;
	int ret = 0;
	int exclude_str_len = 0;
	char *fbuf=NULL;
	char *head_ptr;
	char *tail_ptr;
	char *exclude_str=NULL;
	int fd = -1;
	int i = 0;
	int matched = 0;
	char *match_ptr = NULL;

	if (!data_str) {
		report_error(g_data, FATAL, "parse_fs_exclude: data_str == NULL\n");
		goto EXIT;
	}

	/* Duplicate so that we can change the case */
	if (!(fs_str = strdup(data_str))) {
		report_error(g_data, FATAL, "parse_fs_exclude: fs_str: malloc: %s\n", strerror(errno));
		goto EXIT;
	}

	for (i = 0; fs_str[i]; i++)
	    fs_str[i] = toupper(fs_str[i]);

	/* Load the mtab file */
	if (!load_file(g_data, MTAB_FILE, &fbuf)) {
		report_error(g_data, FATAL, "parse_fs_exclude: Could not load file data: %s\n", MTAB_FILE);
		goto EXIT;
	}

	head_ptr = fbuf;
	while (head_ptr) {
		/* find filesystem type */
		if ((head_ptr = strchr(head_ptr,' '))) {
			head_ptr += 1;
			head_ptr = strchr(head_ptr,' ');
		}
		
		if (!head_ptr)
		    continue;
		
		head_ptr += 1;
		
		tail_ptr = strchr(head_ptr,' ');
		if (!tail_ptr) {
			head_ptr = NULL;
			continue;
		}
		
		*tail_ptr = 0;
		/* Upper case string */
		for (i = 0; head_ptr[i]; i++)
		    head_ptr[i] = toupper(head_ptr[i]);
				
		/* Check if file sytem type exists in exclude string */
		matched = 0;
		if ((match_ptr = strstr(fs_str, head_ptr))) {
			if ((match_ptr == fs_str || *(match_ptr-1) == ',') &&
			    (*(match_ptr+(tail_ptr-head_ptr)) == ',' || *(match_ptr+(tail_ptr-head_ptr)) == '\0'))
			    matched = 1;
		}
		
		*tail_ptr = ' ';
		
		if (matched) {
			/* go backwards a bit so that we can get the
			 * mount point of the filesystem */
			head_ptr -= 2;
			
			while (*head_ptr != ' ' && head_ptr != fbuf)
			    head_ptr -= 1;
			
			if (head_ptr == fbuf) {
				report_error(g_data, FATAL, "parse_fs_exclude: File System Exclude: (1) corrupt mtab file: %s\n", MTAB_FILE);
				goto EXIT;
			}
			
			head_ptr += 1;
			
			if (!(tail_ptr = strchr(head_ptr,' '))) {
				report_error(g_data, FATAL, "parse_fs_exclude: File System Exclude: (2) corrupt mtab file: %s\n", MTAB_FILE);
				goto EXIT;
			}
			
			*tail_ptr = 0;
			
			/* +1 for the extra "," delimiter */
			exclude_str_len += strlen(head_ptr)+1;
			exclude_str = realloc(exclude_str, exclude_str_len+1);
			
			if (exclude_str_len == strlen(head_ptr)+1)
			    *exclude_str = 0;
			else
			    strcat(exclude_str, ",");
			strcat(exclude_str, head_ptr);
			
			*tail_ptr = ' ';
		}


		head_ptr = strchr(head_ptr,'\n');
	}
	
	if (exclude_str) {
		if (!parse_exclude(g_data, exclude_str))
		    goto EXIT;
	}
		
	ret = 1;
	
EXIT:

	if (fd != -1) {
		if (close(fd) == -1) {
			report_error(g_data, FATAL, "parse_fs_exclude: close: %s: %s\n", MTAB_FILE, strerror(errno));
			goto EXIT;
		}
	}

	if (exclude_str) {
		free(exclude_str);
		exclude_str = NULL;
	}
	
	if (fbuf) {
		free(fbuf);
		fbuf = NULL;
	}
	
	if (fs_str) {
		free(fs_str);
		fs_str = NULL;
	}
	
	return ret;
}

/* Parse command line options */
struct cmd_data_s * parse_cmds(struct g_data_s *g_data, int argc, char **argv)
{
	struct cmd_data_s *cmd_data = NULL;
	int ch;
	int i = 0;
	int reg_ret = 0;
	char regex_errbuf[1024];
	char *ENV_locate_path = NULL;
	int add_default_db = 1;
	int dash_ret = 0;

	if (!(cmd_data = init_cmd_data(g_data)))
	    goto EXIT;
	
	if (strcmp(g_data->progname, "updatedb") == 0)
	    cmd_data->updatedb = TRUE;

	while ((ch = getopt(argc,argv,"VvuhqU:r:o:e:l:d:-:n:f:c:i")) != EOF) {
		switch(ch) {
			/* Help */
		 case 'h':
			usage(g_data);
			cmd_data->exit_but_nice = 1;
			goto EXIT;
			break;
			/* Quiet Mode. Don't print warnings or errors. */
		 case 'q':
			/* We set g_data since this is the only place it is
			 * called from */
			g_data->QUIET = TRUE;
			break;
		 case 'V':
			printf("%s\n", SL_VERSION);
			cmd_data->exit_but_nice = 1;
			goto EXIT;
			break;
			/* Turn VERBOSE mode ON */
		 case 'v':
			g_data->VERBOSE = TRUE;
			break;
			/* Exclude specified directories from database */
		 case 'e':
			if (!parse_exclude(g_data, optarg))
			    goto EXIT;
			break;

			/* Set the security level of the database when creating or updating the
			 * database. If set to 0, security checks will not be preformed */
		 case 'l':
			if (optarg[0] != '0' && optarg[0] != '1') {
				report_error(g_data, FATAL,"Security level must be 0 or 1.\n");
				goto EXIT;
			}

			g_data->slevel = optarg[0];
			break;

			/* Index from the root '/' path */
		 case 'u':
			cmd_data->updatedb = TRUE;
			add_default_db = 0;
			break;

			/* Index from a specific path */
		 case 'U':
			/* TODO: Parse multiple paths to index */
			cmd_data->updatedb = TRUE;
			if (g_data->index_path)
			    free(g_data->index_path);
			g_data->index_path = strdup(optarg);
			add_default_db = 0;
			break;
		 case 'c':
			if (cmd_data->updatedb_conf)
			    free(cmd_data->updatedb_conf);
			cmd_data->updatedb_conf = strdup(optarg);
			break;
			/* Search database with specified regular expression */
		 case 'r':
			if (!set_regexp_data(g_data, optarg))
			    goto EXIT;

			break;
			
			/* Specify the database to search in */
		 case 'd':
			add_default_db = 0;
			if (!parse_userdb(g_data, optarg))
			    goto EXIT;
			break;

			/* Specify the database path to write to when creating or updating a database */
		 case 'o':
			// XXX
			//if (!o_OPT_READY)
			//  report_error(FATAL,QUIET,"%s: Must specify an 'Update' database option first.\n",progname);
			if (!set_output_db(g_data, optarg))
			    goto EXIT;

			break;

			/* Limit the amount of search results */
		 case 'n':
			/* Make sure it is a digit */
			for (i=0; i < strlen(optarg); i+=1) {
				if (!isdigit(optarg[i]) && optarg[i] != '-') {
					report_error(g_data, FATAL, "Argument 'n': '%s': value must be an integer.\n", optarg);
					goto EXIT;
				}
			}
			g_data->queries = atoi(optarg);
			if (g_data->queries < 0)
			    report_error(g_data, WARNING, "Argument 'n': '%d': value should be greater than 0.\n", g_data->queries);
			break;

			/* Make search case insensitive */
		 case 'i':
			g_data->nocase = 1;
			break;
			/* Exclude by filesystem */
                 case 'I':
                        g_data->INITDIFFDB = TRUE;
                        break;
		 case 'f':
			if (!parse_fs_exclude(g_data, optarg))
			    goto EXIT;
			break;
		 case '-':
			dash_ret = parse_dash(g_data, optarg);
			if (!dash_ret)
			    goto EXIT;
			else if (dash_ret == 2) {
				cmd_data->exit_but_nice = 1;
				goto EXIT;
			}
			break;
		 default:
			break;
		}
	}

	/* Get search strings, unless a regular expression was specified */
	if (!g_data->regexp_data)
	    cmd_data->search_str = parse_search_str(g_data, argv, optind);
	else {
		/* Initialize preg here so that regcomp can initialize it incase an error occurs */
		if (!(g_data->regexp_data->preg = malloc(sizeof(regex_t)))) {
			report_error(g_data, FATAL, "parse_cmds: Argument 'r': preg: malloc: %s\n", strerror(errno));
			goto EXIT;
		}
		
		/* We compile the regexp here so that g_data->nocase will be determined */
		if ((reg_ret = regcomp(g_data->regexp_data->preg, g_data->regexp_data->pattern, g_data->nocase?REG_ICASE:0)) != 0) {
			regerror(reg_ret, g_data->regexp_data->preg, regex_errbuf, 1024);
			report_error(g_data, FATAL, "match: regular expression: %s\n", regex_errbuf);
			goto EXIT;
		}
	}

	if (!cmd_data->updatedb) {
		/* Parse environment variables */
		if ((ENV_locate_path = getenv("LOCATE_PATH"))) {
			if (!parse_userdb(g_data, getenv("LOCATE_PATH")))
			    goto EXIT;
		}
		
		if (add_default_db) {
			if (!parse_userdb(g_data, DEFAULT_DB))
			    goto EXIT;
		}
	} else
	    parse_updatedb(g_data, cmd_data->updatedb_conf);

	return cmd_data;

EXIT:
	if (cmd_data->exit_but_nice)
	    return cmd_data;
	
	free_cmd_data(cmd_data);
	cmd_data = NULL;

	return NULL;
}

