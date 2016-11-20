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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <iv_avl.h>
#include <iv_list.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "mksums_common.h"

struct scan_state
{
	pthread_mutex_t		lock;
	pthread_cond_t		cond;
	struct iv_list_head	*files;
	struct iv_avl_tree	dirs_to_scan;
	ino_t			last_dir_inode_scanned;
	int			threads_scanning;
};

struct dir_to_scan
{
	struct iv_avl_node	an;
	struct iv_list_head	list;
	struct dir		*dir;
	ino_t			d_ino;
};

struct temp_dir_entry
{
	struct iv_avl_node	an;
	char			*d_name;
	unsigned char		d_type;
	union {
		struct dir_to_scan	*ds;
		struct file_to_hash	*fh;
	};
};

static int compare_dirs_to_scan(const struct iv_avl_node *_a,
				const struct iv_avl_node *_b)
{
	const struct dir_to_scan *a;
	const struct dir_to_scan *b;

	a = iv_container_of(_a, struct dir_to_scan, an);
	b = iv_container_of(_b, struct dir_to_scan, an);

	if (a->d_ino < b->d_ino)
		return -1;
	if (a->d_ino > b->d_ino)
		return 1;
	return 0;
}

static struct dir_to_scan *pick_dir(struct scan_state *st)
{
	struct iv_avl_node *an;
	struct dir_to_scan *ds;

	an = st->dirs_to_scan.root;
	while (1) {
		struct iv_avl_node *an2;

		ds = iv_container_of(an, struct dir_to_scan, an);
		if (st->last_dir_inode_scanned < ds->d_ino)
			an2 = an->left;
		else
			an2 = an->right;

		if (an2 == NULL)
			break;

		an = an2;
	}

	if (ds->d_ino < st->last_dir_inode_scanned) {
		an = iv_avl_tree_next(an);
		if (an == NULL)
			an = iv_avl_tree_min(&st->dirs_to_scan);

		ds = iv_container_of(an, struct dir_to_scan, an);
	}

	st->last_dir_inode_scanned = ds->d_ino;

	return ds;
}

static int compare_temp_dir_entries(const struct iv_avl_node *_a,
				    const struct iv_avl_node *_b)
{
	const struct temp_dir_entry *a;
	const struct temp_dir_entry *b;

	a = iv_container_of(_a, struct temp_dir_entry, an);
	b = iv_container_of(_b, struct temp_dir_entry, an);

	return strcmp(a->d_name, b->d_name);
}

static void scan_one_dir(struct dir_to_scan *ds, struct iv_avl_tree *dirs,
			 struct iv_list_head *fhs)
{
	int dirfd;
	DIR *dird;
	struct iv_avl_tree ent_tree;
	struct iv_avl_node *an;

	dirfd = dup(ds->dir->dirfd);
	if (dirfd < 0) {
		perror("dup");
		exit(1);
	}

	dird = fdopendir(dirfd);
	if (dird == NULL) {
		perror("fdopendir");
		exit(1);
	}

	INIT_IV_AVL_TREE(&ent_tree, compare_temp_dir_entries);

	while (1) {
		struct dirent *ent;
		ino_t d_ino;
		unsigned char d_type;
		int len;
		struct temp_dir_entry *e;

		errno = 0;

		ent = readdir(dird);
		if (ent == NULL) {
			if (errno) {
				perror("readdir");
				exit(1);
			}
			break;
		}

		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		d_ino = ent->d_ino;
		d_type = ent->d_type;

		if (d_ino == 0xffffffff || d_type == DT_UNKNOWN) {
			struct stat buf;

			if (fstatat(ds->dir->dirfd, ent->d_name, &buf,
				    AT_SYMLINK_NOFOLLOW) < 0) {
				perror("fstatat");
				exit(1);
			}

			d_ino = buf.st_ino;
			d_type = IFTODT(buf.st_mode);
		}

		if (d_type != DT_DIR && d_type != DT_REG)
			continue;

		len = strlen(ent->d_name);

		e = alloca(sizeof(*e));
		if (e == NULL)
			abort();

		e->d_type = d_type;

		if (d_type == DT_DIR) {
			struct dir *dir;

			dir = malloc(sizeof(struct dir) + len + 1);
			if (dir == NULL)
				abort();
			dir->parent = ds->dir;
			dir->dirfd = -1;
			strcpy(dir->name, ent->d_name);

			e->ds = malloc(sizeof(struct dir_to_scan));
			if (e->ds == NULL)
				abort();
			e->ds->dir = dir;
			e->ds->d_ino = d_ino;

			e->d_name = dir->name;
		} else if (d_type == DT_REG) {
			e->fh = malloc(sizeof(struct file_to_hash) + len + 1);
			if (e->fh == NULL)
				abort();
			e->fh->dir = ds->dir;
			e->fh->d_ino = d_ino;
			e->fh->state = STATE_NOTYET;
			memset(e->fh->hash, 0, sizeof(e->fh->hash));
			strcpy(e->fh->d_name, ent->d_name);

			e->d_name = e->fh->d_name;
		}

		iv_avl_tree_insert(&ent_tree, &e->an);
	}

