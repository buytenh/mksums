all:		hlsums mksums

clean:
		rm -f hlsums
		rm -f mksums

hlsums:		hlsums.c hlsums_common.h read_sum_files.c
		gcc -O3 -Wall -g -o hlsums hlsums.c read_sum_files.c `pkg-config --cflags --libs ivykis`

mksums:		mksums.c find_hard_links.c hash_chain.c mksums_common.c mksums_common.h scan_tree.c
		gcc -D_FILE_OFFSET_BITS=64 -O3 -Wall -g -lcrypto -lpthread -o mksums mksums.c find_hard_links.c hash_chain.c mksums_common.c scan_tree.c `pkg-config --cflags --libs ivykis`
