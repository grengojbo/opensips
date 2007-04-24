/*
 * $Id$
 *
 * presence module - presence server implementation
 *
 * Copyright (C) 2006 Voice Sistem S.R.L.
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
 *  2006-08-15  initial version (anca)
 */


#include "../../ut.h"
#include "../../usr_avp.h"
#include "../../data_lump_rpl.h"
#include "../../parser/parse_expires.h"
#include "../../parser/parse_event.h"
#include "../../parser/contact/parse_contact.h"
#include "../../parser/parse_rr.h"
#include "presence.h"
#include "subscribe.h"
#include "utils_func.h"
#include "notify.h"
#include "../../ip_addr.h"

#define LCONTACT_BUF_SIZE 1024

static str su_200_rpl  = str_init("OK");
static str pu_481_rpl  = str_init("Subscription does not exist");
static str pu_400_rpl  = str_init("Bad request");
static str pu_489_rpl  = str_init("Bad Event");

int send_202ok(struct sip_msg * msg, int lexpire, str *rtag, str* local_contact)
{
	static str hdr_append;

	hdr_append.s = (char *)pkg_malloc( sizeof(char)*(local_contact->len+ 50));
	if(hdr_append.s == NULL)
	{
		LOG(L_ERR,"PRESENCE: send_202ok:ERROR no more pkg memory\n");
		return -1;
	}
	hdr_append.len = sprintf(hdr_append.s, "Expires: %d\r\n", lexpire);	
	
	strncpy(hdr_append.s+hdr_append.len ,"Contact: <", 10);
	hdr_append.len += 10;
	strncpy(hdr_append.s+hdr_append.len, local_contact->s, local_contact->len);
	hdr_append.len+= local_contact->len;
	strncpy(hdr_append.s+hdr_append.len, ">", 1);
	hdr_append.len += 1;
	strncpy(hdr_append.s+hdr_append.len, CRLF, CRLF_LEN);
	hdr_append.len += CRLF_LEN;

	hdr_append.s[hdr_append.len]= '\0';
	
	if (add_lump_rpl( msg, hdr_append.s, hdr_append.len, LUMP_RPL_HDR)==0 )
	{
		LOG(L_ERR,"ERROR:send_202oky : unable to add lump_rl\n");
		goto error;
	}

	if( slb.reply_dlg( msg, 202, &su_200_rpl, rtag)== -1)
	{
		LOG(L_ERR,"PRESENCE:send_202ok: ERORR while sending reply\n");
		goto error;
	}
	
	pkg_free(hdr_append.s);
	return 0;

error:

	pkg_free(hdr_append.s);
	return -1;
}

int send_200ok(struct sip_msg * msg, int lexpire, str *rtag, str* local_contact)
{
	static str hdr_append;	

	hdr_append.s = (char *)pkg_malloc( sizeof(char)*(local_contact->len+ 50));
	if(hdr_append.s == NULL)
	{
		LOG(L_ERR,"ERROR:send_200ok : unable to add lump_rl\n");
		return -1;
	}
	hdr_append.len = sprintf(hdr_append.s, "Expires: %d\r\n", lexpire);
	strncpy(hdr_append.s+hdr_append.len ,"Contact: <", 10);
	hdr_append.len += 10;
	strncpy(hdr_append.s+hdr_append.len, local_contact->s, local_contact->len);
	hdr_append.len+= local_contact->len;
	strncpy(hdr_append.s+hdr_append.len, ">", 1);
	hdr_append.len += 1;
	strncpy(hdr_append.s+hdr_append.len, CRLF, CRLF_LEN);
	hdr_append.len += CRLF_LEN;

	hdr_append.s[hdr_append.len]= '\0';

	if (add_lump_rpl( msg, hdr_append.s, hdr_append.len, LUMP_RPL_HDR)==0 )
	{
		LOG(L_ERR,"ERROR:send_200ok: unable to add lump_rl\n");
		goto error;
	}

	if( slb.reply_dlg( msg, 200, &su_200_rpl, rtag)== -1)
	{
		LOG(L_ERR,"PRESENCE:send_200ok : ERORR while sending reply\n");
		goto error;
	}
	
	pkg_free(hdr_append.s);
	return 0;
error:
	pkg_free(hdr_append.s);
	return -1;

}

