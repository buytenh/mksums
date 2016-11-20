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
#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "mksums_common.h"

int main(int argc, char *argv[])
{
	struct rlimit rlim;
	struct iv_list_head files;
	int i;

	if (getrlimit(RLIMIT_STACK, &rlim) < 0) {
		perror("getrlimit(RLIMIT_STACK)");
		return 1;
	}

	if (rlim.rlim_cur < 134217728 && 134217728 <= rlim.rlim_max) {
		rlim.rlim_cur = 134217728;
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

	INIT_IV_LIST_HEAD(&files);

	for (i = 1; i < argc; i++) {
		if (scan_tree(&files, argv[i]))
			return 0;
	}

	find_hard_links(&files);

	hash_chain(&files);

	free_file_chain(&files);

	return 0;
}