	closedir(dird);

	iv_avl_tree_for_each (an, &ent_tree) {
		struct temp_dir_entry *e;

		e = iv_container_of(an, struct temp_dir_entry, an);

		if (e->d_type == DT_DIR) {
			int fd;

			fd = openat_try_noatime(ds->dir->dirfd,
						e->ds->dir->name,
						O_DIRECTORY);
			if (fd < 0) {
				int err = errno;

				fprintf(stderr, "error opening ");
				print_dir_path(stderr, ds->dir);
				fprintf(stderr, "/%s: %s\n",
					e->ds->dir->name, strerror(err));

				free(e->ds->dir);
				free(e->ds);

				continue;
			}

			e->ds->dir->dirfd = fd;

			iv_avl_tree_insert(dirs, &e->ds->an);
			iv_list_add_tail(&e->ds->list, fhs);
		} else if (e->d_type == DT_REG) {
			iv_list_add_tail(&e->fh->list, fhs);
		}
	}
}

static void *scan_thread(void *cookie)
{
	struct scan_state *st = cookie;

	pthread_mutex_lock(&st->lock);

	while (1) {
		struct dir_to_scan *ds;
		struct iv_avl_tree dirs;
		struct iv_list_head fhs;

		while (st->dirs_to_scan.root == NULL && st->threads_scanning)
			pthread_cond_wait(&st->cond, &st->lock);

		if (st->dirs_to_scan.root == NULL)
			break;

		ds = pick_dir(st);
		if (ds == NULL)
			abort();

		iv_avl_tree_delete(&st->dirs_to_scan, &ds->an);

		st->threads_scanning++;

		pthread_mutex_unlock(&st->lock);

		INIT_IV_AVL_TREE(&dirs, compare_dirs_to_scan);
		INIT_IV_LIST_HEAD(&fhs);
		scan_one_dir(ds, &dirs, &fhs);

		pthread_mutex_lock(&st->lock);

		st->threads_scanning--;

		if (st->dirs_to_scan.root == NULL &&
		    (dirs.root != NULL || !st->threads_scanning)) {
			pthread_cond_broadcast(&st->cond);
		}

		while (dirs.root != NULL) {
			struct iv_avl_node *an;

			an = dirs.root;
			iv_avl_tree_delete(&dirs, an);
			iv_avl_tree_insert(&st->dirs_to_scan, an);
		}

		iv_list_splice(&fhs, &ds->list);
		iv_list_del(&ds->list);

		free(ds);
	}

	pthread_mutex_unlock(&st->lock);

	return NULL;
}

int scan_tree(struct iv_list_head *files, char *root_name)
{
	int dirfd;
	struct scan_state st;
	struct dir *rootdir;
	struct dir_to_scan *rootds;

	dirfd = openat_try_noatime(AT_FDCWD, root_name, O_DIRECTORY);
	if (dirfd < 0)
		return 1;

	pthread_mutex_init(&st.lock, NULL);
	pthread_cond_init(&st.cond, NULL);
	st.files = files;
	INIT_IV_AVL_TREE(&st.dirs_to_scan, compare_dirs_to_scan);
	st.last_dir_inode_scanned = 0;
	st.threads_scanning = 0;

	rootdir = malloc(sizeof(*rootdir) + strlen(root_name) + 1);
	if (rootdir == NULL)
		abort();
	rootdir->parent = NULL;
	rootdir->dirfd = dirfd;
	strcpy(rootdir->name, root_name);

	rootds = malloc(sizeof(*rootds));
	if (rootds == NULL)
		abort();
	iv_list_add_tail(&rootds->list, files);
	rootds->dir = rootdir;
	rootds->d_ino = 1;
	iv_avl_tree_insert(&st.dirs_to_scan, &rootds->an);

	run_threads(scan_thread, &st, 128);

	pthread_mutex_destroy(&st.lock);
	pthread_cond_destroy(&st.cond);

	return 0;
}
