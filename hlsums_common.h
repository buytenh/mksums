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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct hash
{
	struct iv_avl_node	an;
	uint8_t			hash[64];
	struct iv_list_head	dentries;
};

struct dentry
{
	struct iv_list_head	list;
	char			name[0];
};

struct inode
{
	struct iv_avl_node	an;
	dev_t			st_dev;
	ino_t			st_ino;
	mode_t			st_mode;
	nlink_t			st_nlink;
	uid_t			st_uid;
	gid_t			st_gid;
	off_t			st_size;
	int			num_dentries;
	struct iv_list_head	dentries;

	char			visited;

	/* for dedup_inodes() */
	char			readonly;
	int			fd;
	struct iv_avl_tree	extents;
};

/* dedup_inodes.c */
void dedup_inodes(struct iv_avl_tree *inodes, int *need_nl);

/* make_hardlinks.c */
void link_inodes(struct iv_avl_tree *inodes, int *need_nl, int contents_only);

/* read_sum_files.c */
int read_sum_files(struct iv_avl_tree *dst, int num_files, char *file[]);

/* scan_inodes.c */
void scan_inodes(struct hash *h, void *cookie,
		 void (*cb)(void *cookie, struct iv_avl_tree *inodes));

/* segment_inodes.c */
void segment_inodes(struct iv_avl_tree *inodes, int *need_nl, char *task,
		    int (*inodes_equiv)(const struct inode *a,
					const struct inode *b),
		    int (*better_leader)(const struct inode *a,
					 const struct inode *b),
		    int (*can_pair)(struct inode *leader, struct inode *ino),
		    void (*found_equiv)(struct inode *leader,
					struct inode *ino));


#endif
