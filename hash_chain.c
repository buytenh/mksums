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
#include <iv_list.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "mksums_common.h"

static int hash_file(struct file_to_hash *fh, int xattr_cache_hash)
{
	int fd;
	struct stat statbuf;
	SHA512_CTX c;

	fd = openat_try_noatime(fh->dir->dirfd, fh->d_name, 0);
	if (fd < 0) {
		int err = errno;

		fprintf(stderr, "error opening ");
		print_dir_path(stderr, fh->dir);
		fprintf(stderr, "/%s: %s\n", fh->d_name, strerror(err));
		return 1;
	}

	if (xattr_cache_hash) {
		uint8_t sha512[12 + 64];

		if (fstat(fd, &statbuf) < 0) {
			perror("fstat");
			close(fd);
			return 1;
		}

		if (fgetxattr(fd, "user.sha512", sha512,
			      sizeof(sha512)) == sizeof(sha512)) {
			uint64_t sec;
			uint32_t nsec;

			sec = (((uint64_t)sha512[0]) << 56) |
			      (((uint64_t)sha512[1]) << 48) |
			      (((uint64_t)sha512[2]) << 40) |
			      (((uint64_t)sha512[3]) << 32) |
			      (((uint64_t)sha512[4]) << 24) |
			      (((uint64_t)sha512[5]) << 16) |
			      (((uint64_t)sha512[6]) <<  8) |
			      (((uint64_t)sha512[7]) <<  0);

			nsec = (((uint64_t)sha512[ 8]) << 24) |
			       (((uint64_t)sha512[ 9]) << 16) |
			       (((uint64_t)sha512[10]) <<  8) |
			       (((uint64_t)sha512[11]) <<  0);

			if (sec == statbuf.st_mtim.tv_sec &&
			    nsec == statbuf.st_mtim.tv_nsec) {
				memcpy(fh->hash, sha512 + 12, 64);
				close(fd);
				return 0;
			}
		}
	}

	SHA512_Init(&c);

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

		SHA512_Update(&c, buf, ret);

		if (ret < sizeof(buf))
			break;
	}

	SHA512_Final(fh->hash, &c);

	if (xattr_cache_hash) {
		struct stat statbuf2;

		if (fstat(fd, &statbuf2) < 0) {
			perror("fstat");
			close(fd);
			return 0;
		}

		if (statbuf.st_mtim.tv_sec == statbuf2.st_mtim.tv_sec &&
		    statbuf.st_mtim.tv_nsec == statbuf2.st_mtim.tv_nsec) {
			uint8_t sha512[12 + 64];

			sha512[ 0] = (statbuf.st_mtim.tv_sec >> 56) & 0xff;
			sha512[ 1] = (statbuf.st_mtim.tv_sec >> 48) & 0xff;
			sha512[ 2] = (statbuf.st_mtim.tv_sec >> 40) & 0xff;
			sha512[ 3] = (statbuf.st_mtim.tv_sec >> 32) & 0xff;
			sha512[ 4] = (statbuf.st_mtim.tv_sec >> 24) & 0xff;
			sha512[ 5] = (statbuf.st_mtim.tv_sec >> 16) & 0xff;
			sha512[ 6] = (statbuf.st_mtim.tv_sec >>  8) & 0xff;
			sha512[ 7] = (statbuf.st_mtim.tv_sec >>  0) & 0xff;
			sha512[ 8] = (statbuf.st_mtim.tv_nsec >> 24) & 0xff;
			sha512[ 9] = (statbuf.st_mtim.tv_nsec >> 16) & 0xff;
			sha512[10] = (statbuf.st_mtim.tv_nsec >>  8) & 0xff;
			sha512[11] = (statbuf.st_mtim.tv_nsec >>  0) & 0xff;
			memcpy(sha512 + 12, fh->hash, 64);

			fsetxattr(fd, "user.sha512", sha512, sizeof(sha512), 0);
		}
	}

	close(fd);

	return 0;
}


struct hash_state
{
	struct iv_list_head	*files;
	int			xattr_cache_hash;

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
			fh->state = hash_file(fh, hs->xattr_cache_hash) ?
					STATE_FAILED : STATE_OK;
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

void hash_chain(struct iv_list_head *files, int xattr_cache_hash)
{
	struct hash_state hs;

	hs.files = files;
	hs.xattr_cache_hash = xattr_cache_hash;
	pthread_mutex_init(&hs.lock, NULL);
	hs.prehash = files;
	hs.preprint = files;

	run_threads(hash_thread, &hs, 2 * sysconf(_SC_NPROCESSORS_ONLN));

	pthread_mutex_destroy(&hs.lock);
}
