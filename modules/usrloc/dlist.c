/*
 * $Id$
 *
 * List of registered domains
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * ========
 * 2005-07-11 get_all_ucontacts returns also the contact's flags (bogdan)
 * 2006-11-28 added get_number_of_users() (Jeffrey Magder - SOMA Networks)
 * 2007-09-12 added partitioning support for fetching all ul contacts
 *            (bogdan)
 */

/*! \file
 *  \brief USRLOC - List of registered domains
 *  \ingroup usrloc
 */


#include "dlist.h"
#include <stdlib.h>	       /* abort */
#include <string.h>            /* strlen, memcmp */
#include <stdio.h>             /* printf */
#include "../../ut.h"
#include "../../db/db_ut.h"
#include "../../mem/shm_mem.h"
#include "../../dprint.h"
#include "../../ip_addr.h"
#include "../../socket_info.h"
#include "udomain.h"           /* new_udomain, free_udomain */
#include "utime.h"
#include "ul_mod.h"


/*! \brief
 * List of all registered domains
 */
dlist_t* root = 0;


/*! \brief
 * Returned the first udomain if input param in NULL or the next following
 * udomain after the given udomain
 */
udomain_t* get_next_udomain(udomain_t *_d)
{
	dlist_t *it;

	if (_d==NULL)
		return root->d;

	for( it=root ; it ; it=it->next)
		if (it->d == _d) return (it->next==NULL)?NULL:it->next->d ;
	return NULL;
}


/*! \brief
 * Find domain with the given name
 * \return 0 if the domain was found
 * and 1 of not
 */
static inline int find_dlist(str* _n, dlist_t** _d)
{
	dlist_t* ptr;

	ptr = root;
	while(ptr) {
		if ((_n->len == ptr->name.len) &&
		    !memcmp(_n->s, ptr->name.s, _n->len)) {
			*_d = ptr;
			return 0;
		}
		
		ptr = ptr->next;
	}
	
	return 1;
}



