/*
 * $Id$
 *
 * SDP parser interface
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
 * HISTORY:
 * --------
 * 2007-09-09 osas: ported and enhanced sdp parsing functions from nathelper module
 *
 */


#ifndef SDP_H
#define SDP_H

#include "../msg_parser.h"

typedef struct sdp_payload_attr {
	struct sdp_payload_attr *next;
	int payload_num; /**< payload index inside stream */
	str rtp_payload;
	str rtp_enc;
	str rtp_clock;
	str rtp_params;
	str sendrecv_mode;
	str ptime;
} sdp_payload_attr_t;

typedef struct sdp_stream_cell {
	struct sdp_stream_cell *next;
	/* c=<network type> <address type> <connection address> */
	int pf;         /**< connection address family: AF_INET/AF_INET6 */
	str ip_addr;    /**< connection address */
	int stream_num; /**< stream index inside a session */
	/* m=<media> <port> <transport> <payloads> */
	str media;
	str port;
	str transport;
	str payloads;
	int payloads_num;                         /**< number of payloads inside a stream */
	struct sdp_payload_attr **p_payload_attr; /**< fast access pointers to payloads */
	struct sdp_payload_attr *payload_attr;
} sdp_stream_cell_t;

typedef struct sdp_session_cell {
	struct sdp_session_cell *next;
	int session_num;  /**< session index inside sdp */
	str cnt_disp;     /**< the Content-Disposition header (for Content-Type:multipart/mixed) */
	int streams_num;  /**< number of streams inside a session */
	struct sdp_stream_cell*  streams;
} sdp_session_cell_t;

/**
 * Here we hold the head of the parsed sdp structure
 */
typedef struct sdp_info {
	int sessions_num;	/**< number of SDP sessions */
	struct sdp_session_cell *sessions;
} sdp_info_t;


/*
 * Parse SDP.
 */
int parse_sdp(struct sip_msg* _m);

/**
 * Get a session for the current sip msg based on position inside SDP.
 */
sdp_session_cell_t* get_sdp_session(struct sip_msg* _m, int session_num);
/**
 * Get a session for the given sdp based on position inside SDP.
 */
sdp_session_cell_t* get_sdp_session_sdp(struct sdp_info* sdp, int session_num);

/**
 * Get a stream for the current sip msg based on positions inside SDP.
 */
sdp_stream_cell_t* get_sdp_stream(struct sip_msg* _m, int session_num, int stream_num);
/**
 * Get a stream for the given sdp based on positions inside SDP.
 */
sdp_stream_cell_t* get_sdp_stream_sdp(struct sdp_info* sdp, int session_num, int stream_num);

/**
 * Get a payload from a stream based on payload.
 */
sdp_payload_attr_t* get_sdp_payload4payload(sdp_stream_cell_t *stream, str *rtp_payload);

/**
 * Get a payload from a stream based on position.
 */
sdp_payload_attr_t* get_sdp_payload4index(sdp_stream_cell_t *stream, int index);

/**
 * Free all memory associated with parsed structure.
 *
 * Note: this will free up the parsed sdp structure (form PKG_MEM).
 */
void free_sdp(sdp_info_t** _sdp);

/**
 * Clone the given sdp_session_cell structure.
 */
sdp_session_cell_t * clone_sdp_session_cell(sdp_session_cell_t *session);
/**
 * Free all memory associated with the cloned sdp_session structure.
 *
 * Note: this will free up the parsed sdp structure (form SHM_MEM).
 */
void free_cloned_sdp_session(sdp_session_cell_t *_session);

/**
 * Clone the given sdp_info structure.
 *
 * Note: all cloned structer will be in SHM_MEM.
 */
sdp_info_t* clone_sdp_info(struct sip_msg* _m);
/**
 * Free all memory associated with the cloned sdp_info structure.
 *
 * Note: this will free up the parsed sdp structure (form SHM_MEM).
 */
void free_cloned_sdp(sdp_info_t* sdp);

/**
 * Print the content of the given sdp_info structure.
 *
 * Note: only for debug purposes.
 */
void print_sdp(sdp_info_t* sdp);
/**
 * Print the content of the given sdp_session structure.
 *
 * Note: only for debug purposes.
 */
void print_sdp_session(sdp_session_cell_t* sdp_session);

#endif /* SDP_H */