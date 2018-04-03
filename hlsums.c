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
#include <getopt.h>
#include <iv_avl.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include "hlsums_common.h"

static int do_link;
static int do_dedup;

static void process_inode_set(void *_need_nl, struct iv_avl_tree *inodes)
{
	int *need_nl = _need_nl;

	if (do_link)
		link_inodes(inodes, need_nl);

	if (do_dedup)
		dedup_inodes(inodes, need_nl);
}

static void link_dedup(struct iv_avl_tree *hashes)
{
	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, hashes) {
		struct hash *h;
		char dispbuf[256];
		int i;
		int need_nl;

		h = iv_container_of(an, struct hash, an);

		strcpy(dispbuf, "\rmerging ");
		for (i = 0; i < sizeof(h->hash) && i < 8; i++)
			sprintf(dispbuf + 2 * i + 9, "%.2x", h->hash[i]);

		fputs(dispbuf, stderr);

		need_nl = 1;
		scan_inodes(h, &need_nl, process_inode_set);
	}

	fprintf(stderr, "\rmerging done            \n");
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
	static struct option long_options[] = {
		{ "dedup", no_argument, 0, 'd', },
		{ "link", no_argument, 0, 'l', },
		{ 0, 0, 0, 0, },
	};
	struct rlimit rlim;
	struct iv_avl_tree hashes;

	while (1) {
		int c;

		c = getopt_long(argc, argv, "dl", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'd':
			do_dedup = 1;
			break;

		case 'l':
			do_link = 1;
			break;

		case '?':
			return 1;

		default:
			abort();
		}
	}

	if (argc == optind) {
		fprintf(stderr, "%s: [--dedup] [--link] [sumfile]+\n", argv[0]);
		return 1;
	}

	if (!do_dedup && !do_link)
		do_link = 1;

	if (getrlimit(RLIMIT_STACK, &rlim) < 0) {
		perror("getrlimit(RLIMIT_STACK)");
		return 1;
	}

	if (rlim.rlim_cur < 536870912 && 536870912 <= rlim.rlim_max) {
		rlim.rlim_cur = 536870912;
		setrlimit(RLIMIT_STACK, &rlim);
	}

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
		perror("getrlimit");
		return 1;
	}

	if (geteuid() == 0 && rlim.rlim_max < 1048576)
		rlim.rlim_max = 1048576;

	if (rlim.rlim_cur < rlim.rlim_max) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rlim);
	}

	if (read_sum_files(&hashes, argc - optind, argv + optind))
		return 1;

	link_dedup(&hashes);

	free_hashes(&hashes);

	return 0;
}
