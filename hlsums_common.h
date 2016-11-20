/*
 * mksums, a tool for hashing all files in a directory tree
 * Copyright (C) 2016 Lennert Buytenhek
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version
 * 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License version 2.1 along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __HLSUMS_COMMON_H
#define __HLSUMS_COMMON_H

#include <iv_avl.h>
#include <iv_list.h>
#include <stdint.h>

struct hash
{
	struct iv_avl_node	an;
	uint8_t			hash[20];
	struct iv_list_head	dentries;
};

struct dentry
{
	struct iv_list_head	list;
	char			name[0];
};

/* read_sum_files.c */
int read_sum_files(struct iv_avl_tree *dst, int num_files, char *file[]);


#endif
