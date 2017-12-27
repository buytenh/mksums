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

static int inodes_mergeable(struct inode *a, struct inode *b)
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

static int missing(struct inode *ino)
{
	if (ino->st_nlink < ino->num_dentries)
		abort();

	return ino->st_nlink - ino->num_dentries;
}

static int prefer_inode(struct inode *a, struct inode *b)
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

static struct inode *
pick_leader(struct iv_avl_tree *inodes, struct inode *inofirst)
{
	struct inode *leader;
	struct iv_avl_node *an;

	leader = inofirst;
	iv_avl_tree_for_each (an, inodes) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		if (!inodes_mergeable(ino, leader))
			continue;

		if (prefer_inode(ino, leader))
			leader = ino;
	}

	return leader;
}

static void print_inode(struct inode *ino, struct inode *leader)
{
	struct iv_list_head *lh;

	fprintf(stderr, " dev %.4lx ino %ld mode %lo nlink %ld"
			" uid %ld gid %ld size %lld%s%s\n",
		(long)ino->st_dev,
		(long)ino->st_ino,
		(long)ino->st_mode,
		(long)ino->st_nlink,
		(long)ino->st_uid,
		(long)ino->st_gid,
		(long long)ino->st_size,
		(ino == leader) ? " <==" : "",
		(ino->st_nlink != ino->num_dentries) ? " (missing-refs)" : "");

	iv_list_for_each (lh, &ino->dentries) {
		struct dentry *d;

		d = iv_container_of(lh, struct dentry, list);
		fprintf(stderr, "  %s\n", d->name);
	}
}

static void try_link(char *from, char *to)
{
	static const char *tempfile = "zufequohshuel8Aihoovie9ooMiegiiJ";
	int ret;

	ret = link(to, tempfile);
	if (ret < 0) {
		fprintf(stderr, "linking %s: %s\n", to, strerror(errno));
		return;
	}

	ret = rename(tempfile, from);
	if (ret < 0) {
		fprintf(stderr, "renaming %s: %s\n", from, strerror(errno));
		unlink(tempfile);
		return;
	}

	ret = unlink(tempfile);
	if (ret == 0) {
		fprintf(stderr, "unexpected hard links: %s / %s\n", from, to);
		abort();
	}
}

static void merge_inodes(struct hash *h, struct iv_avl_tree *inodes)
{
	struct inode *leader;
	int printed_leader;
	struct dentry *dto;
	struct iv_avl_node *an;
	struct iv_avl_node *an2;

	leader = pick_leader(inodes,
			     iv_container_of(iv_avl_tree_min(inodes),
					     struct inode, an));

	printed_leader = 0;

	dto = iv_container_of(leader->dentries.next, struct dentry, list);
	iv_avl_tree_for_each_safe (an, an2, inodes) {
		struct inode *ino;
		struct iv_list_head *lh;
		struct iv_list_head *lh2;

		ino = iv_container_of(an, struct inode, an);

		if (!inodes_mergeable(ino, leader))
			continue;
		iv_avl_tree_delete(inodes, an);

		if (ino == leader)
			continue;

		if (!printed_leader) {
			print_inode(leader, leader);
			printed_leader = 1;
		}
		print_inode(ino, leader);

		iv_list_for_each_safe (lh, lh2, &ino->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);
			iv_list_del(&d->list);

			try_link(d->name, dto->name);

			iv_list_add_tail(&d->list, &h->dentries);
		}
	}

	if (printed_leader)
		fprintf(stderr, "\n");
}

static void link_hash(void *_h, struct iv_avl_tree *inodes)
{
	struct hash *h = _h;

	fprintf(stderr, "\n");

	while (!iv_avl_tree_empty(inodes))
		merge_inodes(h, inodes);
}

void make_hardlinks(struct iv_avl_tree *hashes)
{
	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, hashes) {
		struct hash *h;
		char dispbuf[256];
		int i;

		h = iv_container_of(an, struct hash, an);

		strcpy(dispbuf, "\rmerging ");
		for (i = 0; i < sizeof(h->hash); i++)
			sprintf(dispbuf + 2 * i + 9, "%.2x", h->hash[i]);

		fputs(dispbuf, stderr);

		scan_inodes(h, h, link_hash);
	}

	fprintf(stderr, "\rmerging done                                 "
			"                                               "
			"                                            \n");
}
