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

void scan_inodes(struct hash *h, void *cookie,
		 void (*cb)(void *cookie, struct iv_avl_tree *inodes))
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

	cb(cookie, &inodes);
}
