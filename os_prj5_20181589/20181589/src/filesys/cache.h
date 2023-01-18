#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

struct buffer_cache_entry {
	bool dirty;
	bool used;
	block_sector_t sector;
	bool clock_bit;
	struct lock lock;
	void *data;
};


void buffer_cache_init(void);
struct buffer_cache_entry *buffer_cache_lookup(block_sector_t sector);
bool buffer_cache_read(block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs);
struct buffer_cache_entry *select_victim(void);
void buffer_cache_flush_entry(struct buffer_cache_entry *b_flush_entry);
bool buffer_cache_write(block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs);
void buffer_cache_terminate(void);
void buffer_cache_flush_all(void);