int update_subscription(struct sip_msg* msg, subs_t* subs, str *rtag,
		int to_tag_gen, ev_t* event)
{	
	db_key_t query_cols[17];
	db_op_t  query_ops[17];
	db_val_t query_vals[17], update_vals[5];
	db_key_t result_cols[4], update_keys[5];
	db_res_t *result= NULL;
	unsigned int remote_cseq, local_cseq;
	db_row_t *row ;	
	db_val_t *row_vals ;
	int n_update_cols= 0;
	int n_query_cols = 0;
	int n_result_cols = 0;
	int i ,remote_cseq_col= 0, local_cseq_col= 0;
	

	DBG("PRESENCE: update_subscription...\n");
	printf_subs(subs);	
	
	query_cols[n_query_cols] = "to_user";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->to_user.s;
	query_vals[n_query_cols].val.str_val.len = subs->to_user.len;
	n_query_cols++;
	
	query_cols[n_query_cols] = "to_domain";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->to_domain.s;
	query_vals[n_query_cols].val.str_val.len = subs->to_domain.len;
	n_query_cols++;

	query_cols[n_query_cols] = "from_user";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->from_user.s;
	query_vals[n_query_cols].val.str_val.len = subs->from_user.len;
	n_query_cols++;
	
	query_cols[n_query_cols] = "from_domain";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->from_domain.s;
	query_vals[n_query_cols].val.str_val.len = subs->from_domain.len;
	n_query_cols++;

	query_cols[n_query_cols] = "event";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val = subs->event->stored_name;
	n_query_cols++;

	query_cols[n_query_cols] = "event_id";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	if( subs->event_id.s != NULL)
	{
		query_vals[n_query_cols].val.str_val.s = subs->event_id.s;
		query_vals[n_query_cols].val.str_val.len = subs->event_id.len;
	} else {
		query_vals[n_query_cols].val.str_val.s = "";
		query_vals[n_query_cols].val.str_val.len = 0;
	}
	n_query_cols++;
	
	query_cols[n_query_cols] = "callid";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->callid.s;
	query_vals[n_query_cols].val.str_val.len = subs->callid.len;
	n_query_cols++;

	query_cols[n_query_cols] = "to_tag";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->to_tag.s;
	query_vals[n_query_cols].val.str_val.len = subs->to_tag.len;
	n_query_cols++;

	query_cols[n_query_cols] = "from_tag";
	query_ops[n_query_cols] = OP_EQ;
	query_vals[n_query_cols].type = DB_STR;
	query_vals[n_query_cols].nul = 0;
	query_vals[n_query_cols].val.str_val.s = subs->from_tag.s;
	query_vals[n_query_cols].val.str_val.len = subs->from_tag.len;
	n_query_cols++;
	
	result_cols[remote_cseq_col=n_result_cols++] = "remote_cseq";
	result_cols[local_cseq_col=n_result_cols++] = "local_cseq";

	if (pa_dbf.use_table(pa_db, active_watchers_table) < 0) 
	{
		LOG(L_ERR, "PRESENCE:update_subscription: ERROR in use_table\n");
		goto error;
	}
	
/* update the informations in database*/

	if( to_tag_gen ==0) /*if a SUBSCRIBE within a dialog */
	{
		DBG("PRESENCE:update_subscription: querying database  \n");
		if (pa_dbf.query (pa_db, query_cols, query_ops, query_vals,
			 result_cols, n_query_cols, n_result_cols, 0,  &result) < 0) 
		{
			LOG(L_ERR, "PRESENCE:update_subscription: ERROR while querying"
					" presentity\n");
			if(result)
				pa_dbf.free_result(pa_db, result);
			return -1;
		}
		if(result== NULL)
			return -1;

		if(result && result->n <=0)
		{
			LOG(L_ERR, "PRESENCE:update_subscription: The query returned"
					" no result\n");
			
			if (slb.reply(msg, 481, &pu_481_rpl) == -1)
			{
				LOG(L_ERR, "PRESENCE: update_subscription: ERROR while"
						" sending reply\n");
				pa_dbf.free_result(pa_db, result);
				return -1;
			}
			pa_dbf.free_result(pa_db, result);
			return 0;
		}
		// verify remote cseq
		row = &result->rows[0];
		row_vals = ROW_VALUES(row);
		local_cseq= row_vals[local_cseq_col].val.int_val;
		remote_cseq= row_vals[remote_cseq_col].val.int_val;
		if(subs->cseq<= remote_cseq)
		{
			LOG(L_ERR, "PRESENCE:update_subscription: ERROR wrong sequence number\n");
			if (slb.reply(msg, 400, &pu_400_rpl) == -1)
			{
				LOG(L_ERR, "PRESENCE: update_subscription: ERROR while"
						" sending reply\n");
			}
			pa_dbf.free_result(pa_db, result);
			return -1;
		}
		else
		   remote_cseq=subs->cseq;
		
		pa_dbf.free_result(pa_db, result);
		result= NULL;
		// from this point on the subs.cseq field will store 
		// the local_cseq used for Notify
		subs->cseq= local_cseq;
		
		/* delete previous stored subscription if the expires value is 0*/
		if(subs->expires == 0)
		{
			DBG("PRESENCE:update_subscription: expires =0 ->"
					" deleting from database\n");
			if(pa_dbf.delete(pa_db, query_cols, query_ops, query_vals,
						n_query_cols)< 0 )
			{
				LOG(L_ERR,"PRESENCE:update_subscription: ERROR cleaning"
						" unsubscribed messages\n");	
			}
			if(event->type & PUBL_TYPE)
			{	
				if( send_202ok(msg, subs->expires, rtag, &subs->local_contact) <0)
				{
					LOG(L_ERR, "PRESENCE:update_subscription:ERROR while"
						" sending 202 OK\n");
					goto error;
				}
				
				if(event->wipeer)
				{
					if(query_db_notify(&subs->to_user,&subs->to_domain, event->wipeer ,
							NULL, NULL, NULL)< 0)
					{
						LOG(L_ERR, "PRESENCE:update_subscription:Could not send"
							" notify for presence.winfo\n");
					}
				}	
			}	
			else /* if unsubscribe for winfo */
			{
				if( send_200ok(msg, subs->expires, rtag, &subs->local_contact) <0)
				{
					LOG(L_ERR, "PRESENCE:update_subscription:ERROR while"
						" sending 202 OK\n");
					goto error;
				}
			}
			return 1;
		}

		/* otherwise update in database expires and remote_cseq */

		update_keys[n_update_cols] = "expires";
		update_vals[n_update_cols].type = DB_INT;
		update_vals[n_update_cols].nul = 0;
		update_vals[n_update_cols].val.int_val = subs->expires + (int)time(NULL);
		n_update_cols++;
		
		update_keys[n_update_cols] = "remote_cseq";
		update_vals[n_update_cols].type = DB_INT;
		update_vals[n_update_cols].nul = 0;
		update_vals[n_update_cols].val.int_val = remote_cseq ; // the last cseq in dialog
		n_update_cols++;

		update_keys[n_update_cols] = "status";
		update_vals[n_update_cols].type = DB_STR;
		update_vals[n_update_cols].nul = 0;
		update_vals[n_update_cols].val.str_val = subs->status;
		n_update_cols++;

		if( pa_dbf.update( pa_db,query_cols, query_ops, query_vals,
					update_keys, update_vals, n_query_cols,n_update_cols )<0) 
		{
			LOG( L_ERR , "PRESENCE:update_subscription:ERROR while updating"
					" presence information\n");
			goto error;
		}
	}
	else
	{
		if(subs->expires!= 0)
		{		
			query_cols[n_query_cols] = "contact";
			query_vals[n_query_cols].type = DB_STR;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.str_val.s = subs->contact.s;
			query_vals[n_query_cols].val.str_val.len = subs->contact.len;
			n_query_cols++;
	
			query_cols[n_query_cols] = "status";
			query_vals[n_query_cols].type = DB_STR;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.str_val.s = subs->status.s;
			query_vals[n_query_cols].val.str_val.len = subs->status.len;
			n_query_cols++;
			
			query_cols[n_query_cols] = "remote_cseq";
			query_vals[n_query_cols].type = DB_INT;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.int_val = subs->cseq; // initialise with 0
			n_query_cols++;


			query_cols[n_query_cols] = "local_cseq";
			query_vals[n_query_cols].type = DB_INT;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.int_val = 0; // initialise with 0
			n_query_cols++;
			subs->cseq= 0;

			DBG("expires: %d\n", subs->expires);
			query_cols[n_query_cols] = "expires";
			query_vals[n_query_cols].type = DB_INT;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.int_val = subs->expires +
				(int)time(NULL);
			n_query_cols++;

			if(subs->record_route.s!=NULL && subs->record_route.len!=0)
			{
				query_cols[n_query_cols] = "record_route";
				query_vals[n_query_cols].type = DB_STR;
				query_vals[n_query_cols].nul = 0;
				query_vals[n_query_cols].val.str_val.s = subs->record_route.s;
				query_vals[n_query_cols].val.str_val.len = 
					subs->record_route.len;
				n_query_cols++;
			}

			/*  */
			/* Save receive socket also -- ke. */
		    struct socket_info *si = msg->rcv.bind_address;
			query_cols[n_query_cols] = "socket_info";
			query_vals[n_query_cols].type = DB_STR;
			query_vals[n_query_cols].val.str_val = si->sock_str;
			query_vals[n_query_cols].nul = 0;
			n_query_cols++;

			subs->sockinfo_str.s= si->sock_str.s;
			subs->sockinfo_str.len= si->sock_str.len;
			
			query_cols[n_query_cols] = "local_contact";
			query_vals[n_query_cols].type = DB_STR;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.str_val.s = subs->local_contact.s;
			query_vals[n_query_cols].val.str_val.len = 
				subs->local_contact.len;
			n_query_cols++;

			query_cols[n_query_cols] = "version";
			query_vals[n_query_cols].type = DB_INT;
			query_vals[n_query_cols].nul = 0;
			query_vals[n_query_cols].val.int_val = 0;
			n_query_cols++;

			DBG("PRESENCE:update_subscription:Inserting into database:"		
				"\nn_query_cols:%d\n",n_query_cols);
			for(i = 0;i< n_query_cols; i++)
			{
				if(query_vals[i].type==DB_STR)
				DBG("[%d] = %s %.*s\n",i, query_cols[i], 
					query_vals[i].val.str_val.len,query_vals[i].val.str_val.s );
				if(query_vals[i].type==DB_INT)
					DBG("[%d] = %s %d\n",i, query_cols[i], 
						query_vals[i].val.int_val);
			}
	
			if (pa_dbf.insert(pa_db, query_cols, query_vals, n_query_cols) < 0) 
			{
				LOG(L_ERR, "PRESENCE:update_subscription: ERROR while storing"
						" new subscribtion\n");
				goto error;
			}
		
		}
		/*otherwise there is a subscription outside a dialog with expires= 0 
		 * no update in database, but
		 * should try to send Notify */
		 
	}

/* reply_and_notify  */

	if(event->type & PUBL_TYPE)
	{	
		if( send_202ok(msg, subs->expires, rtag, &subs->local_contact) <0)
		{
			LOG(L_ERR, "PRESENCE:update_subscription:ERROR while"
					" sending 202 OK\n");
			goto error;
		}
		
		if(event->wipeer)
		{	
			if(query_db_notify(&subs->to_user,&subs->to_domain, event->wipeer,
					subs, NULL, NULL)< 0)
			{
				LOG(L_ERR, "PRESENCE:update_subscription:Could not send"
					" notify for presence.winfo\n");
			}	
			if(subs->send_on_cback== 0)
			{	
				if(notify(subs, NULL, NULL, 0)< 0)
				{
					LOG(L_ERR, "PRESENCE:update_subscription: Could not send"
					" notify for presence\n");
				}
			}
		}
		else
		{
			if(notify(subs, NULL, NULL, 0)< 0)
			{
				LOG(L_ERR, "PRESENCE:update_subscription: Could not send"
				" notify for presence\n");
			}
		}	
			
	}
	else 
	{
		if( send_200ok(msg, subs->expires, rtag, &subs->local_contact) <0)
		{
			LOG(L_ERR, "PRESENCE:update_subscription:ERROR while"
					" sending 202 OK\n");
			goto error;
		}		
		if(notify(subs, NULL, NULL, 0 )< 0)
		{
			LOG(L_ERR, "PRESENCE:update_subscription: ERROR while"
				" sending notify\n");
		}
	}
	
	return 0;
	
error:

	LOG(L_ERR, "PRESENCE:update_presentity: ERROR occured\n");
	return -1;

}

