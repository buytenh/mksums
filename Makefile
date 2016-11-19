all:		hardlink_sums mksums

clean:
		rm -f hardlink_sums
		rm -f mksums

hardlink_sums:	hardlink_sums.c
		gcc -O3 -Wall -g -o hardlink_sums hardlink_sums.c `pkg-config --cflags --libs ivykis`

mksums:		mksums.c common.c common.h find_hard_links.c hash_chain.c scan_tree.c
		gcc -D_FILE_OFFSET_BITS=64 -O3 -Wall -g -lcrypto -lpthread -o mksums mksums.c common.c find_hard_links.c hash_chain.c scan_tree.c `pkg-config --cflags --libs ivykis`
