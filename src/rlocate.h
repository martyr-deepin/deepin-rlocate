#ifndef RLOCATE_H
#define RLOCATE_H 1

#include <fts.h>
#include <regex.h>

void rlocate_start_updatedb(struct g_data_s *g_data);
int rlocate_ftscompare(const FTSENT **e1, const FTSENT **e2);
void rlocate_end_updatedb();
void rlocate_init(struct g_data_s* g_data,
		  const char *rlocate_db, 
                  char *str, 
                  char *casestr, 
                  const int globflag);
void rlocate_printit(struct g_data_s *g_data, const char *codedpath);
void rlocate_done(struct g_data_s* g_data);
int rlocate_fast_updatedb(struct g_data_s *g_data,
			  FILE *fd_tmp, 
			  struct enc_data_s *enc_data);
int rlocate_lock(struct g_data_s* g_data);
void rlocate_unlock();

#endif /* !RLOCATE_H */
