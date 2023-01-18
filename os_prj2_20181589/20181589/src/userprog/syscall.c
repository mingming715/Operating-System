#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include "devices/input.h"
#include <stdlib.h>
#include <string.h>
#define WORD 4

void check_user_vaddr(const void *vaddr);
static void syscall_handler (struct intr_frame *);
struct lock thread_lock;

void check_user_vaddr(const void *vaddr){
	if(!is_user_vaddr(vaddr)){
		exit(-1);
	}
}

	void
syscall_init (void) 
{
	lock_init(&thread_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

	static void
syscall_handler (struct intr_frame *f) 
{
	/*User Stack
	  |--------------|
	  |   Arguments  |
	  |--------------|
	  |  Syscall No. |
	  |--------------|  <<----esp
	  |              |
	  |              |
	  |______________|

											*/

//	printf("syscall : %d\n", *(uint32_t *)(f->esp));
	int *syscall_no = f->esp;
	switch (syscall_no[0]) {	//System call Number
		case SYS_HALT:	//0개
			halt();
			break;
		case SYS_EXIT:	//1개
			check_user_vaddr(&syscall_no[1]);
			exit(*(uint32_t *)(f->esp+4));
			break;
		case SYS_EXEC:	//1개
			check_user_vaddr(&syscall_no[1]);
			f->eax = exec((const char *)*(uint32_t *)(f->esp+4));
			break;
		case SYS_WAIT:	//1개
			check_user_vaddr(&syscall_no[1]);
			f->eax = wait((pid_t)*(uint32_t *)(f->esp+4));
			break;
		case SYS_CREATE:	//2개
			check_user_vaddr(&syscall_no[1]);
			check_user_vaddr(&syscall_no[2]);
			f->eax = create((const char *)*(uint32_t *)(f->esp+4), (unsigned)*(uint32_t *)(f->esp+8));
			break;
		case SYS_REMOVE:	//1개
			check_user_vaddr(&syscall_no[1]);
			f->eax = remove((const char *)*(uint32_t *)(f->esp+4));
			break;
		case SYS_OPEN:	//1개	
			check_user_vaddr(&syscall_no[1]);
			f->eax = open((const char *)*(uint32_t *)(f->esp+4));
			break;
		case SYS_FILESIZE:	//1개
			check_user_vaddr(&syscall_no[1]);
			f->eax = filesize((int)*(uint32_t *)(f->esp+4));
			break;
		case SYS_READ:	//3개
			check_user_vaddr(&syscall_no[1]);
			check_user_vaddr(&syscall_no[2]);
			check_user_vaddr(&syscall_no[3]);
			f->eax = (uint32_t)read(syscall_no[1], (void*)syscall_no[2], (unsigned)syscall_no[3]);
			break;
		case SYS_WRITE:	//3개
			check_user_vaddr(&syscall_no[1]);
			check_user_vaddr(&syscall_no[2]);
			check_user_vaddr(&syscall_no[3]);
			f->eax = (uint32_t)write(syscall_no[1], (void*)syscall_no[2], (unsigned)syscall_no[3]);
			break;
		case SYS_SEEK:	//2개
			check_user_vaddr(&syscall_no[1]);
			check_user_vaddr(&syscall_no[2]);
			seek((int)*(uint32_t *)(f->esp+4), (unsigned)*(uint32_t *)(f->esp+8));
			break;
		case SYS_TELL:	//1개
			check_user_vaddr(&syscall_no[1]);
			f->eax = tell((int)*(uint32_t *)(f->esp+4));
			break;
		case SYS_CLOSE:	//1개
			check_user_vaddr(&syscall_no[1]);
			close((int)*(uint32_t *)(f->esp+4));
			break;
		case SYS_FIB:
			check_user_vaddr(&syscall_no[1]);
			f->eax = (uint32_t)fibonacci(syscall_no[1]);
			break;
		case SYS_MAX:
			check_user_vaddr(&syscall_no[1]);
			check_user_vaddr(&syscall_no[2]);
			check_user_vaddr(&syscall_no[3]);
			check_user_vaddr(&syscall_no[4]);
			f->eax = (uint32_t)max_of_four_int(syscall_no[1], syscall_no[2], syscall_no[3], syscall_no[4]);
			break;
	}
}

void halt(void){
	shutdown_power_off();
}

void exit(int status){
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_current()->exit_status = status;
	thread_exit();
	for(int i=2; i<128; i++){
		if(thread_current()->fd[i] != NULL) close(i);
	}
}

int write(int fd, const void *buffer, unsigned length){
	int final=-1;
	lock_acquire(&thread_lock);
	if(fd==1){
		putbuf(buffer,length);
		final = length;
	}
	else if(fd>=2){
		if(thread_current()->fd[fd]==NULL){
			lock_release(&thread_lock);
			exit(-1);
		}
		if(thread_current()->fd[fd]->deny_write)
			file_deny_write(thread_current()->fd[fd]);
		final = file_write(thread_current()->fd[fd], buffer, length);
	}
	lock_release(&thread_lock);
	return final;
}
pid_t exec (const char *file){
	if(file == NULL) return -1;
	return (pid_t)process_execute(file);
}


int wait (pid_t pid){
	return process_wait(pid);
}

int fibonacci(int n){
	int a=0, b=1, c, i;
	if(n==0)
		return a;
	for(i=2; i<=n; i++){
		c=a+b;
		a=b;
		b=c;
	}
	return b;
}

int max_of_four_int(int a, int b, int c, int d){
	int max=a;
	if(b>max) max=b;
	if(c>max) max=c;
	if(d>max) max=d;
	return max;
}

int read (int fd, void *buffer, unsigned length){
	int i;
	int final=-1;
	char *tmpbuf;
	tmpbuf = (char *)buffer;

	if(!is_user_vaddr(buffer)) exit(-1);

	lock_acquire(&thread_lock);


	if (fd==0){
		for(i=0; i<length; i++){
			if((tmpbuf[i]=(char)input_getc()) == '\0')
				break;
		}
		final=i;
	}
	else if (fd>=2){
		if(thread_current()->fd[fd]==NULL){
			lock_release(&thread_lock);
			exit(-1);
		}
		final = file_read(thread_current()->fd[fd], buffer, length);
	}

//	if(lock_held_by_current_thread(&thread_lock)) printf("Held");

	lock_release(&thread_lock);
	return final;
}
bool create (const char *file, unsigned initial_size){
	if(file==NULL) exit(-1);

	return filesys_create(file, initial_size);
}
bool remove (const char *file){
	if(file==NULL) exit(-1);

	return filesys_remove(file);
}
int open (const char *file){
	int final = -1;
	if(file==NULL) exit(-1);

	lock_acquire(&thread_lock);
	struct file * f = filesys_open(file);
	if (f==NULL){
		final=-1;
	}
	else{
		for(int i=2; i<128; i++){
			if(thread_current()->fd[i] == NULL){
				if(strcmp(thread_current()->name, file)==0)
					file_deny_write(f);
				thread_current()->fd[i] = f;
				final = i;
				break;
			}
		}
	}

//	if(lock_held_by_current_thread(&thread_lock)) printf("Held");
	lock_release(&thread_lock);
	return final;
}
int filesize (int fd){
	if(thread_current()->fd[fd] == NULL) exit(-1);
	return file_length(thread_current()->fd[fd]); 
}

void seek (int fd, unsigned position){
	if(thread_current()->fd[fd]==NULL) exit(-1);
	return file_seek(thread_current()->fd[fd], position);
}
unsigned tell (int fd){
	if(thread_current()->fd[fd]==NULL) exit(-1);
	return file_tell(thread_current()->fd[fd]);
}
void close (int fd){
	if(thread_current()->fd[fd]==NULL) exit(-1);
	struct file * tmp;
	tmp = thread_current()->fd[fd];
	thread_current()->fd[fd]=NULL;
	return file_close(tmp);
}
