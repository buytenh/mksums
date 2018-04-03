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
#include <iv_avl.h>
#include <iv_list.h>
#include <string.h>
#include "hlsums_common.h"

void segment_inodes(struct iv_avl_tree *inodes, int *need_nl, char *task,
		    int (*inodes_equiv)(const struct inode *a,
					const struct inode *b),
		    int (*better_leader)(const struct inode *a,
					 const struct inode *b),
		    int (*can_pair)(struct inode *leader, struct inode *ino),
		    void (*found_equiv)(struct inode *leader,
					struct inode *ino))
{
	struct inode *pick_leader(void)
	{
		struct inode *leader;
		struct iv_avl_node *an;

		leader = NULL;
		iv_avl_tree_for_each (an, inodes) {
			struct inode *ino;

			ino = iv_container_of(an, struct inode, an);

			if (ino->visited)
				continue;

			if (leader != NULL) {
				if (!inodes_equiv(ino, leader))
					continue;
				if (!better_leader(ino, leader))
					continue;
			}

			leader = ino;
		}

		return leader;
	}

	void print_inode(struct inode *ino, int is_leader)
	{
		struct iv_list_head *lh;
		char buf[128];

		if (iv_list_empty(&ino->dentries))
			return;

		strcpy(buf, "");
		if (is_leader)
			snprintf(buf, sizeof(buf), " <== (%s)", task);

		fprintf(stderr, " dev %.4lx ino %ld mode %lo nlink %ld"
				" uid %ld gid %ld size %lld%s%s\n",
			(long)ino->st_dev,
			(long)ino->st_ino,
			(long)ino->st_mode,
			(long)ino->st_nlink,
			(long)ino->st_uid,
			(long)ino->st_gid,
			(long long)ino->st_size,
			buf,
			(ino->st_nlink != ino->num_dentries) ?
				" (missing-refs)" : "");

		iv_list_for_each (lh, &ino->dentries) {
			struct dentry *d;

			d = iv_container_of(lh, struct dentry, list);
			fprintf(stderr, "  %s\n", d->name);
		}
	}

	void generate_equivs(struct inode *leader)
	{
		int printed_leader;
		struct iv_avl_node *an;

		leader->visited = 1;

		printed_leader = 0;
		iv_avl_tree_for_each (an, inodes) {
			struct inode *ino;

			ino = iv_container_of(an, struct inode, an);

			if (ino->visited)
				continue;

			if (!inodes_equiv(ino, leader))
				continue;

			ino->visited = 1;

			if (can_pair != NULL && !can_pair(leader, ino))
				continue;

			if (!printed_leader) {
				if (*need_nl) {
					fprintf(stderr, "\n");
					*need_nl = 0;
				}
				print_inode(leader, 1);

				printed_leader = 1;
			}

			print_inode(ino, 0);

			found_equiv(leader, ino);
		}

		if (printed_leader)
			fprintf(stderr, "\n");
	}


	struct iv_avl_node *an;

	iv_avl_tree_for_each (an, inodes) {
		struct inode *ino;

		ino = iv_container_of(an, struct inode, an);
		ino->visited = 0;
	}

	while (1) {
		struct inode *leader;

		leader = pick_leader();
		if (leader == NULL)
			break;

		generate_equivs(leader);
	}
}
