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
#include <iv_avl.h>
#include <iv_list.h>
#include "common.h"

struct fh_ref
{
	struct iv_avl_node	an;
	struct file_to_hash	*fh;
};

static int compare_fh_refs(struct iv_avl_node *_a, struct iv_avl_node *_b)
{
	const struct fh_ref *a = iv_container_of(_a, struct fh_ref, an);
	const struct fh_ref *b = iv_container_of(_b, struct fh_ref, an);

	if (a->fh->d_ino < b->fh->d_ino)
		return -1;
	if (a->fh->d_ino > b->fh->d_ino)
		return 1;
	return 0;
}

static struct fh_ref *find_ref(struct iv_avl_tree *tree, ino_t d_ino)
{
	struct iv_avl_node *an;

	an = tree->root;
	while (an != NULL) {
		struct fh_ref *ref;

		ref = iv_container_of(an, struct fh_ref, an);
		if (d_ino == ref->fh->d_ino)
			return ref;

		if (d_ino < ref->fh->d_ino)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

void find_hard_links(struct iv_list_head *files)
{
	struct iv_avl_tree fh_refs;
	struct iv_list_head *lh;

	INIT_IV_AVL_TREE(&fh_refs, compare_fh_refs);

	iv_list_for_each (lh, files) {
		struct file_to_hash *fh;
		struct fh_ref *ref;

		fh = iv_container_of(lh, struct file_to_hash, list);

		ref = find_ref(&fh_refs, fh->d_ino);
		if (ref == NULL) {
			ref = alloca(sizeof(*ref));
			ref->fh = fh;
			iv_avl_tree_insert(&fh_refs, &ref->an);

			fh->state = STATE_NOTYET;
		} else {
			fh->state = STATE_BACKREF;
			fh->backref = ref->fh;
		}
	}
}
