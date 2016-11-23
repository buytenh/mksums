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

struct hash_group
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
compare_hash_group(const struct iv_avl_node *_a, const struct iv_avl_node *_b)
{
	const struct hash_group *a =
		iv_container_of(_a, struct hash_group, an);
	const struct hash_group *b =
		iv_container_of(_b, struct hash_group, an);

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

static struct hash_group *
find_hash_group(struct iv_avl_tree *tree, dev_t dev, ino_t ino)
{
	struct iv_avl_node *an;

	an = tree->root;
	while (an != NULL) {
		struct hash_group *hg;

		hg = iv_container_of(an, struct hash_group, an);
		if (dev == hg->st_dev && ino == hg->st_ino)
			return hg;

		if (dev < hg->st_dev)
			an = an->left;
		else if (dev > hg->st_dev)
			an = an->right;
		else if (ino < hg->st_ino)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

static int hash_groups_mergeable(struct hash_group *a, struct hash_group *b)
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

static int missing(struct hash_group *hg)
{
	if (hg->st_nlink < hg->num_dentries)
		abort();

	return hg->st_nlink - hg->num_dentries;
}

static int prefer_hash_group(struct hash_group *a, struct hash_group *b)
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

static struct hash_group *
find_dest_group(struct iv_avl_tree *hash_groups, struct hash_group *hgfirst)
{
	struct hash_group *hgdest;
	struct iv_avl_node *an;

	hgdest = hgfirst;
	iv_avl_tree_for_each (an, hash_groups) {
		struct hash_group *hg;

		hg = iv_container_of(an, struct hash_group, an);
		if (!hash_groups_mergeable(hg, hgdest))
			continue;

		if (prefer_hash_group(hg, hgdest))
			hgdest = hg;
	}

	return hgdest;
}

static void print_hash_group(struct hash_group *hg, struct hash_group *hgdest)
{
	struct iv_list_head *lh;

	fprintf(stderr, " dev %.4lx ino %ld mode %lo nlink %ld"
			" uid %ld gid %ld size %lld%s%s\n",
		(long)hg->st_dev,
		(long)hg->st_ino,
		(long)hg->st_mode,
		(long)hg->st_nlink,
		(long)hg->st_uid,
		(long)hg->st_gid,
		(long long)hg->st_size,
		(hg == hgdest) ? " <==" : "",
		(hg->st_nlink != hg->num_dentries) ? " (missing-refs)" : "");

	iv_list_for_each (lh, &hg->dentries) {
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

static void merge_hash_groups(struct hash *h, struct iv_avl_tree *hash_groups)
{
	struct hash_group *hgdest;
	int printed_hgdest;
	struct dentry *dto;
	struct iv_avl_node *an;
	struct iv_avl_node *an2;

	hgdest = find_dest_group(hash_groups,
				 iv_container_of(iv_avl_tree_min(hash_groups),
						 struct hash_group, an));

	printed_hgdest = 0;

	dto = iv_container_of(hgdest->dentries.next, struct dentry, list);
	iv_avl_tree_for_each_safe (an, an2, hash_groups) {
		struct hash_group *hg;
		struct iv_list_head *lh;
		struct iv_list_head *lh2;

		hg = iv_container_of(an, struct hash_group, an);

		if (!hash_groups_mergeable(hg, hgdest))
			continue;
		iv_avl_tree_delete(hash_groups, an);

		if (hg == hgdest)
			continue;

		if (!printed_hgdest) {
			print_hash_group(hgdest, hgdest);
			printed_hgdest = 1;
		}
		print_hash_group(hg, hgdest);

		iv_list_for_each_safe (lh, lh2, &hg->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);
			iv_list_del(&d->list);

			try_link(d->name, dto->name);

			iv_list_add_tail(&d->list, &h->dentries);
		}
	}

	if (printed_hgdest)
		fprintf(stderr, "\n");
}

static void link_hash(struct hash *h)
{
	struct iv_avl_tree hash_groups;
	struct iv_list_head *lh;
	struct iv_list_head *lh2;

	INIT_IV_AVL_TREE(&hash_groups, compare_hash_group);

	iv_list_for_each_safe (lh, lh2, &h->dentries) {
		struct dentry *d;
		struct stat buf;
		int ret;
		struct hash_group *hg;

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

		hg = find_hash_group(&hash_groups, buf.st_dev, buf.st_ino);
		if (hg != NULL) {
			hg->num_dentries++;
			iv_list_add_tail(&d->list, &hg->dentries);
			continue;
		}

		hg = alloca(sizeof(*hg));
		if (hg == NULL)
			abort();

		hg->st_dev = buf.st_dev;
		hg->st_ino = buf.st_ino;
		hg->st_mode = buf.st_mode;
		hg->st_nlink = buf.st_nlink;
		hg->st_uid = buf.st_uid;
		hg->st_gid = buf.st_gid;
		hg->st_size = buf.st_size;
		hg->num_dentries = 1;
		INIT_IV_LIST_HEAD(&hg->dentries);
		iv_list_add_tail(&d->list, &hg->dentries);

		iv_avl_tree_insert(&hash_groups, &hg->an);
	}

	fprintf(stderr, "\n");

	while (!iv_avl_tree_empty(&hash_groups))
		merge_hash_groups(h, &hash_groups);
}

void make_hardlinks(struct iv_avl_tree *hashes)
{
	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, hashes) {
		struct hash *h;
		char dispbuf[50];
		int i;

		h = iv_container_of(an, struct hash, an);

		strcpy(dispbuf, "\rmerging ");
		for (i = 0; i < 20; i++)
			sprintf(dispbuf + 2 * i + 9, "%.2x", h->hash[i]);

		fputs(dispbuf, stderr);

		link_hash(h);
	}

	fprintf(stderr, "\rmerging done                                    \n");
}
