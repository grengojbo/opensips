/*
 * $Id$
 *
 * SDP parser helpers
 *
 * Copyright (C) 2008 SOMA Networks, INC.
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
 *
 * History:
 * --------
 * 2007-09-09 ported helper functions from nathelper module (osas)
 *
 */


#ifndef _SDP_HLPR_FUNCS_H
#define  _SDP_HLPR_FUNCS_H

#include "../../str.h"
#include "../msg_parser.h"

/**
 * Detect the mixed part delimiter.
 *
 * Example: "boundary1"
 * Content-Type: multipart/mixed; boundary="boundary1"
 */
int get_mixed_part_delimiter(str * body, str * mp_delimiter);

int extract_rtpmap(str *body, str *rtpmap_payload, str *rtpmap_encoding, str *rtpmap_clockrate, str *rtpmap_parmas);
int extract_ptime(str *body, str *ptime);
int extract_sendrecv_mode(str *body, str *sendrecv_mode);
int extract_mediaip(str *body, str *mediaip, int *pf, char *line);
int extract_media_attr(str *body, str *mediamedia, str *mediaport, str *mediatransport, str *mediapayload);

char *find_sdp_line(char *p, char *plimit, char linechar);
char *find_next_sdp_line(char *p, char *plimit, char linechar, char *defptr);

char* get_sdp_hdr_field(char* , char* , struct hdr_field* );

char *find_sdp_line_delimiter(char *p, char *plimit, str delimiter);
char *find_next_sdp_line_delimiter(char *p, char *plimit, str delimiter, char *defptr); 
#endif