static inline int get_all_db_ucontacts(void *buf, int len, unsigned int flags,
								unsigned int part_idx, unsigned int part_max)
{
	static char query_buf[512];
	static str query_str;

	struct socket_info *sock;
	unsigned int dbflags;
	db_res_t* res = NULL;
	db_row_t *row;
	dlist_t *dom;
	char *p, *p1;
	char now_s[25];
	int now_len;
	int port, proto, p_len, p1_len;
	str host;
	int i;
	void *cp;
	int shortage, needed;

	cp = buf;
	shortage = 0;
	/* Reserve space for terminating 0000 */
	len -= sizeof(p_len);

	/* get the current time in DB format */
	now_len = 25;
	if (db_time2str( time(0), now_s, &now_len)!=0) {
		LM_ERR("failed to print now time\n");
		return -1;
	}

	for (dom = root; dom!=NULL ; dom=dom->next) {
		/* build query */
		i = snprintf( query_buf, sizeof(query_buf), "select %.*s, %.*s, %.*s,"
#ifdef ORACLE_USRLOC
			" %.*s, %.*s from %s where %.*s > %.*s and "
			"bitand(%.*s, %d) = %d and mod(id, %u) = %u",
#else
			" %.*s, %.*s from %s where %.*s > %.*s and %.*s & %d = %d and "
			"id %% %u = %u",
#endif
			received_col.len, received_col.s,
			contact_col.len, contact_col.s,
			sock_col.len, sock_col.s,
			cflags_col.len, cflags_col.s,
			path_col.len, path_col.s,
			dom->d->name->s,
			expires_col.len, expires_col.s,
			now_len, now_s,
			cflags_col.len, cflags_col.s,
			flags, flags, part_max, part_idx);
		if ( i>=sizeof(query_buf) ) {
			LM_ERR("DB query too long\n");
			return -1;
		}
		query_str.s = query_buf;
		query_str.len = i;
		if ( ul_dbf.raw_query( ul_dbh, &query_str, &res)<0 ) {
			LM_ERR("raw_query failed\n");
			return -1;
		}
		if( RES_ROW_N(res)==0 ) {
			ul_dbf.free_result(ul_dbh, res);
			continue;
		}

		for(i = 0; i < RES_ROW_N(res); i++) {
			row = RES_ROWS(res) + i;

			/* received */
			p = (char*)VAL_STRING(ROW_VALUES(row));
			if ( VAL_NULL(ROW_VALUES(row)) || p==0 || p[0]==0 ) {
				/* contact */
				p = (char*)VAL_STRING(ROW_VALUES(row)+1);
				if (VAL_NULL(ROW_VALUES(row)+1) || p==0 || p[0]==0) {
					LM_ERR("empty contact -> skipping\n");
					continue;
				}
			}
			p_len = strlen(p);

			/* path */
			p1 = (char*)VAL_STRING(ROW_VALUES(row)+4);
			if (VAL_NULL(ROW_VALUES(row)+4) || p1==0 || p1[0]==0){
				p1 = NULL;
				p1_len = 0;
			} else {
				p1_len = strlen(p1);
			}

			needed = (int)(sizeof(p_len)+p_len+sizeof(sock)+sizeof(dbflags)+
				sizeof(p1_len)+p1_len);
			if (len < needed) {
				shortage += needed ;
				continue;
			}

			/* write received/contact */
			memcpy(cp, &p_len, sizeof(p_len));
			cp = (char*)cp + sizeof(p_len);
			memcpy(cp, p, p_len);
			cp = (char*)cp + p_len;

			/* sock */
			p  = (char*)VAL_STRING(ROW_VALUES(row) + 2);
			if (VAL_NULL(ROW_VALUES(row)+2) || p==0 || p[0]==0){
				sock = 0;
			} else {
				if (parse_phostport( p, strlen(p), &host.s, &host.len,
				&port, &proto)!=0) {
					LM_ERR("bad socket <%s>...ignoring\n", p);
					sock = 0;
				} else {
					sock = grep_sock_info( &host, (unsigned short)port, proto);
					if (sock==0) {
						LM_DBG("non-local socket <%s>...ignoring\n", p);
					}
				}
			}

			/* flags */
			dbflags = VAL_BITMAP(ROW_VALUES(row) + 3);

			/* write sock and flags */
			memcpy(cp, &sock, sizeof(sock));
			cp = (char*)cp + sizeof(sock);
			memcpy(cp, &dbflags, sizeof(dbflags));
			cp = (char*)cp + sizeof(dbflags);

			/* write path */
			memcpy(cp, &p1_len, sizeof(p1_len));
			cp = (char*)cp + sizeof(p1_len);
			memcpy(cp, p1, p1_len);
			cp = (char*)cp + p1_len;

			len -= needed;
		} /* row cycle */

		ul_dbf.free_result(ul_dbh, res);
	} /* domain cycle */

	/* len < 0 is possible, if size of the buffer < sizeof(c->c.len) */
	if (len >= 0)
		memset(cp, 0, sizeof(p_len));

	/* Shouldn't happen */
	if (shortage > 0 && len > shortage) {
		abort();
	}

	shortage -= len;

	return shortage > 0 ? shortage : 0;
}



