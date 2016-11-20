all:		hlsums mksums

clean:
		rm -f hlsums
		rm -f mksums

hlsums:		hlsums.c read_sum_files.c read_sum_files.h
		gcc -O3 -Wall -g -o hlsums hlsums.c read_sum_files.c `pkg-config --cflags --libs ivykis`

mksums:		mksums.c common.c common.h find_hard_links.c hash_chain.c scan_tree.c
		gcc -D_FILE_OFFSET_BITS=64 -O3 -Wall -g -lcrypto -lpthread -o mksums mksums.c common.c find_hard_links.c hash_chain.c scan_tree.c `pkg-config --cflags --libs ivykis`
