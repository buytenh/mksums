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
#include <sys/resource.h>
#include <unistd.h>
#include "read_sum_files.h"

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
	if (ret < 0)
		fprintf(stderr, "renaming %s: %s\n", from, strerror(errno));

	unlink(tempfile);
}

static void link_hash(struct hash *h)
{
	char *linkto;
	struct iv_list_head *lh;

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

static void make_hardlinks(struct iv_avl_tree *hashes)
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
	struct rlimit rlim;
	struct iv_avl_tree hashes;

	if (getrlimit(RLIMIT_STACK, &rlim) < 0) {
		perror("getrlimit(RLIMIT_STACK)");
		return 1;
	}

	if (rlim.rlim_cur < 536870912 && 536870912 <= rlim.rlim_max) {
		rlim.rlim_cur = 536870912;
		setrlimit(RLIMIT_STACK, &rlim);
	}

	if (read_sum_files(&hashes, argc - 1, argv + 1))
		return 1;

	make_hardlinks(&hashes);

	free_hashes(&hashes);

	return 0;
}
