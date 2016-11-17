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

#ifndef __SLOCATE_H
#define __SLOCATE_H
#include <sys/types.h>
#include <regex.h>

#define VERSION "0.5.4"
#define SL_RELEASE "December  2, 2006"
#define SL_VERSION "rlocate " VERSION " - Released " SL_RELEASE

#define FALSE 0
#define TRUE 1

/* Warn if a database is older than this.  8 days allows for a weekly
 *    update that takes up to a day to perform.  */
#define WARN_SECONDS (60 * 60 * 24 * 8)

/* Printable version of WARN_SECONDS.  */
#define WARN_MESSAGE "8 days"

#define MTAB_FILE "/etc/mtab"
#define UPDATEDB_FILE UPDATEDB_CONF

/* More fitting paths for FreeBSD -matt */
#if defined(__FreeBSD__)
# define DEFAULT_DB "/var/db/slocate/slocate.db"
# define DEFAULT_DB_DIR "/var/db/slocate/"
#elif defined(__SunOS__)
# define DEFAULT_DB "/var/db/slocate/slocate.db"
# define DEFAULT_DB_DIR "/var/db/slocate/"
#else
# define DEFAULT_DB RLOCATE_DB 
# define DEFAULT_DB_DIR RLOCATE_DB_DIR
#endif

#define DB_UID 0
#define DB_GROUP RLOCATE_GRP
#define DB_MODE 00640

#define SLOC_ESC -0x80

/* Number of bytes to read in at a time from DB when searching */
#define BLOCK_SIZE 4096

#define GRANT_ACCESS '0'
#define VERIFY_ACCESS '1'

/* Regexp data */
struct regexp_data_s {
	char *pattern;
	regex_t *preg;
};

/* Global Data */
struct g_data_s {
	char *progname;
	int QUIET;
	int VERBOSE;
	char slevel;
	int nocase;
	char *index_path;
	uid_t uid;
	gid_t gid;
	gid_t SLOCATE_GID;
	char *output_db;
	char **exclude;
	char **input_db;
	int queries;
	struct regexp_data_s *regexp_data;
	int INITDIFFDB;
	int FULL_UPDATE;
	int FAST_UPDATE;
};

/* Encoding data */
struct enc_data_s {
	char *prev_line;
	short prev_len;
};

/* Decoding data */
struct dec_data_s {
	char *path_head;
	char *full_path;
	char *code_str;
	char *prev_code_str;
	char slevel;
	int b;
	int STATE;
	short code_num;
	int search;
};

void free_global_data(struct g_data_s * g_data);

/* Decode states */
#define DC_NONE 0
#define DC_CODE 1
#define DC_ESC 2
#define DC_ESC_INTR 3
#define DC_DATA 4
#define DC_DATA_INTR 5

/* Function declarations */

char **init_input_db(struct g_data_s *g_data, int len);

#endif
