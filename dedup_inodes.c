/*
 * mksums, a tool for hashing all files in a directory tree
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <iv_avl.h>
#include <iv_list.h>
#include <linux/fs.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "hlsums_common.h"

#ifndef FIDEDUPERANGE
#define FIDEDUPERANGE	_IOWR(0x94, 54, struct file_dedupe_range)

#define FILE_DEDUPE_RANGE_SAME		0
#define FILE_DEDUPE_RANGE_DIFFERS	1

struct file_dedupe_range_info {
	int64_t		dest_fd;
	uint64_t	dest_offset;
	uint64_t	bytes_deduped;
	int32_t		status;
	uint32_t	reserved;
};

struct file_dedupe_range {
	uint64_t	src_offset;
	uint64_t	src_length;
	uint16_t	dest_count;
	uint16_t	reserved1;
	uint32_t	reserved2;
	struct file_dedupe_range_info	info[0];
};
#endif

static void try_open_inodes(struct iv_avl_tree *inodes)
{
	int opened_readonly;
	struct iv_avl_node *an;

	opened_readonly = 0;

	iv_avl_tree_for_each (an, inodes) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		struct iv_list_head *lh;

		ino->readonly = 0;
		ino->fd = -1;

		iv_list_for_each (lh, &ino->dentries) {
			struct dentry *d;
			int fd;

			d = iv_container_of(lh, struct dentry, list);

			fd = open(d->name, O_RDWR);
			if (fd < 0 && errno == EACCES && !opened_readonly) {
				opened_readonly = 1;
				ino->readonly = 1;
				fd = open(d->name, O_RDONLY);
			}

			if (fd >= 0) {
				ino->fd = fd;
				break;
			}

			if (errno != EACCES) {
				fprintf(stderr, "error opening %s: %s\n",
					d->name, strerror(errno));
			}
		}
	}
}

static int inodes_dedupable(const struct inode *a, const struct inode *b)
{
	if (a->st_dev != b->st_dev)
		return 0;

	return 1;
}

static int better_block_source(const struct inode *a, const struct inode *b)
{
	if (a->readonly > b->readonly)
		return 1;
	if (a->readonly < b->readonly)
		return 0;

	if (a->st_ino < b->st_ino)
		return 1;

	return 0;
}

static void do_dedup_inodes(struct inode *leader, struct inode *ino)
{
	struct {
		struct file_dedupe_range r;
		struct file_dedupe_range_info ri;
	} x;

	if (leader->fd == -1 || ino->fd == -1 || ino->readonly)
		return;

	x.r.src_offset = 0;
	x.r.src_length = leader->st_size;
	x.r.dest_count = 1;
	x.r.reserved1 = 0;
	x.r.reserved2 = 0;
	x.ri.dest_fd = ino->fd;
	x.ri.dest_offset = 0;
	x.ri.bytes_deduped = 0;
	x.ri.status = 0;
	x.ri.reserved = 0;

	if (ioctl(leader->fd, FIDEDUPERANGE, &x) < 0) {
		perror("ioctl");
		return;
	}

	if (x.ri.status == FILE_DEDUPE_RANGE_DIFFERS) {
		fprintf(stderr, "welp, data differs\n");
		return;
	}

	if (x.ri.status != FILE_DEDUPE_RANGE_SAME) {
		fprintf(stderr, "FIDEDUPERANGE: %s\n", strerror(-x.ri.status));
		return;
	}

	if (x.ri.bytes_deduped != leader->st_size)
		fprintf(stderr, "welp, didn't dedupe a whole block\n");
}

static void close_inodes(struct iv_avl_tree *inodes)
{
	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, inodes) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		if (ino->fd != -1)
			close(ino->fd);
	}
}

void dedup_inodes(struct iv_avl_tree *inodes, int *need_nl)
{
	try_open_inodes(inodes);

	segment_inodes(inodes, need_nl, "dd",
		       inodes_dedupable, better_block_source, do_dedup_inodes);

	close_inodes(inodes);
}
