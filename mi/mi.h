/*
 * $Id$
 *
 * Copyright (C) 2006 Voice Sistem SRL
 *
 * This file is part of openser, a free SIP server.
 *
 * openser is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *
 * History:
 * ---------
 *  2006-09-08  first version (bogdan)
 */



#ifndef _MI_MI_H_
#define _MI_MI_H_

#include "../str.h"
#include "tree.h"

typedef struct mi_node* (mi_cmd_f)(struct mi_node*, void *param);
typedef int (mi_child_init_f)();

struct mi_cmd {
	int id;
	str name;
	mi_child_init_f *init_f;
	mi_cmd_f *f;
	void *param;
};


typedef struct mi_export_ {
	char *name;
	mi_cmd_f *cmd;
	void *param;
	mi_child_init_f *init_f;
}mi_export_t;


int register_mi_cmd( mi_cmd_f f, char *name, void *param, mi_child_init_f in);

int register_mi_mod( char *mod_name, mi_export_t *mis);

int init_mi_child();

struct mi_cmd* lookup_mi_cmd( char *name, int len);

static inline struct mi_node* run_mi_cmd(struct mi_cmd *cmd, struct mi_node *t)
{
	return cmd->f( t, cmd->param);
}

void get_mi_cmds( struct mi_cmd** cmds, int *size);

#endif

