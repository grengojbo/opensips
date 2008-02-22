/*
 * $Id: pidf.c 1953 2007-04-04 08:50:33Z anca_vamanu $
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
 *  2007-04-14  initial version (anca)
 */
#define _XOPEN_SOURCE

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include "../../dprint.h"
#include "../../sr_module.h"
#include "pidf.h"

xmlAttrPtr xmlNodeGetAttrByName(xmlNodePtr node, const char *name)
{
	xmlAttrPtr attr = node->properties;
	while (attr) {
		if (xmlStrcasecmp(attr->name, (unsigned char*)name) == 0)
			return attr;
		attr = attr->next;
	}
	return NULL;
}

char *xmlNodeGetAttrContentByName(xmlNodePtr node, const char *name)
{
	xmlAttrPtr attr = xmlNodeGetAttrByName(node, name);
	if (attr)
		return (char*)xmlNodeGetContent(attr->children);
	else
		return NULL;
}

xmlNodePtr xmlNodeGetChildByName(xmlNodePtr node, const char *name)
{
	xmlNodePtr cur = node->children;
	while (cur) {
		if (xmlStrcasecmp(cur->name, (unsigned char*)name) == 0)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

xmlNodePtr xmlNodeGetNodeByName(xmlNodePtr node, const char *name,
															const char *ns)
{
	xmlNodePtr cur = node;
	while (cur) {
		xmlNodePtr match = NULL;
		if (xmlStrcasecmp(cur->name, (unsigned char*)name) == 0) {
			if (!ns || (cur->ns && xmlStrcasecmp(cur->ns->prefix,
							(unsigned char*)ns) == 0))
				return cur;
		}
		match = xmlNodeGetNodeByName(cur->children, name, ns);
		if (match)
			return match;
		cur = cur->next;
	}
	return NULL;
}

char *xmlNodeGetNodeContentByName(xmlNodePtr root, const char *name,
		const char *ns)
{
	xmlNodePtr node = xmlNodeGetNodeByName(root, name, ns);
	if (node)
		return (char*)xmlNodeGetContent(node->children);
	else
		return NULL;
}

xmlNodePtr xmlDocGetNodeByName(xmlDocPtr doc, const char *name, const char *ns)
{
	xmlNodePtr cur = doc->children;
	return xmlNodeGetNodeByName(cur, name, ns);
}

char *xmlDocGetNodeContentByName(xmlDocPtr doc, const char *name, 
		const char *ns)
{
	xmlNodePtr node = xmlDocGetNodeByName(doc, name, ns);
	if (node)
		return (char*)xmlNodeGetContent(node->children);
	else
		return NULL;
}

time_t xml_parse_dateTime(char* xml_time_str)
{
	struct tm tm;
	char * p;
	int h, m;
	char h1, h2, m1, m2;
	int sign= 1;
	signed int timezone_diff= 0;

	p= strptime(xml_time_str, "%F", &tm);
	if(p== NULL)
	{
		printf("error: failed to parse time\n");
		return 0;
	}
	p++;
	p= strptime(p, "%T", &tm);
	if(p== NULL)
	{
		printf("error: failed to parse time\n");
		return 0;
	}
	
	if(*p== '\0')
		goto done;

	if(*p== '.')
	{
		p++;
		/* read the fractionar part of the seconds*/
		while(*p!= '\0' && *p>= '0' && *p<= '9')
		{
			p++;
		}
	}

	if(*p== '\0')
		goto done;

	
	/* read time zone */

	if(*p== 'Z')
	{
		goto done;
	}

	if(*p== '+')
		sign= -1;

	p++;

	sscanf(p, "%c%c:%c%c", &h1, &h2, &m1, &m2);
	
	h= (h1- '0')*10+ h2- '0';
	m= (m1- '0')*10+ m2- '0';

	timezone_diff= sign* ((m+ h* 60)* 60);

done:
	return (mktime(&tm) + timezone_diff);	
}


