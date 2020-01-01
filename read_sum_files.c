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
#include <obstack.h>
#include <string.h>
#include "hlsums_common.h"

static int
compare_hash(const struct iv_avl_node *_a, const struct iv_avl_node *_b)
{
	const struct hash *a = iv_container_of(_a, struct hash, an);
	const struct hash *b = iv_container_of(_b, struct hash, an);

	return memcmp(a->hash, b->hash, sizeof(a->hash));
}

static int hextoval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');

	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');

	return -1;
}

static int parse_hash(uint8_t *hash, char *text)
{
	int i;

	for (i = 0; i < 64; i++) {
		int val;
		int val2;

		val = hextoval(text[2 * i]);
		if (val < 0)
			return 1;

		val2 = hextoval(text[2 * i + 1]);
		if (val2 < 0)
			return 1;

		hash[i] = (val << 4) | val2;
	}

	return 0;
}

static struct hash *find_hash(struct iv_avl_tree *tree, uint8_t *hash)
{
	struct iv_avl_node *an;

	an = tree->root;
	while (an != NULL) {
		struct hash *h;
		int ret;

		h = iv_container_of(an, struct hash, an);

		ret = memcmp(hash, h->hash, sizeof(h->hash));
		if (ret == 0)
			return h;

		if (ret < 0)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}

static void add_dentry(struct hash *h, char *name, int namelen)
{
	struct dentry *d;

	d = malloc(sizeof(*d) + namelen + 1);
	if (d == NULL)
		abort();

	strcpy(d->name, name);

	iv_list_add_tail(&d->list, &h->dentries);
}


struct hash_1ref
{
	struct iv_avl_node	an;
	uint8_t			hash[64];
	char			name[0];
};

#define obstack_chunk_alloc	malloc
#define obstack_chunk_free	free

int read_sum_files(struct iv_avl_tree *dst, int num_files, char *file[])
{
	struct iv_avl_tree hash_1ref;
	struct obstack pool;
	int i;

	INIT_IV_AVL_TREE(dst, compare_hash);

	INIT_IV_AVL_TREE(&hash_1ref, compare_hash);

	obstack_init(&pool);
	obstack_chunk_size(&pool) = 131072;

	for (i = 0; i < num_files; i++) {
		FILE *fp;
		char mapbuf[1048576];

		fp = fopen(file[i], "r");
		if (fp == NULL) {
			perror("fopen");
			return 1;
		}

		setbuffer(fp, mapbuf, sizeof(mapbuf));

		while (1) {
			char line[2048];
			int len;
			uint8_t hash[64];
			struct hash *h;
			struct hash_1ref *h1;

			if (fgets(line, sizeof(line), fp) == NULL) {
				if (!feof(fp)) {
					perror("fgets");
					return 1;
				}
				break;
			}

			len = strlen(line);
			if (len && line[len - 1] == '\n')
				line[--len] = 0;

			if (len < 131 || parse_hash(hash, line)) {
				fprintf(stderr, "error parsing line: %s\n",
					line);
				continue;
			}

			h = find_hash(dst, hash);
			if (h != NULL) {
				add_dentry(h, line + 130, len - 130);
				continue;
			}

			h1 = (struct hash_1ref *)find_hash(&hash_1ref, hash);
			if (h1 != NULL) {
				h = malloc(sizeof(*h));
				if (h == NULL)
					abort();
				memcpy(h->hash, hash, sizeof(h->hash));
				INIT_IV_LIST_HEAD(&h->dentries);
				iv_avl_tree_insert(dst, &h->an);

				add_dentry(h, h1->name, strlen(h1->name));
				add_dentry(h, line + 130, len - 130);

				iv_avl_tree_delete(&hash_1ref, &h1->an);

				continue;
			}

			h1 = obstack_alloc(&pool, sizeof(*h1) + len - 130 + 1);
			if (h1 == NULL)
				abort();

			memcpy(h1->hash, hash, sizeof(h1->hash));
			strcpy(h1->name, line + 130);
			iv_avl_tree_insert(&hash_1ref, &h1->an);
		}

		fclose(fp);
	}

	obstack_free(&pool, NULL);

	return 0;
}