void msg_watchers_clean(unsigned int ticks,void *param)
{
	db_key_t db_keys[3];
	db_val_t db_vals[3];
	db_op_t  db_ops[3] ;
	db_res_t *result= NULL;

	DBG("PRESENCE: msg_watchers_clean:cleaning pending subscriptions\n");
	
	db_keys[0] ="inserted_time";
	db_ops[0] = OP_LT;
	db_vals[0].type = DB_INT;
	db_vals[0].nul = 0;
	db_vals[0].val.int_val = (int)time(NULL)- 24*3600 ;

	db_keys[1] = "subs_status";
	db_ops [1] = OP_EQ;
	db_vals[1].type = DB_STR;
	db_vals[1].nul = 0;
	db_vals[1].val.str_val.s = "pending";
	db_vals[1].val.str_val.len = 7;

	if (pa_dbf.use_table(pa_db, watchers_table) < 0) 
	{
		LOG(L_ERR, "PRESENCE:msg_watchers_clean: ERROR in use_table\n");
		return ;
	}
	
	if(pa_dbf.query(pa_db, db_keys, db_ops, db_vals, 0,	1, 0, 0, &result )< 0)
	{
		LOG(L_ERR, "PRESENCE:msg_watchers_clean: ERROR while querying database"
				" for expired messages\n");
		if(result)
			pa_dbf.free_result(pa_db, result);
		return;
	}
	if(result == NULL)
		return;
	if(result->n <= 0)
	{
		pa_dbf.free_result(pa_db, result);
		return;
	}
	pa_dbf.free_result(pa_db, result);

	if (pa_dbf.delete(pa_db, db_keys, db_ops, db_vals, 2) < 0) 
		LOG(L_ERR,"PRESENCE:msg_watchers_clean: ERROR cleaning pending "
				" subscriptions\n");
}

