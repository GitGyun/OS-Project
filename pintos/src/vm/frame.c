#include "vm/frame.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"
#include <stdio.h>

static struct hash frame_table;
static struct list victim_selector;

/* Lock used for paging system */
struct semaphore paging_lock;

static bool frame_table_insert (struct fte *);
static void frame_table_del_frame (struct fte *);

static unsigned fte_hash (const struct hash_elem *, void *);
static bool fte_cmp_kpage (const struct hash_elem *,
                           const struct hash_elem *, void *);
static struct fte *select_victim (void);

static bool debug = false;

/* Initialize the frame table. Called by init.c */
void
frame_table_init (void)
{
  hash_init (&frame_table, fte_hash, fte_cmp_kpage, NULL);
  list_init (&victim_selector);
  sema_init (&paging_lock, 1);
}

/* Insert fte to the frame table */
static bool
frame_table_insert (struct fte *f)
{
  sema_down (&paging_lock);
  bool success = (hash_insert (&frame_table, &f->elem) == NULL);
  list_push_back (&victim_selector, &f->lelem);
  sema_up (&paging_lock);

  return success;
}

/* Find fte from address. If fte does not exist, return NULL. */
struct fte *
frame_table_find (void *kpage)
{
  struct fte f;
  f.kpage = kpage;

  sema_down (&paging_lock);
  struct hash_elem *e = hash_find (&frame_table, &f.elem);
  sema_up (&paging_lock);

  if (e != NULL)
    {
      struct fte *f_found = hash_entry (e, struct fte, elem);
      return f_found;
    }
  return NULL;
}

/* Delete fte from the frame table */
static void
frame_table_del_frame (struct fte *f)
{
  sema_down (&paging_lock);
  hash_delete (&frame_table, &f->elem);
  list_remove (&f->lelem);
  sema_up (&paging_lock);
}

/* Allocate a new frame */
void *
frame_alloc (void *upage, enum palloc_flags flags, bool writable)
{
  ASSERT (upage != NULL);

  struct thread *t = thread_current ();
  struct fte *victim;
  /* First attempt to allocate new frame. */
  void *kpage = palloc_get_page (flags);
  if (kpage  == NULL)
    {
      /* Physical memory insufficient. Eviction is required. */
      /* Select victim which is to be evicted. */
      //struct fte *victim = select_victim ();
      victim = select_victim ();

      /* Evict victim to swap disk */
      swap_out (victim);

      /* Retry frame allocation once more */
      kpage = palloc_get_page (flags && PAL_ASSERT);
    }

  /* If retry is also failed, eviction performed incorrectly. */
  if (kpage == NULL)
    {
      PANIC ("Eviction failed");
    }

  /* upage - kpage mapping on pagedir */
  bool success = (pagedir_get_page (t->pagedir, upage) == NULL)
                 && pagedir_set_page (t->pagedir, upage, kpage, writable);
  if (!success)
    goto update_error;

  /* Allocate new frame table entry (fte) */
  struct fte *fte_new = malloc (sizeof (struct fte));
  ASSERT (fte_new != NULL);

  /* Initialize fte */
  fte_new->kpage = kpage;
  fte_new->upage = upage;
  fte_new->process = t;

  /* Insert fte to the frame table */
  success = frame_table_insert (fte_new);
  if (!success)
    {
      free (fte_new);
      goto update_error;
    }

  /* Update supplemental page table. */
  struct spte *p = suppl_page_table_find (t->suppl_page_table, upage);
  if (p == NULL)
    {
      /* Current upage is not in supplemental page table. I.e., frame
         allocation for this UPAGE is first time. (If this UPAGE have
         been evicted, then frame is reallocated to swap in.)
         Allocate new spte. */
      p = spte_create (upage, kpage);
      p->writable = writable;

      success = suppl_page_table_insert (t->suppl_page_table, p);

      if (!success)
        {
          free (fte_new);
          free (p);
          goto update_error;
        }
    }
  else
    {
      if (debug)
        printf ("(%2d) frame alloc: spte allready exist; just update kpage.\n", thread_tid ());

      /* This page is swapped in. Just update kpage only. */
      p->kpage = kpage;
    }
  p->stat = PG_ON_MEMORY;

  if (debug)
    printf ("(%2d) frame alloc: upage %p, kpage %p, kpage found from pagedir %p, stat %s\n",
            thread_tid (), p->upage, p->kpage, pagedir_get_page (t->pagedir, upage),
            p->stat == PG_EVICTED? "PG_EVICTED" : "PG_ON_MEMORY");


  return kpage;

 update_error:
  PANIC ("Error occured while updating"
         "page directory, frame table, or supplimental page table.");
}

/* Free KPAGE */
void
frame_free (void *kpage)
{
  if (debug)
    printf ("(%2d) frame free: kpage %p\n", thread_tid (), kpage);

  struct fte *f = frame_table_find (kpage);
  struct hash *spt = thread_current ()->suppl_page_table;
  struct spte *p = suppl_page_table_find (spt, f->upage);

  struct thread *t = f->process;
  void *upage = f->upage;

  /* Discard fte from the frame table */
  if (f != NULL)
    {
      frame_table_del_frame (f);
      free (f);
    }
  else
    printf("freed frame that was already freed: %p\n", kpage);

  /* Delete kpage in spte */
  if (p != NULL)
    p->kpage = NULL;

  /* Discard upage - kpage mapping from pagedir */
  pagedir_clear_page (t->pagedir, upage);

  /* Free KPAGE */
  palloc_free_page (kpage);
}



/* ===== Helpers ===== */

/* Hash function for fte. */
static unsigned
fte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct fte *f = hash_entry (e, struct fte, elem);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

/* Compare fte's addresses. */
static bool
fte_cmp_kpage (const struct hash_elem *a_,
               const struct hash_elem *b_, void *aux UNUSED)
{
  const struct fte *a = hash_entry (a_, struct fte, elem);
  const struct fte *b = hash_entry (b_, struct fte, elem);

  return a->kpage < b->kpage;
}

/* Select fte to be evicted */
static struct fte *
select_victim (void)
{
  return list_entry (list_front (&victim_selector), struct fte, lelem);
}
