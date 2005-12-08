/*
 * $Id$
 *
 * Copyright (c) 2004 Juha Heinanen
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
 */

#include <stdlib.h>
#include <string.h>
#include "../dprint.h"
#include "../mem/mem.h"
#include "parse_allow.h"
#include "parse_methods.h"
#include "msg_parser.h"

 
/*
 * This method is used to parse Allow HF body.
 *
 * params: msg : sip msg
 * returns 0 on success,
 *        -1 on failure.
 */
int parse_allow(struct sip_msg *msg)
{
	unsigned int *methods;

	if (!msg->allow && (parse_headers(msg,HDR_ALLOW_F,0)==-1 || !msg->allow))
		return -1;

	/* maybe the header is already parsed! */
	if (msg->allow->parsed)
		return 0;

	/* bad luck! :-( - we have to parse it */
	methods = pkg_malloc(sizeof(unsigned int));
	if (methods == 0) {
		LOG(L_ERR, "ERROR:parse_allow: Out of pkg_memory\n");
		return -1;
	}

	if (parse_methods(&(msg->allow->body), methods)!=0) {
		LOG(L_ERR, "ERROR:parse_allow: Bad allow body header\n"); 
		return -1;
	}

	msg->allow->parsed = (void*)methods;
	return 0;
}

