#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

#define PATH_MAX_LEN 256
/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
	void
filesys_init (bool format) 
{
	fs_device = block_get_role (BLOCK_FILESYS);
	if (fs_device == NULL)
		PANIC ("No file system device found, can't initialize file system.");

	buffer_cache_init();

	inode_init ();
	free_map_init ();

	if (format) 
		do_format ();

	thread_current()->cur_dir = dir_open_root();
	free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
	void
filesys_done (void) 
{

	buffer_cache_terminate();
	free_map_close ();

}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
	bool
filesys_create (const char *name, off_t initial_size) 
{
	block_sector_t inode_sector = 0;
	char *parsed_name = (char *)malloc(sizeof(char) * (PATH_MAX_LEN + 1));
	struct dir *dir = path_parse(name, parsed_name);
	//struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, false)
			&& dir_add (dir, parsed_name, inode_sector));
	if (!success && inode_sector != 0) 
		free_map_release (inode_sector, 1);
	dir_close (dir);
	free(parsed_name);

	return success;
}

struct dir *path_parse(char *name, char *file_name)
{
	int if_parsed = 0;
	struct dir *dir = NULL;
	if(!name || !file_name || strlen(name) == 0)
		return NULL;

	char *path = (char *)malloc(sizeof(char) * (PATH_MAX_LEN + 1));
	strlcpy(path, name, PATH_MAX_LEN);

	if_parsed = 1;
	if(path[0] == '/')
	{
		if(if_parsed == 1)
			if_parsed = 0;
		dir = dir_open_root();
	}
	else
	{
		if(if_parsed == 0)
			if_parsed = 1;
		dir = dir_reopen(thread_current()->cur_dir);
	}

	if_parsed = 0;
	char *ptr_cur, *ptr_next, *ptr_tmp;
	ptr_cur = strtok_r(path, "/", &ptr_tmp);
	ptr_next = strtok_r(NULL, "/", &ptr_tmp);

	if(dir == NULL)
		return NULL;

	if(!inode_is_dir(dir_get_inode(dir)))
		return NULL;

	while(ptr_cur != NULL && ptr_next != NULL)
	{
		if_parsed++;
		struct inode *inode = NULL;
        if (!dir_lookup(dir, ptr_cur, &inode) || !inode_is_dir(inode))
        {
			if_parsed--;
            dir_close(dir);
            return NULL;
        }
        dir_close(dir);
        dir = dir_open(inode);

		ptr_cur = ptr_next;
		ptr_next = strtok_r(NULL, "/", &ptr_tmp);	
	}

	if(ptr_cur == NULL)
		strlcpy(file_name, ".", PATH_MAX_LEN);
	else
		strlcpy(file_name, ptr_cur, PATH_MAX_LEN);

	free(path);
	if_parsed = 0;
	return dir;
}
/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
	struct file *
filesys_open (const char *name)
{
	//  printf("name: %s\n", name);
	char *parsed_name = (char *)malloc(sizeof(char) * (PATH_MAX_LEN+1));
	struct dir *dir = path_parse(name, parsed_name);
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, parsed_name, &inode);
	dir_close (dir);
	free(parsed_name);
	return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
	bool
filesys_remove (const char *name) 
{
	char *parsed_name = (char *)malloc(sizeof(char)*(PATH_MAX_LEN+1));
	char *tmp = (char *)malloc(sizeof(char)*(PATH_MAX_LEN+1));
	struct dir *dir = path_parse(name, parsed_name);
	bool success = false;
	struct inode *inode;

	dir_lookup(dir, parsed_name, &inode);

	if(inode_is_dir(inode))
	{
		struct dir *new = NULL;
		new = dir_open(inode);
		if(new)
		{
			if(!dir_readdir(new, tmp))
			{
				success = dir != NULL && dir_remove (dir, parsed_name);
			}
			dir_close(new);
		}
	}
	else
	{
		success = dir != NULL && dir_remove (dir, parsed_name);
	}
	dir_close (dir);
	free(parsed_name);
	free(tmp);

	return success;
}

bool change_dir(char *path)
{
	int dir_changed = 0;
	char *tmp = (char *)malloc(sizeof(char) * (PATH_MAX_LEN+1));
	strlcpy(tmp, path, PATH_MAX_LEN);
	strlcat(tmp, "/0", PATH_MAX_LEN);
	dir_changed = 1;
	char *parsed_name = (char *)malloc(sizeof(char) * (PATH_MAX_LEN+1));
	struct dir *dir = path_parse(tmp, parsed_name);
	for(int i=0; i<5; i++)
		dir_changed++;

	if(dir == NULL)
	{
		dir_changed = 0;
		free(tmp);
		free(parsed_name);
		return false;
	}

	dir_changed=0;
	free(tmp);
	free(parsed_name);
	dir_close(thread_current()->cur_dir);
	thread_current()->cur_dir=dir;
	return true;
}

bool create_dir(char *name)
{
	int dir_created = 0;
	block_sector_t inode_sector = 0;
	char *parsed_name = (char *)malloc(sizeof(char) * (PATH_MAX_LEN+1));
	struct dir *dir = path_parse(name, parsed_name);
	dir_created = 1;
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& dir_create (inode_sector, 16)
			&& dir_add (dir, parsed_name, inode_sector));
	for(int i=0; i<5; i++)
		dir_created ++;
	if(success)
	{
		struct dir *tmp_dir = dir_open(inode_open(inode_sector));
		dir_add(tmp_dir, ".", inode_sector);
		dir_add(tmp_dir, "..", inode_get_inumber(dir_get_inode(dir)));
		dir_created++;
		dir_close(tmp_dir);
		dir_created--;
		free(parsed_name);
		dir_close(dir);
		dir_created = 1;
		return true;
	}
	else
	{
		dir_created--;
		if(inode_sector)
		{
			free_map_release(inode_sector, 1);
		}
		free(parsed_name);
		dir_created = 0;
		dir_close(dir);
		return false;
	}
	dir_created = 0;
	return false;
}


/* Formats the file system. */
	static void
do_format (void)
{
	int format_done = 0;
	printf ("Formatting file system...");
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");

	struct dir *tmp_dir = dir_open(inode_open(ROOT_DIR_SECTOR));
	dir_add(tmp_dir, ".", ROOT_DIR_SECTOR);
	dir_add(tmp_dir, "..", ROOT_DIR_SECTOR);
	dir_close(tmp_dir);

	format_done = 1;
	free_map_close ();
	printf ("done.\n");
}