static inline int get_all_mem_ucontacts(void *buf, int len, unsigned int flags,
								unsigned int part_idx, unsigned int part_max)
{
	dlist_t *p;
	urecord_t *r;
	ucontact_t *c;
	void *cp;
	void **dest;
	map_iterator_t it;
	int shortage;
	int needed;
	int count;
	int i = 0;
	cp = buf;
	shortage = 0;
	/* Reserve space for terminating 0000 */
	len -= sizeof(c->c.len);

	for (p = root; p != NULL; p = p->next) {

		for(i=0; i<p->d->size; i++) {

			if ( (i % part_max) != part_idx )
				continue;

			lock_ulslot( p->d, i);
			count = map_size(p->d->table[i].records);

			if( count <= 0 )
			{
				unlock_ulslot(p->d, i);
				continue;
			}

			for ( map_first( p->d->table[i].records, &it);
				iterator_is_valid(&it);
				iterator_next(&it) ) {

				
				dest = iterator_val(&it);
				if( dest == NULL )
					return -1;
				r =( urecord_t * ) *dest;



				for (c = r->contacts; c != NULL; c = c->next) {
					if (c->c.len <= 0)
						continue;
					/*
					 * List only contacts that have all requested
					 * flags set
					 */
					if ((c->cflags & flags) != flags)
						continue;
					if (c->received.s) {
						needed = (int)(sizeof(c->received.len)
								+ c->received.len + sizeof(c->sock)
								+ sizeof(c->cflags) + sizeof(c->path.len)
								+ c->path.len);
						if (len >= needed) {
							memcpy(cp,&c->received.len,sizeof(c->received.len));
							cp = (char*)cp + sizeof(c->received.len);
							memcpy(cp, c->received.s, c->received.len);
							cp = (char*)cp + c->received.len;
							memcpy(cp, &c->sock, sizeof(c->sock));
							cp = (char*)cp + sizeof(c->sock);
							memcpy(cp, &c->cflags, sizeof(c->cflags));
							cp = (char*)cp + sizeof(c->cflags);
							memcpy(cp, &c->path.len, sizeof(c->path.len));
							cp = (char*)cp + sizeof(c->path.len);
							memcpy(cp, c->path.s, c->path.len);
							cp = (char*)cp + c->path.len;
							len -= needed;
						} else {
							shortage += needed;
						}
					} else {
						needed = (int)(sizeof(c->c.len) + c->c.len +
							sizeof(c->sock) + sizeof(c->cflags) +
							sizeof(c->path.len) + c->path.len);
						if (len >= needed) {
							memcpy(cp, &c->c.len, sizeof(c->c.len));
							cp = (char*)cp + sizeof(c->c.len);
							memcpy(cp, c->c.s, c->c.len);
							cp = (char*)cp + c->c.len;
							memcpy(cp, &c->sock, sizeof(c->sock));
							cp = (char*)cp + sizeof(c->sock);
							memcpy(cp, &c->cflags, sizeof(c->cflags));
							cp = (char*)cp + sizeof(c->cflags);
							memcpy(cp, &c->path.len, sizeof(c->path.len));
							cp = (char*)cp + sizeof(c->path.len);
							memcpy(cp, c->path.s, c->path.len);
							cp = (char*)cp + c->path.len;
							len -= needed;
						} else {
							shortage += needed;
						}
					}
				}
			}
			unlock_ulslot(p->d, i);
		}
	}
	/* len < 0 is possible, if size of the buffer < sizeof(c->c.len) */
	if (len >= 0)
		memset(cp, 0, sizeof(c->c.len));

	/* Shouldn't happen */
	if (shortage > 0 && len > shortage) {
		abort();
	}

	shortage -= len;

	return shortage > 0 ? shortage : 0;
}



/*! \brief
 * Return list of all contacts for all currently registered
 * users in all domains. Caller must provide buffer of
 * sufficient length for fitting all those contacts. In the
 * case when buffer was exhausted, the function returns
 * estimated amount of additional space needed, in this
 * case the caller is expected to repeat the call using
 * this value as the hint.
 *
 * Information is packed into the buffer as follows:
 *
 * +------------+----------+-----+------+-----+
 * |contact1.len|contact1.s|sock1|flags1|path1|
 * +------------+----------+-----+------+-----+
 * |contact2.len|contact2.s|sock2|flags2|path1|
 * +------------+----------+-----+------+-----+
 * |..........................................|
 * +------------+----------+-----+------+-----+
 * |contactN.len|contactN.s|sockN|flagsN|pathN|
 * +------------+----------+-----+------+-----+
 * |000000000000|
 * +------------+
 */
int get_all_ucontacts(void *buf, int len, unsigned int flags,
								unsigned int part_idx, unsigned int part_max)
{
	if (db_mode==DB_ONLY)
		return get_all_db_ucontacts( buf, len, flags, part_idx, part_max);
	else
		return get_all_mem_ucontacts( buf, len, flags, part_idx, part_max);
}



/*! \brief
 * Create a new domain structure
 * \return 0 if everything went OK, otherwise value < 0 is returned
 *
 * \note The structure is NOT created in shared memory so the
 * function must be called before ser forks if it should
 * be available to all processes
 */
