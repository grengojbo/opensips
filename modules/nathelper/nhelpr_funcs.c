/*
 * $Id$
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
 * --------
 *  2003-11-06  body len is computed using the message len (it's
 *               not taken any more from the msg. content-length) (andrei)
 *  2008-08-30  body len is taken from Conent-length header as it is more 
 *               reliable (UDP packages may contain garbage at the end)(bogdan)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "nhelpr_funcs.h"
#include "../../dprint.h"
#include "../../config.h"
#include "../../ut.h"
#include "../../forward.h"
#include "../../resolve.h"
#include "../../globals.h"
#include "../../udp_server.h"
#include "../../pt.h"
#include "../../parser/msg_parser.h"
#include "../../parser/parse_content.h"




#define READ(val) \
	(*(val + 0) + (*(val + 1) << 8) + (*(val + 2) << 16) + (*(val + 3) << 24))
#define advance(_ptr,_n,_str,_error) \
	do{\
		if ((_ptr)+(_n)>(_str).s+(_str).len)\
			goto _error;\
		(_ptr) = (_ptr) + (_n);\
	}while(0);
#define one_of_16( _x , _t ) \
	(_x==_t[0]||_x==_t[15]||_x==_t[8]||_x==_t[2]||_x==_t[3]||_x==_t[4]\
	||_x==_t[5]||_x==_t[6]||_x==_t[7]||_x==_t[1]||_x==_t[9]||_x==_t[10]\
	||_x==_t[11]||_x==_t[12]||_x==_t[13]||_x==_t[14])
#define one_of_8( _x , _t ) \
	(_x==_t[0]||_x==_t[7]||_x==_t[1]||_x==_t[2]||_x==_t[3]||_x==_t[4]\
	||_x==_t[5]||_x==_t[6])



int check_content_type(struct sip_msg *msg)
{
	static unsigned int appl[16] = {
		0x6c707061/*appl*/,0x6c707041/*Appl*/,0x6c705061/*aPpl*/,
		0x6c705041/*APpl*/,0x6c507061/*apPl*/,0x6c507041/*ApPl*/,
		0x6c505061/*aPPl*/,0x6c505041/*APPl*/,0x4c707061/*appL*/,
		0x4c707041/*AppL*/,0x4c705061/*aPpL*/,0x4c705041/*APpL*/,
		0x4c507061/*apPL*/,0x4c507041/*ApPL*/,0x4c505061/*aPPL*/,
		0x4c505041/*APPL*/};
	static unsigned int icat[16] = {
		0x74616369/*icat*/,0x74616349/*Icat*/,0x74614369/*iCat*/,
		0x74614349/*ICat*/,0x74416369/*icAt*/,0x74416349/*IcAt*/,
		0x74414369/*iCAt*/,0x74414349/*ICAt*/,0x54616369/*icaT*/,
		0x54616349/*IcaT*/,0x54614369/*iCaT*/,0x54614349/*ICaT*/,
		0x54416369/*icAT*/,0x54416349/*IcAT*/,0x54414369/*iCAT*/,
		0x54414349/*ICAT*/};
	static unsigned int ion_[8] = {
		0x006e6f69/*ion_*/,0x006e6f49/*Ion_*/,0x006e4f69/*iOn_*/,
		0x006e4f49/*IOn_*/,0x004e6f69/*ioN_*/,0x004e6f49/*IoN_*/,
		0x004e4f69/*iON_*/,0x004e4f49/*ION_*/};
	static unsigned int sdp_[8] = {
		0x00706473/*sdp_*/,0x00706453/*Sdp_*/,0x00704473/*sDp_*/,
		0x00704453/*SDp_*/,0x00506473/*sdP_*/,0x00506453/*SdP_*/,
		0x00504473/*sDP_*/,0x00504453/*SDP_*/};
	str           str_type;
	unsigned int  x;
	char          *p;

	if (!msg->content_type)
	{
		LM_WARN("the header Content-TYPE is absent!"
			"let's assume the content is text/plain ;-)\n");
		return 1;
	}

	trim_len(str_type.len,str_type.s,msg->content_type->body);
	p = str_type.s;
	advance(p,4,str_type,error_1);
	x = READ(p-4);
	if (!one_of_16(x,appl))
		goto other;
	advance(p,4,str_type,error_1);
	x = READ(p-4);
	if (!one_of_16(x,icat))
		goto other;
	advance(p,3,str_type,error_1);
	x = READ(p-3) & 0x00ffffff;
	if (!one_of_8(x,ion_))
		goto other;

	/* skip spaces and tabs if any */
	while (*p==' ' || *p=='\t')
		advance(p,1,str_type,error_1);
	if (*p!='/')
	{
		LM_ERR("no / found after primary type\n");
		goto error;
	}
	advance(p,1,str_type,error_1);
	while ((*p==' ' || *p=='\t') && p+1<str_type.s+str_type.len)
		advance(p,1,str_type,error_1);

	advance(p,3,str_type,error_1);
	x = READ(p-3) & 0x00ffffff;
	if (!one_of_8(x,sdp_))
		goto other;

	if (*p==';'||*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p==0) {
		LM_DBG("type <%.*s> found valid\n", (int)(p-str_type.s), str_type.s);
		return 1;
	} else {
		LM_ERR("bad end for type!\n");
		return -1;
	}

error_1:
	LM_ERR("body ended :-(!\n");
error:
	return -1;
other:
	LM_ERR("invalid type for a message\n");
	return -1;
}


/*
 * Get message body and check Content-Type header field
 */
int extract_body(struct sip_msg *msg, str *body )
{
	char c;
	int skip;
	
	body->s = get_body(msg);
	if (body->s==0) {
		LM_ERR("failed to get the message body\n");
		goto error;
	}
	
	/* better use the content-len value - no need of any explicit
	 * parcing as get_body() parsed all headers and Conten-Length
	 * body header is automaticaly parsed when found */
	body->len = get_content_length(msg);
	if (body->len==0) {
		LM_ERR("message body has length zero\n");
		goto error;
	}
	
	/* no need for parse_headers(msg, EOH), get_body will 
	 * parse everything */
	/*is the content type correct?*/
	if (check_content_type(msg)==-1)
	{
		LM_ERR("content type mismatching\n");
		goto error;
	}

	for (skip = 0; skip < body->len; skip++) {
		c = body->s[body->len - skip - 1];
		if (c != '\r' && c != '\n')
			break;
	}
	if (skip == body->len) {
		LM_ERR("empty body");
		goto error;
	}
	body->len -= skip;

	/*LM_DBG("DEBUG:extract_body:%d=|%.*s|\n",body->len,body->len,body->s);*/

	return 1;
error:
	return -1;
}

