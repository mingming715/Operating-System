#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"

#include "userprog/syscall.h"
#include <stdlib.h>
static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
	tid_t
process_execute (const char *file_name) 
{
	char *fn_copy;
	tid_t tid;
	struct list_elem* elem;
	struct thread *t;
	/* Make a copy of FILE_NAME.
	   Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL){
		palloc_free_page(fn_copy);
		return -1;
	}
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Extract the name of the command */
	int i;
	char name_command[256];
	strlcpy(name_command, file_name, strlen(file_name)+1);
	for(i=0; name_command[i]!='\0' && name_command[i] != ' '; i++) continue;
	name_command[i] = '\0';

	struct file* ret;
	ret = filesys_open(name_command);
	
//	printf("name command: %s\n", name_command);
	if(strcmp(name_command, "child-syn-read") != 0 && strcmp(name_command, "child-syn-wrt") != 0 && strcmp(name_command, "child-linear") != 0){
		if(ret == NULL){
			return -1;
		}
	}
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (name_command, PRI_DEFAULT, start_process, fn_copy);
	if (tid == TID_ERROR){
		palloc_free_page (name_command);
		return -1;
	}

	struct thread * child = NULL;

	for (elem = list_begin(&thread_current()->child); elem != list_end(&thread_current()->child); elem = list_next(elem)) {
		t = list_entry(elem, struct thread, childelem);
		if(t->tid == tid){
			child = t;
			break;
		}
	}
	sema_down(&child->lock_load);

/*
   
	if(child->exit_status == -1)
		return process_wait(tid);
*/
	return tid;
}

/* A thread function that loads a user process and starts it
   running. */
	static void
