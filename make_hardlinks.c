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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "hlsums_common.h"

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
};

static int
compare_inodes(const struct iv_avl_node *_a, const struct iv_avl_node *_b)
{
	const struct inode *a = iv_container_of(_a, struct inode, an);
	const struct inode *b = iv_container_of(_b, struct inode, an);

	if (a->st_dev < b->st_dev)
		return -1;
	if (a->st_dev > b->st_dev)
		return 1;

	if (a->st_ino < b->st_ino)
		return -1;
	if (a->st_ino > b->st_ino)
		return 1;

	return 0;
}

static struct inode *
find_inode(struct iv_avl_tree *tree, dev_t st_dev, ino_t st_ino)
{
	struct iv_avl_node *an;

	an = tree->root;
	while (an != NULL) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		if (st_dev == ino->st_dev && st_ino == ino->st_ino)
			return ino;

		if (st_dev < ino->st_dev)
			an = an->left;
		else if (st_dev > ino->st_dev)
			an = an->right;
		else if (st_ino < ino->st_ino)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

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
find_dest_inode(struct iv_avl_tree *inodes, struct inode *inofirst)
{
	struct inode *inodest;
	struct iv_avl_node *an;

	inodest = inofirst;
	iv_avl_tree_for_each (an, inodes) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		if (!inodes_mergeable(ino, inodest))
			continue;

		if (prefer_inode(ino, inodest))
			inodest = ino;
	}

	return inodest;
}

static void print_inode(struct inode *ino, struct inode *inodest)
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
		(ino == inodest) ? " <==" : "",
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
	struct inode *inodest;
	int printed_inodest;
	struct dentry *dto;
	struct iv_avl_node *an;
	struct iv_avl_node *an2;

	inodest = find_dest_inode(inodes,
				  iv_container_of(iv_avl_tree_min(inodes),
						  struct inode, an));

	printed_inodest = 0;

	dto = iv_container_of(inodest->dentries.next, struct dentry, list);
	iv_avl_tree_for_each_safe (an, an2, inodes) {
		struct inode *ino;
		struct iv_list_head *lh;
		struct iv_list_head *lh2;

		ino = iv_container_of(an, struct inode, an);

		if (!inodes_mergeable(ino, inodest))
			continue;
		iv_avl_tree_delete(inodes, an);

		if (ino == inodest)
			continue;

		if (!printed_inodest) {
			print_inode(inodest, inodest);
			printed_inodest = 1;
		}
		print_inode(ino, inodest);

		iv_list_for_each_safe (lh, lh2, &ino->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);
			iv_list_del(&d->list);

			try_link(d->name, dto->name);

			iv_list_add_tail(&d->list, &h->dentries);
		}
	}

	if (printed_inodest)
		fprintf(stderr, "\n");
}

static void link_hash(struct hash *h)
{
	struct iv_avl_tree inodes;
	struct iv_list_head *lh;
	struct iv_list_head *lh2;

	INIT_IV_AVL_TREE(&inodes, compare_inodes);

	iv_list_for_each_safe (lh, lh2, &h->dentries) {
		struct dentry *d;
		struct stat buf;
		int ret;
		struct inode *ino;

		d = iv_container_of(lh, struct dentry, list);

		ret = stat(d->name, &buf);
		if (ret < 0) {
			if (errno != ENOENT) {
				fprintf(stderr, "stat %s: %s\n", d->name,
					strerror(errno));
			}
			continue;
		}

		if (!buf.st_size)
			continue;

		iv_list_del(&d->list);

		ino = find_inode(&inodes, buf.st_dev, buf.st_ino);
		if (ino != NULL) {
			ino->num_dentries++;
			iv_list_add_tail(&d->list, &ino->dentries);
			continue;
		}

		ino = alloca(sizeof(*ino));
		if (ino == NULL)
			abort();

		ino->st_dev = buf.st_dev;
		ino->st_ino = buf.st_ino;
		ino->st_mode = buf.st_mode;
		ino->st_nlink = buf.st_nlink;
		ino->st_uid = buf.st_uid;
		ino->st_gid = buf.st_gid;
		ino->st_size = buf.st_size;
		ino->num_dentries = 1;
		INIT_IV_LIST_HEAD(&ino->dentries);
		iv_list_add_tail(&d->list, &ino->dentries);

		iv_avl_tree_insert(&inodes, &ino->an);
	}

	fprintf(stderr, "\n");

	while (!iv_avl_tree_empty(&inodes))
		merge_inodes(h, &inodes);
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

		link_hash(h);
	}

	fprintf(stderr, "\rmerging done                                 "
			"                                               "
			"                                            \n");
}
