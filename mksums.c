/*
 * mksums, a tool for hashing all files in a directory tree
 * Copyright (C) 2016, 2023 Lennert Buytenhek
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
#include <getopt.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "mksums_common.h"

int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{ "xattr-cache-hash", no_argument, 0, 'x', },
		{ 0, 0, 0, 0, },
	};
	int xattr_cache_hash;
	struct rlimit rlim;
	struct iv_list_head files;
	int i;

	xattr_cache_hash = 0;

	while (1) {
		int c;

		c = getopt_long(argc, argv, "x", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'x':
			xattr_cache_hash = 1;
			break;

		case '?':
			return 1;

		default:
			abort();
		}
	}

	if (argc == optind) {
		fprintf(stderr, "%s: [--xattr-cache-hash] [dir]+\n", argv[0]);
		return 1;
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

	INIT_IV_LIST_HEAD(&files);

	for (i = optind; i < argc; i++) {
		if (scan_tree(&files, argv[i]))
			return 0;
	}

	find_hard_links(&files);

	hash_chain(&files, xattr_cache_hash);

	free_file_chain(&files);

	return 0;
}
