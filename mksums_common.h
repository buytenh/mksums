/*
 * mksums, a tool for hashing all files in a directory tree
 * Copyright (C) 2015, 2016, 2023 Lennert Buytenhek
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

#ifndef __COMMON_H
#define __COMMON_H

#include <dirent.h>
#include <iv_list.h>
#include <sys/types.h>

struct dir
{
	struct dir		*parent;
	int			dirfd;
	char			name[0];
};

enum state {
	STATE_NOTYET,
	STATE_BACKREF,
	STATE_OK,
	STATE_FAILED,
};

struct file_to_hash
{
	struct iv_list_head	list;
	struct dir		*dir;
	ino_t			d_ino;
	enum state		state;
	union {
		uint8_t			hash[64];
		struct file_to_hash	*backref;
	};
	char			d_name[0];
};

/* find_hard_links.c */
void find_hard_links(struct iv_list_head *files);

/* hash_chain.c */
void hash_chain(struct iv_list_head *files, int xattr_cache_hash);

/* mksums_common.c */
int openat_try_noatime(int dirfd, const char *pathname, int flags);
void print_dir_path(FILE *fp, struct dir *dir);
void free_file_chain(struct iv_list_head *files);
void run_threads(void *(*handler)(void *), void *cookie, int nthreads);

/* scan_tree.c */
int scan_tree(struct iv_list_head *files, char *root_name);


#endif
