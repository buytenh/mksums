/*
 * replika, a set of tools for dealing with hashmapped disk images
 * Copyright (C) 2017 Lennert Buytenhek
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

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <iv_avl.h>
#include <iv_list.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include "extents.h"

#define EXTENTS_BATCH		16384

struct extent
{
	struct iv_avl_node	an;
	uint64_t		fe_logical;
	uint64_t		fe_physical;
	uint64_t		fe_length;
};

static int
compare_extents(const struct iv_avl_node *_a, const struct iv_avl_node *_b)
{
	const struct extent *a = iv_container_of(_a, struct extent, an);
	const struct extent *b = iv_container_of(_b, struct extent, an);

	if (a->fe_logical < b->fe_logical)
		return -1;
	if (a->fe_logical > b->fe_logical)
		return 1;

	return 0;
}

static int can_merge(struct extent *last, struct fiemap_extent *fe)
{
	if (last == NULL)
		return 0;

	if (last->fe_logical + last->fe_length != fe->fe_logical)
		return 0;

	if (last->fe_physical + last->fe_length != fe->fe_physical)
		return 0;

	return 1;
}

int extent_tree_build(struct iv_avl_tree *extents, int fd)
{
	uint64_t off;
	struct extent *last;

	INIT_IV_AVL_TREE(extents, compare_extents);

	off = 0;
	last = NULL;

	while (off < UINT64_MAX) {
		struct {
			struct fiemap f;
			struct fiemap_extent fe[EXTENTS_BATCH];
		} req;
		int i;

		req.f.fm_start = off;
		req.f.fm_length = UINT64_MAX;
		req.f.fm_flags = 0;
		req.f.fm_extent_count = EXTENTS_BATCH;

		if (ioctl(fd, FS_IOC_FIEMAP, &req) < 0) {
			perror("ioctl(FS_IOC_FIEMAP)");
			return -1;
		}

		if (req.f.fm_mapped_extents == 0)
			break;

		for (i = 0; i < req.f.fm_mapped_extents; i++) {
			struct fiemap_extent *fe = req.fe + i;

			if (!(fe->fe_flags & FIEMAP_EXTENT_UNKNOWN)) {
				if (can_merge(last, fe)) {
					last->fe_length += fe->fe_length;
				} else {
					last = malloc(sizeof(*last));
					if (last == NULL)
						abort();

					last->fe_logical = fe->fe_logical;
					last->fe_physical = fe->fe_physical;
					last->fe_length = fe->fe_length;
					iv_avl_tree_insert(extents, &last->an);
				}
			}

			off = fe->fe_logical + fe->fe_length;

			if (fe->fe_flags & FIEMAP_EXTENT_LAST) {
				off = UINT64_MAX;
				break;
			}
		}
	}

	return 0;
}

static struct extent *find_extent(struct iv_avl_tree *extents, uint64_t off)
{
	struct iv_avl_node *an;
	struct extent *best;

	best = NULL;

	an = extents->root;
	while (an != NULL) {
		struct extent *e;

		e = iv_container_of(an, struct extent, an);
		if (off == e->fe_logical)
			return e;

		if (off < e->fe_logical) {
			an = an->left;
		} else {
			best = e;
			an = an->right;
		}
	}

	if (best != NULL && off >= best->fe_logical + best->fe_length)
		best = NULL;

	return best;
}

static struct extent *find_next_extent(struct extent *e)
{
	struct iv_avl_node *an;

	an = iv_avl_tree_next(&e->an);
	if (an != NULL)
		return iv_container_of(an, struct extent, an);

	return NULL;
}

int extent_tree_diff(struct iv_avl_tree *a, uint64_t aoff,
		     struct iv_avl_tree *b, uint64_t boff, uint64_t length)
{
	struct extent *ae;
	struct extent *be;

	ae = find_extent(a, aoff);
	be = find_extent(b, boff);

	while (length) {
		uint64_t adelta;
		uint64_t bdelta;
		uint64_t toadvance;

		if (ae == NULL || aoff < ae->fe_logical)
			return 1;

		if (be == NULL || boff < be->fe_logical)
			return 1;

		adelta = aoff - ae->fe_logical;
		bdelta = boff - be->fe_logical;

		if (ae->fe_physical + adelta != be->fe_physical + bdelta)
			return 1;

		toadvance = length;
		if (toadvance > ae->fe_length - adelta)
			toadvance = ae->fe_length - adelta;
		if (toadvance > be->fe_length - bdelta)
			toadvance = be->fe_length - bdelta;

		aoff += toadvance;
		if (aoff == ae->fe_logical + ae->fe_length)
			ae = find_next_extent(ae);

		boff += toadvance;
		if (boff == be->fe_logical + be->fe_length)
			be = find_next_extent(be);

		length -= toadvance;
	}

	return 0;
}

static void __free_element(struct iv_avl_node *an)
{
	struct extent *e;

	e = iv_container_of(an, struct extent, an);

	if (e->an.left != NULL)
		__free_element(e->an.left);
	if (e->an.right != NULL)
		__free_element(e->an.right);
	free(e);
}

void extent_tree_free(struct iv_avl_tree *extents)
{
	if (extents->root != NULL)
		__free_element(extents->root);
}
