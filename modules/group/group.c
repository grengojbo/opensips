/*
 * $Id$
 *
 * Group membership
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 * 2003-02-25 - created by janakj
 * 2004-06-07   updated to the new DB api, added group_db_{bind,init,close,ver}
 *               (andrei)
 *
 */


#include <string.h>
#include "../../dprint.h"               /* Logging */
#include "../../db/db.h"                /* Generic database API */
#include "../../ut.h"
#include "../../parser/digest/digest.h" /* get_authorized_cred */
#include "../../parser/hf.h"            /* Header Field types */
#include "../../parser/parse_from.h"    /* From parser */
#include "../../parser/parse_uri.h"
#include "group.h"
#include "group_mod.h"                   /* Module parameters */



/*
 * Get Request-URI
 */
static inline int get_request_uri(struct sip_msg* _m, str* _u)
{
	if (_m->new_uri.s) {
		_u->s = _m->new_uri.s;
		_u->len = _m->new_uri.len;
	} else {
		_u->s = _m->first_line.u.request.uri.s;
		_u->len = _m->first_line.u.request.uri.len;
	}

	return 0;
}


/*
 * Get To header field URI
 */
static inline int get_to_uri(struct sip_msg* _m, str* _u)
{
	if (!_m->to && ((parse_headers(_m, HDR_TO_F, 0) == -1) || (!_m->to))) {
		LOG(L_ERR, "get_to_uri(): Can't get To header field\n");
		return -1;
	}
	
	_u->s = ((struct to_body*)_m->to->parsed)->uri.s;
	_u->len = ((struct to_body*)_m->to->parsed)->uri.len;

	return 0;
}


/*
 * Get From header field URI
 */
static inline int get_from_uri(struct sip_msg* _m, str* _u)
{
	if (parse_from_header(_m) < 0) {
		LOG(L_ERR, "get_from_uri(): Error while parsing From body\n");
		return -1;
	}
	
	_u->s = ((struct to_body*)_m->from->parsed)->uri.s;
	_u->len = ((struct to_body*)_m->from->parsed)->uri.len;

	return 0;
}


int get_username_domain(struct sip_msg *msg, group_check_p gcp,
											str *username, str *domain)
{
	struct sip_uri puri;
	struct hdr_field* h;
	struct auth_body* c = 0; /* Makes gcc happy */
	xl_value_t value;
	str uri;

	uri.s = 0;
	uri.len = 0;

	switch(gcp->id) {
		case 1: /* Request-URI */
			if (get_request_uri( msg, &uri) < 0) {
				LOG(L_ERR, "ERROR:group:get_username_domain: failed to get "
					"Request-URI\n");
				return -1;
			}
			break;

		case 2: /* To */
			if (get_to_uri( msg, &uri) < 0) {
				LOG(L_ERR, "ERROR:group:get_username_domain: failed to get "
					"To URI\n");
				return -1;
			}
			break;

		case 3: /* From */
			if (get_from_uri( msg, &uri) < 0) {
				LOG(L_ERR, "ERROR:group:get_username_domain: failed to get "
					"From URI\n");
				return -1;
			}
			break;

		case 4: /* Credentials */
			get_authorized_cred( msg->authorization, &h);
			if (!h) {
				get_authorized_cred( msg->proxy_auth, &h);
				if (!h) {
					LOG(L_ERR, "ERROR:group:get_username_domain: no "
						"authorized credentials found (error in scripts)\n");
					return -1;
				}
			}
			c = (auth_body_t*)(h->parsed);
			break;

		case 5: /* AVP spec */
			if(xl_get_spec_value( msg, &gcp->sp, &value, 0)!=0 
				|| value.flags&XL_VAL_NULL || value.rs.len<=0)
			{
				LOG(L_ERR,"ERROR:group:get_username_domain: no AVP found"
					" (error in scripts)\n");
				return -1;
			}
			uri.s = value.rs.s;
			uri.len = value.rs.len;
			break;
	}

	if (gcp->id != 4) {
		if (parse_uri(uri.s, uri.len, &puri) < 0) {
			LOG(L_ERR, "ERROR:group:get_username_domain: failed to parse "
				"URI <%.*s>\n",uri.len,uri.s);
			return -1;
		}

		*username = puri.user;
		*domain = puri.host;
	} else {
		*username = c->digest.username.user;
		*domain = *(GET_REALM(&c->digest));
	}
	return 0;
}



/*
 * Check if username in specified header field is in a table
 */
int is_user_in(struct sip_msg* _msg, char* _hf, char* _grp)
{
	db_key_t keys[3];
	db_val_t vals[3];
	db_key_t col[1];
	db_res_t* res;

	keys[0] = user_column.s;
	keys[1] = group_column.s;
	keys[2] = domain_column.s;
	col[0] = group_column.s;

	if ( get_username_domain( _msg, (group_check_p)_hf, &(VAL_STR(vals)),
	&(VAL_STR(vals+2)))!=0) {
		LOG(L_ERR, "is_user_in(): Error while getting username@domain\n");
		return -1;
	}

	VAL_TYPE(vals) = VAL_TYPE(vals + 1) = VAL_TYPE(vals + 2) = DB_STR;
	VAL_NULL(vals) = VAL_NULL(vals + 1) = VAL_NULL(vals + 2) = 0;

	VAL_STR(vals + 1) = *((str*)_grp);

	if (group_dbf.use_table(group_dbh, table.s) < 0) {
		LOG(L_ERR, "is_user_in(): Error in use_table\n");
		return -5;
	}

	if (group_dbf.query(group_dbh, keys, 0, vals, col, (use_domain) ? (3): (2),
				1, 0, &res) < 0) {
		LOG(L_ERR, "is_user_in(): Error while querying database\n");
		return -5;
	}

	if (RES_ROW_N(res) == 0) {
		DBG("is_user_in(): User is not in group '%.*s'\n", 
		    ((str*)_grp)->len, ZSW(((str*)_grp)->s));
		group_dbf.free_result(group_dbh, res);
		return -6;
	} else {
		DBG("is_user_in(): User is in group '%.*s'\n", 
		    ((str*)_grp)->len, ZSW(((str*)_grp)->s));
		group_dbf.free_result(group_dbh, res);
		return 1;
	}
}


int group_db_init(char* db_url)
{
	if (group_dbf.init==0){
		LOG(L_CRIT, "BUG: group_db_bind: null dbf \n");
		goto error;
	}
	group_dbh=group_dbf.init(db_url);
	if (group_dbh==0){
		LOG(L_ERR, "ERROR: group_db_bind: unable to connect to the "
				"database\n");
		goto error;
	}
	return 0;
error:
	return -1;
}


int group_db_bind(char* db_url)
{
	if (bind_dbmod(db_url, &group_dbf)<0){
		LOG(L_ERR, "ERROR: group_db_bind: unable to bind to the database"
				" module\n");
		return -1;
	}

	if (!DB_CAPABILITY(group_dbf, DB_CAP_QUERY)) {
		LOG(L_ERR, "ERROR: group_db_bind: Database module does not implement 'query' function\n");
		return -1;
	}

	return 0;
}


void group_db_close()
{
	if (group_dbh && group_dbf.close){
		group_dbf.close(group_dbh);
		group_dbh=0;
	}
}


int group_db_ver(str* name)
{
	return table_version( &group_dbf, group_dbh, name);
}