void msg_active_watchers_clean(unsigned int ticks,void *param)
{
	db_key_t db_keys[1];
	db_val_t db_vals[1];
	db_op_t  db_ops[1] ;
	db_key_t result_cols[20];
	db_res_t *result= NULL;
	db_row_t *row ;	
	db_val_t *row_vals ;
	subs_t** subs_array= NULL;
	subs_t* subs= NULL;
	int n, size;
	int n_result_cols = 0;
	int from_user_col, from_domain_col, to_tag_col, from_tag_col;
	int to_user_col, to_domain_col, event_col;
	int callid_col, cseq_col, i, event_id_col = 0;
	int record_route_col = 0, contact_col, cseq;
	int sockinfo_col = 0, local_contact_col= 0;
	str ev_name;
	str* ev_param= NULL;
	char* sep;

	str from_user, from_domain, to_tag, from_tag;
	str to_user, to_domain, event, event_id, callid;
	str record_route, contact;
	str sockinfo_str, local_contact;
	
	DBG("PRESENCE: msg_active_watchers_clean:cleaning expired watcher information\n");
	
	db_keys[0] ="expires";
	db_ops[0] = OP_LT;
	db_vals[0].type = DB_INT;
	db_vals[0].nul = 0;
	db_vals[0].val.int_val = (int)time(NULL);
	

	result_cols[event_col=n_result_cols++] = "event";
	result_cols[from_user_col=n_result_cols++] = "from_user" ;
	result_cols[from_domain_col=n_result_cols++] = "from_domain" ;
	result_cols[to_user_col=n_result_cols++] = "to_user" ;
	result_cols[to_domain_col=n_result_cols++] = "to_domain" ;
	result_cols[event_id_col=n_result_cols++] = "event_id";
	result_cols[from_tag_col=n_result_cols++] = "from_tag";
	result_cols[to_tag_col=n_result_cols++] = "to_tag";	
	result_cols[callid_col=n_result_cols++] = "callid";
	result_cols[cseq_col=n_result_cols++] = "local_cseq";
	result_cols[record_route_col=n_result_cols++] = "record_route";
	result_cols[contact_col=n_result_cols++] = "contact";
	result_cols[local_contact_col=n_result_cols++] = "local_contact";
	result_cols[sockinfo_col=n_result_cols++] = "socket_info"; 

	if (pa_dbf.use_table(pa_db, active_watchers_table) < 0) 
	{
		LOG(L_ERR, "PRESENCE:msg_active_watchers_clean: ERROR in use_table\n");
		return ;
	}

	if(pa_dbf.query(pa_db, db_keys, db_ops, db_vals, result_cols,
						1, n_result_cols, 0, &result )< 0)
	{
		LOG(L_ERR, "PRESENCE:msg_active_watchers_clean: ERROR while querying database"
				" for expired messages\n");
		if(result)
			pa_dbf.free_result(pa_db, result);
		return;
	}
	if(result == NULL)
		return;
	if(result->n <= 0)
	{
		pa_dbf.free_result(pa_db, result);
		return;
	}

	n= result->n;
	subs_array= (subs_t**)pkg_malloc(n* sizeof(subs_t*));
	if(subs_array== NULL)
	{
		LOG(L_ERR, "PRESENCE:msg_active_watchers_clean: ERROR while allocating memory\n");
		pa_dbf.free_result(pa_db, result);
		return;	
	}
	memset(subs_array, 0, n*sizeof(subs_t*));

	for(i=0 ; i< n; i++)
	{
		row = &result->rows[i];
		row_vals = ROW_VALUES(row);		
		
		to_user.s = (char*)row_vals[to_user_col].val.string_val;
		to_user.len =strlen(to_user.s);

		to_domain.s = (char*)row_vals[to_domain_col].val.string_val;
		to_domain.len =strlen(to_domain.s);

		event.s =(char*)row_vals[event_col].val.string_val;
		event.len = strlen(event.s);

		from_user.s = (char*)row_vals[from_user_col].val.string_val;
		from_user.len = strlen(from_user.s);
		
		from_domain.s = (char*)row_vals[from_domain_col].val.string_val;
		from_domain.len = strlen(from_domain.s);
		
		memset(&event_id, 0, sizeof(str));
		event_id.s = (char*)row_vals[event_id_col].val.string_val;
		if(event_id.s)
			event_id.len = strlen(event_id.s);

			
		to_tag.s = (char*)row_vals[to_tag_col].val.string_val;
		to_tag.len = strlen(to_tag.s);
		
		from_tag.s = (char*)row_vals[from_tag_col].val.string_val;
		from_tag.len = strlen(from_tag.s);

		callid.s = (char*)row_vals[callid_col].val.string_val;
		callid.len = strlen(callid.s);

		memset(&contact, 0, sizeof(str));
		contact.s = (char*)row_vals[contact_col].val.string_val;
		if(contact.s)
			contact.len = strlen(contact.s);

		cseq = row_vals[cseq_col].val.int_val;
		
		memset(&record_route, 0, sizeof(str));
		record_route.s = (char*)row_vals[record_route_col].val.string_val;
		if(record_route.s )
			record_route.len = strlen(record_route.s);

		sockinfo_str.s = (char*)row_vals[sockinfo_col].val.string_val;
		sockinfo_str.len = sockinfo_str.s?strlen (sockinfo_str.s):0;

		local_contact.s = (char*)row_vals[local_contact_col].val.string_val;
		local_contact.len = local_contact.s?strlen (local_contact.s):0;
		
		size= sizeof(subs_t)+ ( to_user.len+ to_domain.len+ from_user.len+ from_domain.len+
				 event_id.len+ to_tag.len+ from_tag.len+ callid.len+ contact.len+
				record_route.len+ sockinfo_str.len+ local_contact.len)* sizeof(char);

		subs= (subs_t*)pkg_malloc(size);
		if(subs== NULL)
		{
			LOG(L_ERR," PRESENCE:msg_active_watchers_clean: ERROR while allocating memory\n");
			goto error;
		}
		memset(subs, 0, size);
		size= sizeof(subs_t);
		
		subs->to_user.s= (char*)subs+ size;
		memcpy(subs->to_user.s, to_user.s, to_user.len);
		subs->to_user.len= to_user.len;
		size+= to_user.len;
	
		subs->to_domain.s= (char*)subs+ size;
		memcpy(subs->to_domain.s, to_domain.s, to_domain.len);
		subs->to_domain.len= to_domain.len;
		size+= to_domain.len;

		subs->from_user.s= (char*)subs+ size;
		memcpy(subs->from_user.s, from_user.s, from_user.len);
		subs->from_user.len= from_user.len;
		size+= from_user.len;
	
		subs->from_domain.s= (char*)subs+ size;
		memcpy(subs->from_domain.s, from_domain.s, from_domain.len);
		subs->from_domain.len= from_domain.len;
		size+= from_domain.len;

		sep= memchr(event.s, ';', event.len);
		if(sep == NULL)
		{
			ev_name= event;
		}
		else
		{
			ev_name.s= event.s;
			ev_name.len= sep- event.s;

			ev_param= (str*)pkg_malloc(sizeof(str));
			if(ev_param== NULL)
			{
				LOG(L_ERR, "PRESENCE:msg_presentity_clean: ERROR no more memory\n");
				goto error;
			}
			ev_param->s= sep+1;
			ev_param->len= event.len- ev_name.len -1;
		
			DBG("PRESENCE:msg_presentity_clean: ev_name= %.*s ev_param= %.*s\n",
			ev_name.len, ev_name.s,  ev_param->len, ev_param->s);

		}	
		subs->event= contains_event(&ev_name, ev_param);
		if(ev_param)
			pkg_free(ev_param);
		
		if(subs->event== NULL)
		{
			LOG(L_ERR, "PRESENCE:msg_active_watchers_clean: ERROR while"
					" searching for event\n");
			
			goto error;
		}	

		if(event_id.s)
		{	
			subs->event_id.s= (char*)subs+ size;
			memcpy(subs->event_id.s, event_id.s, event_id.len);
			subs->event_id.len= event_id.len;
			size+= event_id.len;
		}
		subs->to_tag.s= (char*)subs+ size;
		memcpy(subs->to_tag.s, to_tag.s, to_tag.len);
		subs->to_tag.len= to_tag.len;
		size+= to_tag.len;
	
		subs->from_tag.s= (char*)subs+ size;
		memcpy(subs->from_tag.s, from_tag.s, from_tag.len);
		subs->from_tag.len= from_tag.len;
		size+= from_tag.len;

		subs->callid.s= (char*)subs+ size;
		memcpy(subs->callid.s, callid.s, callid.len);
		subs->callid.len= callid.len;
		size+= callid.len;
		
		if(contact.s)
		{	
			subs->contact.s= (char*)subs+ size;
			memcpy(subs->contact.s, contact.s, contact.len);
			subs->contact.len= contact.len;
			size+= contact.len;
		}
		if(record_route.s)
		{
			subs->record_route.s= (char*)subs+ size;
			memcpy(subs->record_route.s, record_route.s, record_route.len);
			subs->record_route.len= record_route.len;
			size+= record_route.len;
		}
		subs->sockinfo_str.s =(char*)subs+ size;
		memcpy(subs->sockinfo_str.s, sockinfo_str.s, sockinfo_str.len);
		subs->sockinfo_str.len = sockinfo_str.len;
		size+= sockinfo_str.len;
		
		subs->local_contact.s =(char*)subs+ size;
		memcpy(subs->local_contact.s, local_contact.s, local_contact.len);
		subs->local_contact.len = local_contact.len;
		size+= local_contact.len;

		subs->expires = 0;
		subs->cseq= cseq;
		subs->status.s = "terminated";
		subs->status.len = 10;
		subs->reason.s = "timeout";
		subs->reason.len = 7;
		subs_array[i]= subs;

	}
	
	pa_dbf.free_result(pa_db, result);
	result= NULL;

	for(i= 0; i< n; i++)
	{
		notify(subs_array[i],  NULL, NULL, 0);
		pkg_free(subs_array[i]);
	}
	pkg_free(subs_array);
		
	if (pa_dbf.use_table(pa_db, active_watchers_table) < 0) 
	{
		LOG(L_ERR, "PRESENCE:msg_active_watchers_clean: ERROR in use_table\n");
		return ;
	}

	if (pa_dbf.delete(pa_db, db_keys, db_ops, db_vals, 1) < 0) 
		LOG(L_ERR,"PRESENCE:msg_active_watchers_clean: ERROR cleaning expired"
				" messages\n");
	return;

error:
	if(result)
		pa_dbf.free_result(pa_db, result);

	if(subs_array)
	{
		for(i= 0; i<n; i++)
		{
			if(subs_array[i])
				pkg_free(subs_array[i]);
			else
				break;
		}
		pkg_free(subs_array);
	}
}

