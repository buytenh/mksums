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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <iv_avl.h>
#include <iv_list.h>
#include <string.h>
#include "hlsums_common.h"

static int inodes_linkable(const struct inode *a, const struct inode *b)
{
	if (a->st_dev != b->st_dev)
		return 0;
	if (a->st_mode != b->st_mode)
		return 0;
	if (a->st_uid != b->st_uid)
		return 0;
	if (a->st_gid != b->st_gid)
		return 0;
	if (a->st_size != b->st_size)
		return 0;

	return 1;
}

static int missing(const struct inode *ino)
{
	if (ino->st_nlink < ino->num_dentries)
		abort();

	return ino->st_nlink - ino->num_dentries;
}

static int better_leader(const struct inode *a, const struct inode *b)
{
	int missing_a;
	int missing_b;

	missing_a = missing(a);
	missing_b = missing(b);
	if (missing_a > missing_b)
		return 1;
	if (missing_a < missing_b)
		return 0;

	if (a->st_nlink > b->st_nlink)
		return 1;
	if (a->st_nlink < b->st_nlink)
		return 0;

	if (a->st_ino < b->st_ino)
		return 1;

	return 0;
}

static int try_link(char *from, char *to)
{
	static const char *tempfile = "zufequohshuel8Aihoovie9ooMiegiiJ";
	int ret;

	ret = link(to, tempfile);
	if (ret < 0) {
		fprintf(stderr, "linking %s: %s\n", to, strerror(errno));
		return -1;
	}

	ret = rename(tempfile, from);
	if (ret < 0) {
		fprintf(stderr, "renaming %s: %s\n", from, strerror(errno));
		unlink(tempfile);
		return -1;
	}

	ret = unlink(tempfile);
	if (ret == 0) {
		fprintf(stderr, "unexpected hard links: %s / %s\n", from, to);
		abort();
	}

	return 0;
}

static void do_link_inodes(struct inode *leader, struct inode *ino)
{
	struct dentry *dleader;
	struct iv_list_head *lh;
	struct iv_list_head *lh2;

	dleader = iv_container_of(leader->dentries.next, struct dentry, list);

	iv_list_for_each_safe (lh, lh2, &ino->dentries) {
		struct dentry *d;

		d = iv_container_of(lh, struct dentry, list);
		if (!try_link(d->name, dleader->name)) {
			iv_list_del(&d->list);
			iv_list_add_tail(&d->list, &leader->dentries);
		}
	}
}

void link_inodes(struct iv_avl_tree *inodes, int *need_nl)
{
	segment_inodes(inodes, need_nl, "hl",
		       inodes_linkable, better_leader, NULL, do_link_inodes);
}
