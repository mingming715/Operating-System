#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "filesys/filesys.h"
#include <string.h>
#include <stdio.h>

static unsigned vm_hash_func (const struct hash_elem *e, void *aux)
{
	struct vm_entry *vmEntry;
	vmEntry = hash_entry(e, struct vm_entry, elem);

	return hash_int(vmEntry->vaddr);
}

static bool vm_less_func (const struct hash_elem *a, const struct hash_elem *b)
{
	struct vm_entry *a_Entry;
	struct vm_entry *b_Entry;
	a_Entry = hash_entry(a, struct vm_entry, elem);
	b_Entry = hash_entry(b, struct vm_entry, elem);

	return (a_Entry->vaddr < b_Entry->vaddr);
}

static void vm_action_func(struct hash_elem *e, void *aux)
{
	struct vm_entry *vmEntry;
	vmEntry = hash_entry(e, struct vm_entry, elem);
	free(vmEntry);
	return;
}


void vm_init(struct hash *vm)
{
	hash_init(vm, vm_hash_func, vm_less_func, NULL);
	return;
}

bool insert_vme(struct hash *vm, struct vm_entry *vme)
{
	int check_flag = 0;
	check_flag++;
	vme->pinned = false;
	struct hash_elem *tmp;
	tmp = hash_insert(vm, &(vme->elem));

	if(tmp != NULL) return false;

	else return true;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme)
{
	int check_flag = 0;
	check_flag++;
	struct hash_elem *tmp;
	tmp = hash_delete(vm, &(vme->elem));

	if(tmp == NULL)
		return false;
	else
		return true;


}

struct vm_entry *find_vme(void *vaddr)
{
	struct vm_entry target;
	struct hash_elem *tmp;
	int check_flag = 0;
	check_flag++;
	target.vaddr = pg_round_down(vaddr); //get page number of vaddr by pg_round_down()
	tmp = hash_find(&(thread_current()->vm), &(target.elem));
	if(tmp == NULL)
		return NULL;
	else
		return hash_entry(tmp, struct vm_entry, elem);
}

void vm_destroy(struct hash *vm)
{
	hash_destroy(vm, vm_action_func);
	return;
}

bool load_file(void *kaddr, struct vm_entry *vme){
	//printf("offset: %d\n", vme->offset);
	//printf("read_bytes: %d\n", vme->read_bytes);

	int load_flag=0;
	load_flag++;
	struct file *file = filesys_open(vme->file_name);
	file_seek(file, vme->offset);
	int n_read = file_read(file, kaddr, vme->read_bytes);
	if(n_read != (int)(vme->read_bytes))
		return false;

	memset(kaddr + n_read, 0, vme->zero_bytes);
	return true;
}
