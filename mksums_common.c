/*
 * mksums, a tool for hashing all files in a directory tree
 * Copyright (C) 2015, 2016 Lennert Buytenhek
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <iv_list.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "mksums_common.h"

int openat_try_noatime(int dirfd, const char *pathname, int flags)
{
	int fd;

	fd = openat(dirfd, pathname, flags | O_RDONLY | O_NOFOLLOW | O_NOATIME);
	if (fd < 0 && errno == EPERM)
		fd = openat(dirfd, pathname, flags | O_RDONLY | O_NOFOLLOW);

	return fd;
}

void print_dir_path(FILE *fp, struct dir *dir)
{
	if (dir->parent != NULL) {
		print_dir_path(fp, dir->parent);
		putc('/', fp);
	}
	fprintf(fp, "%s", dir->name);
}

static void queue_free_dir(struct dir **dir_chain, struct dir *dir)
{
	if (dir->dirfd != -1) {
		if (dir->parent != NULL)
			queue_free_dir(dir_chain, dir->parent);

		close(dir->dirfd);
		dir->dirfd = -1;

		dir->parent = *dir_chain;
		*dir_chain = dir;
	}
}

void free_file_chain(struct iv_list_head *files)
{
	struct dir *dir_chain;

	dir_chain = NULL;
	while (!iv_list_empty(files)) {
		struct file_to_hash *fh;

		fh = iv_container_of(files->next, struct file_to_hash, list);
		iv_list_del(&fh->list);

		queue_free_dir(&dir_chain, fh->dir);

		free(fh);
	}

	while (dir_chain != NULL) {
		struct dir *dir;

		dir = dir_chain;
		dir_chain = dir->parent;

		free(dir);
	}
}

void run_threads(void *(*handler)(void *), void *cookie, int nthreads)
{
	pthread_attr_t attr;
	int ret;
	pthread_t tid[nthreads];
	int i;

	ret = pthread_attr_init(&attr);
	if (ret) {
		fprintf(stderr, "pthread_attr_init: %s\n", strerror(ret));
		exit(1);
	}

	ret = pthread_attr_setstacksize(&attr, 536870912);
	if (ret) {
		fprintf(stderr, "pthread_attr_setstacksize: %s\n",
			strerror(ret));
		exit(1);
	}

	for (i = 0; i < nthreads; i++) {
		ret = pthread_create(tid + i, &attr, handler, cookie);
		if (ret) {
			fprintf(stderr, "pthread_create: %s\n", strerror(ret));
			exit(1);
		}
	}

	ret = pthread_attr_destroy(&attr);
	if (ret) {
		fprintf(stderr, "pthread_attr_destroy: %s\n", strerror(ret));
		exit(1);
	}

	for (i = 0; i < nthreads; i++) {
		ret = pthread_join(tid[i], NULL);
		if (ret) {
			fprintf(stderr, "pthread_join: %s\n", strerror(ret));
			exit(1);
		}
	}
}