int handle_subscribe(struct sip_msg* msg, char* str1, char* str2)
{
	struct sip_uri to_uri;
	struct sip_uri from_uri;
	struct to_body *pto, *pfrom = NULL, TO;
	int lexpire;
	int  to_tag_gen = 0;
	str rtag_value;
	subs_t subs;
	static char buf[50];
	static char cont_buf[LCONTACT_BUF_SIZE];
	str rec_route= {0, 0};
	int error_ret = -1;
	int rt  = 0;
	db_key_t db_keys[8];
	db_val_t db_vals[8];
	db_key_t update_keys[3];
	db_val_t update_vals[3];

	int n_query_cols= 0; 
	db_key_t result_cols[2];
	db_res_t *result = NULL;
	db_row_t *row ;	
	db_val_t *row_vals ;
	str status= {0, 0};
	str reason= {0, 0};
	str contact;
	ev_t* event= NULL;
	param_t* ev_param= NULL;
	str ev_name;
	int result_code, result_n;
	
	/* ??? rename to avoid collisions with other symbols */
	counter ++;
	contact_body_t *b;

	memset(&subs, 0, sizeof(subs_t));

	if ( parse_headers(msg,HDR_EOH_F, 0)==-1 )
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe:error parsing headers\n");

		if (slb.reply(msg, 400, &pu_400_rpl) == -1)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR while sending"
					" 400 reply\n");
		}
		error_ret = 0;
		goto error;
	}

	/* inspecting the Event header field */
	if(msg->event && msg->event->body.len > 0)
	{
		if (!msg->event->parsed && (parse_event(msg->event) < 0))
		{
			LOG(L_ERR,
				"PRESENCE: handle_subscribe: ERROR cannot parse Event header\n");
			goto error;
		}
		if(((event_t*)msg->event->parsed)->parsed & EVENT_OTHER)
		{	
			goto bad_event;
		}
	}
	else
		goto bad_event;

	ev_name= ((event_t*)msg->event->parsed)->text;
	ev_param= ((event_t*)msg->event->parsed)->params;
	
	event= contains_event(&ev_name, NULL);
	
	if(!event && ev_param)
	{
		while(ev_param)
		{
			if(ev_param->name.len== 2 && strncmp(ev_param->name.s, "id", 2)== 0)
				subs.event_id= ev_param->body;

			event= contains_event(&ev_name, &ev_param->name);
			if(event)
				break;
			ev_param= ev_param->next;
		}	
	}	

	if(event== NULL)
	{
		goto bad_event;
	}
	if(!subs.event_id.s && ev_param)
	{
		while(ev_param)
		{
			if(ev_param->name.len== 2 && strncmp(ev_param->name.s, "id", 2)== 0)
			{
				subs.event_id= ev_param->body;
				break;
			}
			ev_param= ev_param->next;
		}	
	}	
	subs.event= event;
	


	/* examine the expire header field */
	if(msg->expires && msg->expires->body.len > 0)
	{
		if (!msg->expires->parsed && (parse_expires(msg->expires) < 0))
		{
			LOG(L_ERR,
				"PRESENCE: handle_subscribe: ERROR cannot parse Expires header\n");
			goto error;
		}
		DBG("PRESENCE: handle_subscribe: 'expires' found\n");
		lexpire = ((exp_body_t*)msg->expires->parsed)->val;
		DBG("PRESENCE: handle_subscribe: lexpire= %d\n", lexpire);

	}
	else 
	{
		DBG("PRESENCE: handle_subscribe: 'expires' not found; default=%d\n",
				default_expires);
		lexpire = default_expires;
	}
	if(lexpire > max_expires)
		lexpire = max_expires;

	subs.expires = lexpire;

	if( msg->to==NULL || msg->to->body.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse TO"
				" header\n");
		goto error;
	}
	/* examine the to header */
	if(msg->to->parsed != NULL)
	{
		pto = (struct to_body*)msg->to->parsed;
		DBG("PRESENCE: handle_subscribe: 'To' header ALREADY PARSED: <%.*s>\n",
				pto->uri.len, pto->uri.s );	
	}
	else
	{
		memset( &TO , 0, sizeof(TO) );
		if( !parse_to(msg->to->body.s,msg->to->body.s + msg->to->body.len + 1, &TO));
		{
			DBG("PRESENCE: handle_subscribe: 'To' header NOT parsed\n");
			goto error;
		}
		pto = &TO;
	}
	
	if(parse_uri(pto->uri.s, pto->uri.len, &to_uri)!=0)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: bad R-URI!\n");
		goto error;
	}

	if(to_uri.user.len<=0 || to_uri.user.s==NULL || to_uri.host.len<=0 ||
			to_uri.host.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: bad URI in To header!\n");
		goto error;
	}
	subs.to_user.s = to_uri.user.s;
	subs.to_user.len = to_uri.user.len;

	subs.to_domain.s = to_uri.host.s;
	subs.to_domain.len = to_uri.host.len;

	/* examine the from header */
	if (!msg->from || !msg->from->body.s)
	{
		DBG("PRESENCE:handle_subscribe: ERROR cannot find 'from' header!\n");
		goto error;
	}
	if (msg->from->parsed == NULL)
	{
		DBG("PRESENCE:handle_subscribe: 'From' header not parsed\n");
		/* parsing from header */
		if ( parse_from_header( msg )<0 ) 
		{
			DBG("PRESENCE:handle_subscribe: ERROR cannot parse From header\n");
			goto error;
		}
	}
	pfrom = (struct to_body*)msg->from->parsed;

	if(parse_uri(pfrom->uri.s, pfrom->uri.len, &from_uri)!=0)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: bad R-URI!\n");
		goto error;
	}

	if(from_uri.user.len<=0 || from_uri.user.s==NULL || from_uri.host.len<=0 ||
			from_uri.host.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: bad URI in To header!\n");
		goto error;
	}
	
	subs.from_user.s = from_uri.user.s;
	subs.from_user.len = from_uri.user.len;

	subs.from_domain.s = from_uri.host.s;
	subs.from_domain.len = from_uri.host.len;

	/*generate to_tag if the message does not have a to_tag*/
	if (pto->tag_value.s==NULL || pto->tag_value.len==0 )
	{  
		DBG("PRESENCE:handle_subscribe: generating to_tag\n");
		to_tag_gen = 1;
		/*generate to_tag then insert it in avp*/
		
		rtag_value.s = buf;
		rtag_value.len = sprintf(rtag_value.s,"%s.%d.%d.%d", to_tag_pref,
				pid, (int)time(NULL), counter);
		if(rtag_value.len<= 0)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR while creating"
					" to_tag\n");
			goto error;
		}
	}
	else
	{
		rtag_value=pto->tag_value;
	}
	subs.to_tag = rtag_value;

	if( msg->callid==NULL || msg->callid->body.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse callid"
				" header\n");
		goto error;
	}
	subs.callid.s = msg->callid->body.s;
	subs.callid.len = msg->callid->body.len;

	if( msg->cseq==NULL || msg->cseq->body.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse cseq"
				" header\n");
		goto error;
	}
	if (str2int( &(get_cseq(msg)->number), &subs.cseq)!=0 )
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse cseq"
				" number\n");
		goto error;
	}
	if( msg->contact==NULL || msg->contact->body.s==NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse contact"
				" header\n");
		goto error;
	}
	if( parse_contact(msg->contact) <0 )
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse contact"
				" header\n");
		goto error;
	}
	b= (contact_body_t* )msg->contact->parsed;

	if(b == NULL)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR cannot parse contact"
				" header\n");
		goto error;
	}
	subs.contact.s = b->contacts->uri.s;
	subs.contact.len = b->contacts->uri.len;
	
