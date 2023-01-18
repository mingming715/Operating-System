#include "filesys/cache.h"
#include "threads/malloc.h"
#include <stdlib.h>
#include <string.h>
#define NUM_CACHE 64		//Buffer cache entry 개수 (32KB)

static uint8_t *p_buffer_cache;
static struct buffer_cache_entry cache[NUM_CACHE];
static struct buffer_cache_entry *clock_hand;	//Victim entry 선정 시 clock algorithm을 위한 변수
static struct lock buffer_cache_lock;

void buffer_cache_init(void)
{
	p_buffer_cache = (uint8_t *)malloc(sizeof(uint8_t) * NUM_CACHE * BLOCK_SECTOR_SIZE);

	for(int i=0; i<NUM_CACHE; i++)
	{
		memset(&cache[i], 0, sizeof(struct buffer_cache_entry));
		cache[i].data = p_buffer_cache + (BLOCK_SECTOR_SIZE * i);
		lock_init(&(cache[i].lock));
	}

	clock_hand = cache;
	lock_init(&buffer_cache_lock);
}

bool buffer_cache_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs)
{
	struct buffer_cache_entry *lookup_res;
	lookup_res = buffer_cache_lookup(sector_idx);
	int read_cnt = 0;
	if(lookup_res == NULL){
		lookup_res = select_victim();
		if(lookup_res == NULL) return false;

		lock_acquire(&lookup_res->lock);

		lookup_res->sector = sector_idx;
		lookup_res->used = true;

		block_read(fs_device, sector_idx, lookup_res->data);
		read_cnt++;
		lock_release(&lookup_res->lock);
	}

	lock_acquire(&lookup_res->lock);
	memcpy(buffer + bytes_read, lookup_res->data + sector_ofs, chunk_size);
	for(int i=0; i<5; i++)
		read_cnt ++;
	lookup_res->clock_bit = true;
	lock_release(&lookup_res->lock);

	read_cnt = 0;
	return true;
}

bool buffer_cache_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs)
{
	bool success = false;
	struct buffer_cache_entry *lookup_res;
	int write_cnt = 0;
	lookup_res = buffer_cache_lookup(sector_idx);
	if(lookup_res == NULL){
		lookup_res = select_victim();
		if(lookup_res == NULL) return success;

		lock_acquire(&lookup_res->lock);

		lookup_res->sector = sector_idx;
		lookup_res->used = true;

		block_read(fs_device, sector_idx, lookup_res->data);
		write_cnt++;
		lock_release(&lookup_res->lock);
	}

	lock_acquire(&lookup_res->lock);
	lookup_res->dirty = true;
	lookup_res->clock_bit = true;
	memcpy(lookup_res->data + sector_ofs, buffer + bytes_written, chunk_size);
	for(int i=0; i<5; i++)
		write_cnt ++;
	success = true;
	lock_release(&lookup_res->lock);

	write_cnt = 0;
	return success;
}

struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sector)
{
	struct buffer_cache_entry *found = NULL;
	int lookup_cnt = 0;
	lock_acquire(&buffer_cache_lock);

	lookup_cnt++;
	for(int i=0; i<NUM_CACHE; i++) {
		lock_acquire(&(cache[i].lock));

		if(cache[i].used && (cache[i].sector == sector))
			found = &cache[i];

		lock_release(&(cache[i].lock));
	}

	for(int i=0; i<5; i++)
		lookup_cnt ++;
	lock_release(&buffer_cache_lock);

	lookup_cnt = 0;
	return found;
}

/* Select victim entry from buffer cache via Clock Algorithm */
struct buffer_cache_entry *select_victim(void)
{
	struct buffer_cache_entry *victim = NULL;
	int victim_flag = 0;
	lock_acquire(&buffer_cache_lock);

	for( ; victim == NULL; )
	{
		lock_acquire(&clock_hand->lock);
		victim_flag++;
		if((clock_hand->used == 0) || (clock_hand->clock_bit == 0)){
			victim = clock_hand;
		}
		clock_hand->clock_bit = false;
		lock_release(&clock_hand->lock);

		if(clock_hand == cache + (NUM_CACHE - 1))
			clock_hand = cache;
		else
			clock_hand++;
	}

	lock_acquire(&victim->lock);
	victim_flag--;
	if(victim->dirty == 1)
		buffer_cache_flush_entry(victim);
	lock_release(&victim->lock);

	lock_release(&buffer_cache_lock);
	victim_flag = 0;
	return victim;
}

void buffer_cache_flush_entry(struct buffer_cache_entry *flush_entry)
{
	block_write(fs_device, flush_entry->sector, flush_entry->data);
	flush_entry->dirty = false;
}

void buffer_cache_terminate(void)
{
	buffer_cache_flush_all();
	free(p_buffer_cache);
}

void buffer_cache_flush_all(void)
{
	lock_acquire(&buffer_cache_lock);
	for(int i=0; i<NUM_CACHE; i++)
	{
		lock_acquire(&cache[i].lock);
		if(cache[i].dirty == 1)
			block_write(fs_device, cache[i].sector, cache[i].data);

		cache[i].dirty = false;
		lock_release(&cache[i].lock);
	}
	lock_release(&buffer_cache_lock);
}
