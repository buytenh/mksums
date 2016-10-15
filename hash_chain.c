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
#include <iv_list.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

static int hash_file(struct file_to_hash *fh)
{
	int fd;
	SHA_CTX c;

	fd = openat_try_noatime(fh->dir->dirfd, fh->d_name, 0);
	if (fd < 0) {
		int err = errno;

		fprintf(stderr, "error opening ");
		print_dir_path(stderr, fh->dir);
		fprintf(stderr, "/%s: %s\n", fh->d_name, strerror(err));
		return 1;
	}

	SHA1_Init(&c);

	while (1) {
		uint8_t buf[1048576];
		int ret;

		ret = read(fd, buf, sizeof(buf));
		if (ret < 0) {
			perror("read");
			close(fd);
			return 1;
		}

		if (ret == 0)
			break;

		SHA1_Update(&c, buf, ret);

		if (ret < sizeof(buf))
			break;
	}

	close(fd);

	SHA1_Final(fh->hash, &c);

	return 0;
}


struct hash_state
{
	struct iv_list_head	*files;
	pthread_mutex_t		lock;
	struct iv_list_head	*prehash;
	struct iv_list_head	*preprint;
};

static void *hash_thread(void *cookie)
{
	struct hash_state *hs = cookie;

	pthread_mutex_lock(&hs->lock);

	while (1) {
		struct iv_list_head *nxt;
		struct file_to_hash *fh;
		int flush;

		nxt = hs->prehash->next;
		if (nxt == hs->files)
			break;

		hs->prehash = nxt;
		fh = iv_container_of(nxt, struct file_to_hash, list);

		if (fh->state == STATE_NOTYET) {
			pthread_mutex_unlock(&hs->lock);
			fh->state = hash_file(fh) ? STATE_FAILED : STATE_OK;
			pthread_mutex_lock(&hs->lock);
		}

		flush = 0;
		while (hs->preprint != hs->prehash) {
			struct file_to_hash *fh_hash;

			fh = iv_container_of(hs->preprint->next,
					     struct file_to_hash, list);
			if (fh->state == STATE_NOTYET)
				break;

			hs->preprint = &fh->list;

			if (fh->state == STATE_BACKREF)
				fh_hash = fh->backref;
			else
				fh_hash = fh;

			if (fh_hash->state == STATE_OK) {
				int i;

				for (i = 0; i < sizeof(fh_hash->hash); i++)
					printf("%.2x", fh_hash->hash[i]);
				printf("  ");
				print_dir_path(stdout, fh->dir);
				printf("/%s\n", fh->d_name);

				flush = 1;
			}
		}

		if (flush)
			fflush(stdout);
	}

	pthread_mutex_unlock(&hs->lock);

	return NULL;
}

void hash_chain(struct iv_list_head *files)
{
	struct hash_state hs;

	hs.files = files;
	pthread_mutex_init(&hs.lock, NULL);
	hs.prehash = files;
	hs.preprint = files;

	run_threads(hash_thread, &hs, 2 * sysconf(_SC_NPROCESSORS_ONLN));

	pthread_mutex_destroy(&hs.lock);
}