/*process record route and add it to a string*/
	if (msg->record_route!=NULL)
	{
		rt = print_rr_body(msg->record_route, &rec_route, 0);
		if(rt != 0)
		{
			LOG(L_ERR,"PRESENCE:handle_subscribe:error processing the record"
					" route [%d]\n", rt);	
			rec_route.s=NULL;
			rec_route.len=0;
		//	goto error;
		}
	}

	subs.record_route.s = rec_route.s;
	subs.record_route.len = rec_route.len;

	if( pfrom->tag_value.s ==NULL || pfrom->tag_value.len == 0)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR no from tag value"
				" present\n");
		goto error;
	}

	subs.from_tag.s = pfrom->tag_value.s;
	subs.from_tag.len = pfrom->tag_value.len;

	subs.version = 0;
	
	if((!server_address.s) || (server_address.len== 0))
	{
		str ip;
		char* proto;
		int port;
		int len;

		memset(cont_buf, 0, LCONTACT_BUF_SIZE*sizeof(char));
		contact.s= cont_buf;
		contact.len= 0;
	
		if(msg->rcv.proto== PROTO_NONE || msg->rcv.proto==PROTO_UDP)
		{
			proto= "udp";
		}
		else
		if(msg->rcv.proto== PROTO_TLS )
		{
			proto= "tls";
		}
		else	
		if(msg->rcv.proto== PROTO_TCP)
		{
			proto= "tcp";
		}
		else
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe:ERROR unsupported proto\n");
			goto error;
		}	
		ip.s= ip_addr2a(&msg->rcv.dst_ip);
		if(ip.s== NULL)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe:ERROR while transforming ip_addr to ascii\n");
			goto error;
		}
		ip.len= strlen(ip.s);
		port = msg->rcv.dst_port;

		if(strncmp(ip.s, "sip:", 4)!=0)
		{
			strncpy(contact.s, "sip:", 4);
			contact.len+= 4;
		}	
		strncpy(contact.s+contact.len, ip.s, ip.len);
		contact.len += ip.len;
		if(contact.len> LCONTACT_BUF_SIZE - 21)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR buffer overflow\n");
			goto error;
		}	
		len= sprintf(contact.s+contact.len, ":%d;transport=" , port);
		if(len< 0)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR in function sprintf\n");
			goto error;

		}	
		contact.len+= len;
		strncpy(contact.s+ contact.len, proto, 3);
		contact.len += 3;
		
		subs.local_contact= contact;
	}
	else
		subs.local_contact= server_address;

	DBG("PRESENCE: handle_subscribe: local_contact: %.*s --- len= %d\n", 
	subs.local_contact.len, subs.local_contact.s, subs.local_contact.len);

	/* call event specific subscription handling */
	if(event->evs_subs_handl)
	{
		if(event->evs_subs_handl(msg)< 0)
		{
			LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR in event specific"
					" subscription handling\n");
			goto error;
		}

	}	

	/* subscription status handling */
	db_keys[n_query_cols] ="p_user";
	db_vals[n_query_cols].type = DB_STR;
	db_vals[n_query_cols].nul = 0;
	db_vals[n_query_cols].val.str_val= subs.to_user;
	n_query_cols++;

	db_keys[n_query_cols] ="p_domain";
	db_vals[n_query_cols].type = DB_STR;
	db_vals[n_query_cols].nul = 0;
	db_vals[n_query_cols].val.str_val = subs.to_domain;
	n_query_cols++;

	db_keys[n_query_cols] ="w_user";
	db_vals[n_query_cols].type = DB_STR;
	db_vals[n_query_cols].nul = 0;
	db_vals[n_query_cols].val.str_val = subs.from_user;
	n_query_cols++;

	db_keys[n_query_cols] ="w_domain";
	db_vals[n_query_cols].type = DB_STR;
	db_vals[n_query_cols].nul = 0;
	db_vals[n_query_cols].val.str_val = subs.from_domain;
	n_query_cols++;

	result_cols[0] = "subs_status";
	result_cols[1] = "reason";

	if(pa_dbf.use_table(pa_db, watchers_table)< 0)
	{
		LOG(L_ERR,"PRESENCE:handle_subscribe: ERROR in use table\n");
		goto error;
	}	

	if(pa_dbf.query(pa_db, db_keys, 0, db_vals, result_cols,
					n_query_cols, 2, 0, &result )< 0)
	{
		LOG(L_ERR, "PRESENCE:handle_subscribe: ERROR while querying"
				" watchers table\n");
		if(result)
			pa_dbf.free_result(pa_db, result);
		goto error;
	}
	if(result== NULL)
		goto error;
		
	result_n= result->n;
	if(result_n> 0)
	{	
		row = &result->rows[0];
		row_vals = ROW_VALUES(row);
		status.len= strlen(row_vals[0].val.string_val);
		status.s= (char*)pkg_malloc(status.len* sizeof(char));
		if(status.s== NULL)
		{
			LOG(L_ERR, "PRESENCE:handle_subscribe: ERORR No more memory\n");
			pa_dbf.free_result(pa_db, result);
			goto error;
		}	
		memcpy(status.s, row_vals[0].val.string_val, status.len);
		subs.status= status;
	
		if(row_vals[1].val.string_val)
		{
			reason.len= strlen(row_vals[1].val.string_val);
			if(reason.len== 0)
				reason.s= NULL;
			else
			{
				reason.s= (char*)pkg_malloc(reason.len*sizeof(char));
				if(reason.s)
				{
					LOG(L_ERR, "PRESENCE:handle_subscribe: ERORR No more memory\n");
					pa_dbf.free_result(pa_db, result);
					goto error;		
				}		
			memcpy(reason.s, row_vals[1].val.string_val, reason.len);
			}
			subs.reason= reason;
		}
	}	
	pa_dbf.free_result(pa_db, result);
	
	/* get subs.status */
	if(!event->req_auth)
	{
		subs.status.s = "active";
		subs.status.len = 6 ;
		/* if record noes not exist in watchers_table insert */
		if(result_n<= 0)
		{
			db_keys[n_query_cols] ="subs_status";
			db_vals[n_query_cols].type = DB_STR;
			db_vals[n_query_cols].nul = 0;
			db_vals[n_query_cols].val.str_val = subs.status;
			n_query_cols++;
								
			db_keys[n_query_cols] = "inserted_time";
			db_vals[n_query_cols].type = DB_INT;
			db_vals[n_query_cols].nul = 0;
			db_vals[n_query_cols].val.int_val= (int)time(NULL);
			n_query_cols++;
			
			if(pa_dbf.insert(pa_db, db_keys, db_vals, n_query_cols )< 0)
			{	
				LOG(L_ERR, "PRESENCE: subscribe:ERROR while updating watchers table\n");
				goto error;
			}
		}	
	}
	else    /* check authorization rules or take status from 'watchers' table */
	{
		result_code= subs.event->is_watcher_allowed(&subs);
		if(result_code< 0)
		{
			LOG(L_ERR, "PRESENCE: subscribe: ERROR in function event specific function"
					" is_watcher_allowed\n");
			goto error;
		}				
		if(result_code!=0)	/* a change has ocured */
		{
			if(result_n <=0)
			{
				db_keys[n_query_cols] ="subs_status";
				db_vals[n_query_cols].type = DB_STR;
				db_vals[n_query_cols].nul = 0;
				db_vals[n_query_cols].val.str_val = subs.status;
				n_query_cols++;
					
				if(subs.reason.s && subs.reason.len)
				{
					db_keys[n_query_cols] ="reason";
					db_vals[n_query_cols].type = DB_STR;
					db_vals[n_query_cols].nul = 0;
					db_vals[n_query_cols].val.str_val = subs.reason;
					n_query_cols++;	
				}	
				
				db_keys[n_query_cols] = "inserted_time";
				db_vals[n_query_cols].type = DB_INT;
				db_vals[n_query_cols].nul = 0;
				db_vals[n_query_cols].val.int_val= (int)time(NULL);
				n_query_cols++;

				if(pa_dbf.insert(pa_db, db_keys, db_vals, n_query_cols )< 0)
				{	
					LOG(L_ERR, "PRESENCE: handle_subscribe:ERROR while updating watchers table\n");
					goto error;
				}
			}
			else
			{	/* update if different */
				if(strncmp(status.s, subs.status.s, status.len)||
						(reason.s && subs.reason.s &&
								strncmp(reason.s, subs.reason.s, reason.len)))
				{		
									
					int n_update_cols= 0;

					update_keys[0]="subs_status";
					update_vals[0].type = DB_STR;
					update_vals[0].nul = 0;
					update_vals[0].val.str_val= subs.status;
					n_update_cols++;

					if(subs.reason.s && subs.reason.len)
					{
						update_keys[1]="reason";
						update_vals[1].type = DB_STR;
						update_vals[1].nul = 0;
						update_vals[1].val.str_val= subs.reason;
						n_update_cols++;
					}	
					
					if(pa_dbf.update(pa_db, db_keys, 0, db_vals, 
								update_keys, update_vals, n_query_cols, n_update_cols)< 0)
					{
						LOG(L_ERR, "PRESENCE:handle_subscribe: ERORR while"
								" updating database table\n");
						goto error;
					}
				}
			}
		}
		else	/* if nothing is known put status "pending" */
		{	
			if(result_n<= 0)
			{
				subs.status.s= "pending";
				subs.status.len= 7;
				
				db_keys[n_query_cols] ="subs_status";
				db_vals[n_query_cols].type = DB_STR;
				db_vals[n_query_cols].nul = 0;
				db_vals[n_query_cols].val.str_val = subs.status;
				n_query_cols++;
					
				db_keys[n_query_cols] = "inserted_time";
				db_vals[n_query_cols].type = DB_INT;
				db_vals[n_query_cols].nul = 0;
				db_vals[n_query_cols].val.int_val= (int)time(NULL);
				n_query_cols++;

				if(pa_dbf.insert(pa_db, db_keys, db_vals, n_query_cols )< 0)
				{	
					LOG(L_ERR, "PRESENCE: handle_subscribe:ERROR while updating watchers table\n");
					goto error;
				}

			}
			
		}
	}
	printf_subs(&subs);	
	if( update_subscription(msg, &subs, &rtag_value, to_tag_gen, event) <0 )
	{	
		LOG(L_ERR,"PRESENCE:handle_subscribe: ERROR while updating database\n");
		goto error;
	}

	if(status.s && status.len)
	{
		pkg_free(status.s);
		if(reason.s )
			pkg_free(reason.s);
	}
	if(subs.record_route.s)
		pkg_free(subs.record_route.s);

	return 1;

bad_event:

	LOG(L_ERR, "PRESENCE: handle_subscribe:Missing or unsupported event"
		" header field value\n");
	if (slb.reply(msg, 489, &pu_489_rpl) == -1)
	{
		LOG(L_ERR, "PRESENCE: handle_subscribe: ERROR while sending"
			" reply\n");
	}
	error_ret = 0;

error:
	LOG(L_ERR, "PRESENCE:handle_subscribe: ERROR occured\n");
	if(status.s && status.len)
	{
		pkg_free(status.s);
		if(reason.s )
			pkg_free(reason.s);
	}
	if(subs.record_route.s)
		pkg_free(subs.record_route.s);

	return error_ret;

}