start_process (void *file_name_)
{
	char *file_name = file_name_;
	struct intr_frame if_;
	bool success;
	char *command = (char *)malloc(sizeof(char)*PGSIZE);
	char *tmp;

	strlcpy(command, file_name, PGSIZE);

	vm_init(&thread_current()->vm);
	/* Initialize interrupt frame and load executable. */
	memset (&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;
	success = load (file_name, &if_.eip, &if_.esp);


	/* If load failed, quit. */
	palloc_free_page (file_name);
	if (!success){
		sema_up(&thread_current()->lock_load);
		thread_exit();
	}
	sema_up(&thread_current()->lock_load);
	
	/* Start the user process by simulating a return from an
	   interrupt, implemented by intr_exit (in
	   threads/intr-stubs.S).  Because intr_exit takes all of its
	   arguments on the stack in the form of a `struct intr_frame',
	   we just point the stack pointer (%esp) to our stack frame
	   and jump to it. */

	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
	NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
	int
process_wait (tid_t child_tid UNUSED) 
{
	struct list_elem* elem;
	struct thread* curr = thread_current();
	int exit_status;
	struct thread* temp = NULL;
	struct thread* found = NULL;

	for(elem = list_begin(&(curr->child)); elem != list_end(&(curr->child)); elem = list_next(elem)){
		temp = list_entry(elem, struct thread, childelem);
		if(child_tid == temp->tid){
			found = temp;
			break;
		}
	}
//	printf("name: %s\n", found->name);	
	/* Get exit status from child thread when the child thread is dead */
	if(found->status == THREAD_DYING){
		return -1;
	}
	/* Check that child thread ID is valid */
	if(found == NULL){
		return -1;
	}
	else{
		sema_down(&(found->lock_child));
		exit_status = found->exit_status;
		list_remove(&(found->childelem));
		sema_up(&(found->lock_memory));
		return exit_status;
	}
	return -1;
}

void process_exit(void)
{
	struct thread *cur = thread_current();
	uint32_t *pd;

	for(int i=2; i<128; i++){
		if(cur->fd[i] != NULL){
			close(i);
		}
	}

	vm_destroy(&(thread_current()->vm));
	/*Destroy the current process's page directory and switch back
	  to the kernel-only page directory */
	pd = cur->pagedir;
	if(pd!= NULL)
	{
		cur->pagedir = NULL;
		pagedir_activate(NULL);
		pagedir_destroy(pd);
	}


	sema_up(&(cur->lock_child));
	sema_down(&(cur->lock_memory));

}
/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
	void
process_activate (void)
{
	struct thread *t = thread_current ();

	/* Activate thread's page tables. */
	pagedir_activate (t->pagedir);

	/* Set thread's kernel stack for use in processing
	   interrupts. */
	tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
	unsigned char e_ident[16];
	Elf32_Half    e_type;
	Elf32_Half    e_machine;
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;
	Elf32_Off     e_phoff;
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;
	Elf32_Half    e_shentsize;
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
	Elf32_Word p_type;
	Elf32_Off  p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (char *file_name, struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
	bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
	struct thread *t = thread_current ();
	struct Elf32_Ehdr ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pagedir = pagedir_create ();
	if (t->pagedir == NULL) 
		goto done;
	process_activate ();

	/* Parsing start */
   char ** argv;
	int argc = 0;
	char temp[128];
	char *word;
	char *saveptr;
	int is_blank=0;

	for(i=0; i<(int)strlen(file_name); i++){
		if(file_name[i]==' ' && file_name[i+1]!=' ' && file_name[i+1]!= '\0' && file_name[i]!='\0' && is_blank==0){
			argc+=1;
			is_blank = 1;
		}
		else {
			is_blank = 0;
		}
	}
	argc++;
	argv = (char **)malloc(sizeof(char *)*argc);

	strlcpy(temp, file_name, strlen(file_name)+1);
	word = strtok_r(temp, " ", &saveptr);
	for(i=0; i<=argc; i++){
		argv[i] = word;
		word = strtok_r(NULL, " ", &saveptr);
	}
	argv[argc]=NULL;

	strlcpy(t->name, argv[0], strlen(t->name)+1);


	/* Open executable file. */
	file = filesys_open (t->name);
	if (file == NULL) 
	{
		printf ("load: %s: open failed\n", file_name);
		goto done; 
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 3
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
			|| ehdr.e_phnum > 1024) 
	{
		printf ("load: %s: error loading executable\n", file_name);
		goto done; 
	}
	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) 
	{
		struct Elf32_Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) 
		{
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) 
				{
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint32_t file_page = phdr.p_offset & ~PGMASK;
					uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint32_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0)
					{
						/* Normal segment.
						   Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					}
					else 
					{
						/* Entirely zero.
						   Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (t->name, file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (esp))
		goto done;

	/*Stack make */
	int align = 0;
	for(i=argc-1;i>=0;i--){
		align += strlen(argv[i])+1;
		align = align % 4;
		(*esp) -= strlen(argv[i])+1;
		memcpy(*esp, argv[i], strlen(argv[i])+1);
		argv[i] = (char*)(*esp);
	}

	for(i=(4-align)%4;i;i--){
		(*esp)--;
		**(uint8_t**)esp = 0;
	}

	(*esp) -= sizeof(char *);
	for(i=argc-1;i>=0;i--){
		(*esp) -= sizeof(void *);
		memcpy(*esp, argv+i, sizeof(void*));
	}

	argv[argc]=*esp;
	(*esp) -= sizeof(void*);
	memcpy(*esp, argv+argc, sizeof(void*));

	(*esp) -= sizeof(int);
	memcpy(*esp, &argc, sizeof(int));

	(*esp) -= sizeof(void*);
	**(uint32_t **)esp = 0;

//	hex_dump(*esp,*esp,100,1);
	/* Start address. */
	*eip = (void (*) (void)) ehdr.e_entry;
	
	free(argv);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
	static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
		return false; 

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (Elf32_Off) file_length (file)) 
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz) 
		return false; 

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}
/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

   - READ_BYTES bytes at UPAGE must be read from FILE
   starting at offset OFS.

   - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
	static bool
load_segment (char *file_name, struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) 
	{
		/* Calculate how to fill this page.
		   We will read PAGE_READ_BYTES bytes from FILE
		   and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
	/*	
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;
			*/
		/* Load this page. */
/* 		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
		{
			palloc_free_page (kpage);
			return false; 
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);
		*/
		/* Add the page to the process's address space. */
		/*
		if (!install_page (upage, kpage, writable)) 
		{
			palloc_free_page (kpage);
			return false; 
		}
		*/
		/* Initialize vm entry */
		struct vm_entry *vmEntry = malloc(sizeof(struct vm_entry));
		vmEntry->type = VM_BIN;
		vmEntry->vaddr = upage;
		vmEntry->writable = writable;
		vmEntry->is_loaded = false;
	//	vmEntry->file = file;
		vmEntry->file_name = (char *)malloc(sizeof(char)*strlen(file_name)+1);
		strlcpy(vmEntry->file_name, file_name, strlen(file_name)+1);
		vmEntry->offset = ofs;
		vmEntry->read_bytes = page_read_bytes;
		vmEntry->zero_bytes = page_zero_bytes;

		/* Include in hash table */
		insert_vme(&(thread_current()->vm), vmEntry);

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		ofs += page_read_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
	static bool
setup_stack (void **esp) 
{
	//uint8_t *kpage;
	struct page *kpage;
	bool success = false;

	kpage = alloc_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) 
	{
		success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage->kaddr, true);
		if (success){
			*esp = PHYS_BASE;
			/* Initialize vm entry */
			struct vm_entry *vmEntry = malloc(sizeof(struct vm_entry));
			vmEntry->is_loaded = true;
			vmEntry->writable = true;
			vmEntry->type = VM_ANON;
			vmEntry->vaddr = ((uint8_t *) PHYS_BASE) - PGSIZE;

			kpage->vme = vmEntry;
			/* Include in has table */
			insert_vme(&(thread_current()->vm), vmEntry);
		}
		else {
			free_page (kpage->kaddr);
			return false;
		}
	}
	return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
	static bool
install_page (void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	   address, then map our page there. */
	return (pagedir_get_page (t->pagedir, upage) == NULL
			&& pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool expand_stack(void *addr)
{
	struct page *kpage = alloc_page(PAL_USER | PAL_ZERO);
	struct vm_entry *vmEntry = malloc(sizeof(struct vm_entry));
	if(vmEntry == NULL)
		return false;

	vmEntry->type = VM_ANON;
	vmEntry->vaddr = pg_round_down(addr);
	vmEntry->writable = true;
	vmEntry->is_loaded = true;
	insert_vme(&thread_current()->vm, vmEntry);
	kpage->vme = vmEntry;

	if(!install_page(vmEntry->vaddr, kpage->kaddr, vmEntry->writable))
	{
		free_page(kpage->kaddr);
		free(vmEntry);
		return false;
	}
	return true;
}

bool verify_stack(void *fault_addr, void *esp)
{
	void *grow_limit = PHYS_BASE - 8*1024*1024;
	bool answer = is_user_vaddr(pg_round_down(fault_addr)) && (fault_addr >= esp-32) && (fault_addr >= grow_limit);
	return answer;
}

bool handle_mm_fault(struct vm_entry *vme)
{
	//void *kaddr;
	//uint8_t *kpage = palloc_get_page (PAL_USER);
	struct page *kpage = alloc_page(PAL_USER);
	kpage->vme = vme;
	switch(vme->type)
	{
		case VM_BIN:
	//		printf("vme->type\n");
			if(!load_file(kpage->kaddr, vme)){
			//	printf("load fail\n");
				free_page(kpage->kaddr);
				return false;
			}
			break;
		case VM_ANON:
			swap_in(vme->swap_slot, kpage->kaddr);
			break;
	}
	if(!install_page(vme->vaddr, kpage->kaddr, vme->writable)){
		free_page(kpage->kaddr);
		return false;
	}

	vme->is_loaded = true;
	return true;
}