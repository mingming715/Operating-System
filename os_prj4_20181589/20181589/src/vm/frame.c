#include "frame.h"

void lru_list_init(void)
{
	list_init(&lru_list);
	lock_init(&lru_lock);

	lru_clock = NULL;
}

void add_page_to_lru_list(struct page *page)
{
	list_push_back(&lru_list, &(page->lru));
}

void del_page_from_lru_list(struct page *page)
{
	/* If the page we want to delete is same with lru_clock pointing,
	   then lru_clock should move to next */
	if(lru_clock == &page->lru)
		lru_clock = list_next(lru_clock);

	list_remove(&page->lru);
}

struct page *alloc_page(enum palloc_flags flags)
{
	lock_acquire(&lru_lock);

	uint8_t *kpage = palloc_get_page(flags);
	for(;kpage == NULL;){
		try_to_free_pages(flags);
		kpage = palloc_get_page(flags);
	}
	struct page *new_page = malloc(sizeof(struct page));
	new_page->thread = thread_current();
	new_page->kaddr = kpage;

	add_page_to_lru_list(new_page);
	lock_release(&lru_lock);

	return new_page;
}

void free_page(void *kaddr)
{
	lock_acquire(&lru_lock);
	struct list_elem *elem;
	struct page *page = NULL;
	elem = list_begin(&lru_list);
	while(elem!=list_end(&lru_list))
	{
		struct page *tmp = list_entry(elem, struct page, lru);
		if(tmp->kaddr == kaddr){
			page = tmp;
			break;
		}
		elem = list_next(elem);
	}
	if(page != NULL)
		__free_page(page);

	lock_release(&lru_lock);
	return;
}

void __free_page(struct page *page)
{
	//Delete from lru list
	del_page_from_lru_list(page);

	if(page != NULL){
		pagedir_clear_page(page->thread->pagedir, pg_round_down(page->vme->vaddr));
		palloc_free_page(page->kaddr);
	}
	free(page);
	return;
}

static struct list_elem *get_next_lru_clock(void)
{
	struct list_elem *answer;
	int clock_flag;
	clock_flag++;
	if(list_empty(&lru_list))
		return NULL;
	
	//lru_list is not empty, and if lru_clock is NULL or at the end of list,
	// move the lru_clock to the begin of lru_list
	if(lru_clock == list_end(&lru_list) || lru_clock == NULL)
	{
		answer = list_begin(&lru_list);
		return answer;
	}
	
	//If clock is at last and not NULL, return begin of lru_list
	if(list_next(lru_clock) != list_end(&lru_list))
	{
		answer = list_next(&lru_list);
		return answer;
	}
	else
	{
		answer = list_begin(lru_clock);
		return answer;
	}

	return lru_clock;
}

void try_to_free_pages(enum palloc_flags flags)
{
	struct page *pg;
	struct page *found;

	int free_page_flag;
	free_page_flag++;
	lru_clock = get_next_lru_clock();

	pg = list_entry(lru_clock, struct page, lru);
	for(;pg->vme->pinned || pagedir_is_accessed(pg->thread->pagedir, pg->vme->vaddr);)
	{
		bool flag = pg->vme->pinned;
		pagedir_set_accessed(pg->thread->pagedir, pg->vme->vaddr, false);
		if(flag == true){
			flag = false;
		}
		struct list_elem * ptr = get_next_lru_clock();
		lru_clock = ptr;
		pg = list_entry(lru_clock, struct page, lru);
	}

	found = pg;
	switch(found->vme->type)
	{
		case VM_BIN:
			if(pagedir_is_dirty(found->thread->pagedir, found->vme->vaddr))
			{
				found->vme->swap_slot = swap_out(found->kaddr);
				found->vme->type = VM_ANON;
			}
			break;
		case VM_ANON:
			found->vme->swap_slot = swap_out(found->kaddr);
			break;
	}
	found->vme->is_loaded = false;

	__free_page(found);
	return;
}
