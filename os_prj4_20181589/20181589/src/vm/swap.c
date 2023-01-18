#include "vm/swap.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "devices/block.h"
const size_t PAGE_BLOCK_CNT = PGSIZE / BLOCK_SECTOR_SIZE; //No. of blocks per page
void swap_init()
{
	bitmap_swap = bitmap_create(1024*8);
}

void swap_in(size_t used_index, void *kaddr)
{
	struct block *swap_partition = block_get_role(BLOCK_SWAP);
	int swap_in_flag=0;
	swap_in_flag++;
	if(!bitmap_test(bitmap_swap, used_index)){
		return;
	}
	else {
		struct block * tmp = malloc(sizeof(struct block *));
		for(int i=0; i<PAGE_BLOCK_CNT; i++){
			block_read(swap_partition, PAGE_BLOCK_CNT*used_index+i, BLOCK_SECTOR_SIZE*i+kaddr);
		}
		bitmap_reset(bitmap_swap, used_index);
		free(tmp);
		return;
	}
	
}

size_t swap_out(void *kaddr)
{
	struct block *swap_partition = block_get_role(BLOCK_SWAP);
	int swap_out_flag=0;
	swap_out_flag++;
	size_t swap_idx = bitmap_scan(bitmap_swap, 0, 1, false);
	if(swap_idx != BITMAP_ERROR){
		struct block * tmp = malloc(sizeof(struct block *));
		for(int i=0; i<PAGE_BLOCK_CNT; i++){
			block_write(swap_partition, PAGE_BLOCK_CNT*swap_idx+i, BLOCK_SECTOR_SIZE*i+kaddr);
		}
		bitmap_set(bitmap_swap, swap_idx, true);
		free(tmp);
	}
	return swap_idx;
}