static inline int new_dlist(str* _n, dlist_t** _d)
{
	dlist_t* ptr;

	/* Domains are created before ser forks,
	 * so we can create them using pkg_malloc
	 */
	ptr = (dlist_t*)shm_malloc(sizeof(dlist_t));
	if (ptr == 0) {
		LM_ERR("no more share memory\n");
		return -1;
	}
	memset(ptr, 0, sizeof(dlist_t));

	/* copy domain name as null terminated string */
	ptr->name.s = (char*)shm_malloc(_n->len+1);
	if (ptr->name.s == 0) {
		LM_ERR("no more memory left\n");
		shm_free(ptr);
		return -2;
	}

	memcpy(ptr->name.s, _n->s, _n->len);
	ptr->name.len = _n->len;
	ptr->name.s[ptr->name.len] = 0;

	if (new_udomain(&(ptr->name), ul_hash_size, &(ptr->d)) < 0) {
		LM_ERR("creating domain structure failed\n");
		shm_free(ptr->name.s);
		shm_free(ptr);
		return -3;
	}

	*_d = ptr;
	return 0;
}


/*! \brief
 * Function registers a new domain with usrloc
 * if the domain exists, pointer to existing structure
 * will be returned, otherwise a new domain will be
 * created
 */
int register_udomain(const char* _n, udomain_t** _d)
{
	dlist_t* d;
	str s;
	db_con_t* con;

	s.s = (char*)_n;
	s.len = strlen(_n);

	if (find_dlist(&s, &d) == 0) {
		*_d = d->d;
		return 0;
	}
	
	if (new_dlist(&s, &d) < 0) {
		LM_ERR("failed to create new domain\n");
		return -1;
	}

	/* Test tables from database if we are gonna
	 * to use database
	 */
	if (db_mode != NO_DB) {
		con = ul_dbf.init(&db_url);
		if (!con) {
			LM_ERR("failed to open database connection\n");
			goto err;
		}

		if(db_check_table_version(&ul_dbf, con, &s, UL_TABLE_VERSION) < 0) {
			LM_ERR("error during table version check.\n");
			goto err;
		}
		/* test if DB really exists */
		if (testdb_udomain(con, d->d) < 0) {
			LM_ERR("testing domain '%.*s' failed\n", s.len, ZSW(s.s));
			goto err;
		}

		ul_dbf.close(con);
	}

	d->next = root;
	root = d;
	
	*_d = d->d;
	return 0;

err:
	if (con) ul_dbf.close(con);
	free_udomain(d->d);
	shm_free(d->name.s);
	shm_free(d);
	return -1;
}


/*! \brief
 * Free all allocated memory
 */
void free_all_udomains(void)
{
	dlist_t* ptr;

	while(root) {
		ptr = root;
		root = root->next;

		free_udomain(ptr->d);
		shm_free(ptr->name.s);
		shm_free(ptr);
	}
}


/*! \brief
 * Just for debugging
 */
void print_all_udomains(FILE* _f)
{
	dlist_t* ptr;
	
	ptr = root;

	fprintf(_f, "===Domain list===\n");
	while(ptr) {
		print_udomain(_f, ptr->d);
		ptr = ptr->next;
	}
	fprintf(_f, "===/Domain list===\n");
}


/*! \brief
 *  Loops through all domains summing up the number of users. 
 */
unsigned long get_number_of_users(void* foo)
{
	int numberOfUsers = 0;

	dlist_t* current_dlist;
	
	current_dlist = root;

	while (current_dlist)
	{
		numberOfUsers += get_stat_val(current_dlist->d->users); 
		current_dlist  = current_dlist->next;
	}

	return numberOfUsers;
}


/*! \brief
 * Run timer handler of all domains
 */
int synchronize_all_udomains(void)
{
	int res = 0;
	dlist_t* ptr;

	get_act_time(); /* Get and save actual time */

	if (db_mode==DB_ONLY) {
		for( ptr=root ; ptr ; ptr=ptr->next)
			res |= db_timer_udomain(ptr->d);
	} else {
		for( ptr=root ; ptr ; ptr=ptr->next)
			res |= mem_timer_udomain(ptr->d);
	}

	return res;
}


/*! \brief
 * Find a particular domain
 */
int find_domain(str* _d, udomain_t** _p)
{
	dlist_t* d;

	if (find_dlist(_d, &d) == 0) {
	        *_p = d->d;
		return 0;
	}

	return 1;
}
