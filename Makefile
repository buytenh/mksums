all:		hlsums mksums

clean:
		rm -f hlsums
		rm -f mksums

hlsums:		hlsums.c dedup_inodes.c extents.c extents.h hlsums_common.h make_hardlinks.c read_sum_files.c scan_inodes.c segment_inodes.c
		gcc -D_FILE_OFFSET_BITS=64 -O3 -Wall -g -o hlsums hlsums.c dedup_inodes.c extents.c make_hardlinks.c read_sum_files.c scan_inodes.c segment_inodes.c `pkg-config --cflags --libs ivykis`

mksums:		mksums.c find_hard_links.c hash_chain.c mksums_common.c mksums_common.h scan_tree.c
		gcc -D_FILE_OFFSET_BITS=64 -O3 -Wall -g -pthread -o mksums mksums.c find_hard_links.c hash_chain.c mksums_common.c scan_tree.c -lcrypto `pkg-config --cflags --libs ivykis`
