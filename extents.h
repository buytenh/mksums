/*
 * replika, a set of tools for dealing with hashmapped disk images
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

#ifndef __EXTENTS_H
#define __EXTENTS_H

#include <stdint.h>
#include <iv_avl.h>

int extent_tree_build(struct iv_avl_tree *extents, int fd);
int extent_tree_diff(struct iv_avl_tree *a, uint64_t aoff,
		     struct iv_avl_tree *b, uint64_t boff, uint64_t length);
void extent_tree_free(struct iv_avl_tree *extents);


#endif
