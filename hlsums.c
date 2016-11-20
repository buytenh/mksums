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
#include <unistd.h>

struct hash
{
	struct iv_avl_node	an;
	uint8_t			hash[20];
	struct iv_list_head	dentries;
};

struct dentry
{
	struct iv_list_head	list;
	char			name[0];
};

static int
compare_hash(const struct iv_avl_node *_a, const struct iv_avl_node *_b)
{
	const struct hash *a = iv_container_of(_a, struct hash, an);
	const struct hash *b = iv_container_of(_b, struct hash, an);

	return memcmp(a->hash, b->hash, 20);
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

	for (i = 0; i < 20; i++) {
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

		ret = memcmp(hash, h->hash, 20);
		if (ret == 0)
			return h;

		if (ret < 0)
			an = an->left;
		else
			an = an->right;
	}

	return NULL;
}


static struct iv_avl_tree hash_single;
static struct iv_avl_tree hash_multiple;

static struct hash *get_hash(uint8_t *hash)
{
	struct hash *h;

	h = find_hash(&hash_multiple, hash);
	if (h != NULL)
		return h;

	h = find_hash(&hash_single, hash);
	if (h != NULL) {
		iv_avl_tree_delete(&hash_single, &h->an);
		iv_avl_tree_insert(&hash_multiple, &h->an);
		return h;
	}

	h = malloc(sizeof(*h));
	if (h == NULL)
		abort();

	memcpy(h->hash, hash, 20);
	INIT_IV_LIST_HEAD(&h->dentries);
	iv_avl_tree_insert(&hash_single, &h->an);

	return h;
}

static int read_sum_file(char *file)
{
	FILE *fp;

	fp = fopen(file, "r");
	if (fp == NULL) {
		perror("fopen");
		return 1;
	}

	while (1) {
		char line[2048];
		int len;
		uint8_t hash[20];
		struct dentry *d;

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

		if (len < 43 || parse_hash(hash, line)) {
			fprintf(stderr, "error parsing line: %s\n", line);
			continue;
		}

		d = malloc(sizeof(*d) + len - 42 + 1);
		if (d == NULL)
			abort();

		strcpy(d->name, line + 42);

		iv_list_add_tail(&d->list, &get_hash(hash)->dentries);
	}

	fclose(fp);

	return 0;
}

static void try_link(char *from, char *to)
{
	static const char *tempfile = "zufequohshuel8Aihoovie9ooMiegiiJ";
	int ret;

	printf("linking %s to %s\n", from, to);

	ret = link(to, tempfile);
	if (ret < 0) {
		perror("link");
		return;
	}

	ret = rename(tempfile, from);
	if (ret < 0)
		perror("rename");

	unlink(tempfile);
}

static void make_hardlinks(void)
{
	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, &hash_multiple) {
		struct hash *h;
		char *linkto;
		struct iv_list_head *lh;

		h = iv_container_of(an, struct hash, an);

		linkto = NULL;
		iv_list_for_each (lh, &h->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);

			if (linkto == NULL) {
				linkto = d->name;
				continue;
			}

			try_link(d->name, linkto);
		}
	}
}

static void free_hashes(struct iv_avl_tree *hashes)
{
	struct iv_avl_node *an;
	struct iv_avl_node *an2;

	iv_avl_tree_for_each_safe (an, an2, hashes) {
		struct hash *h;
		struct iv_list_head *lh;
		struct iv_list_head *lh2;

		h = iv_container_of(an, struct hash, an);

		iv_list_for_each_safe (lh, lh2, &h->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);

			iv_list_del(&d->list);
			free(d);
		}

		iv_avl_tree_delete(hashes, &h->an);
		free(h);
	}
}

int main(int argc, char *argv[])
{
	int i;

	INIT_IV_AVL_TREE(&hash_single, compare_hash);
	INIT_IV_AVL_TREE(&hash_multiple, compare_hash);

	for (i = 1; i < argc; i++) {
		if (read_sum_file(argv[i]))
			return 1;
	}

	make_hardlinks();

	free_hashes(&hash_single);
	free_hashes(&hash_multiple);

	return 0;
}
