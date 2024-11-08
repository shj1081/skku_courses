// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "fs.h"
#include "proc.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;


struct page pages[PHYSTOP/PGSIZE]; // Array of page structures
struct page *page_lru_head; // Head of the LRU list
int num_free_pages = PHYSTOP/PGSIZE; // Number of free pages
int num_lru_pages; // Number of pages in the LRU list

struct spinlock page_lock; // Lock for the LRU list
int freeNum_in_bitmap = (SWAPMAX / (PGSIZE / BSIZE)); // Number of free blocks in the bitmap

// Swap space bitmap structure
struct {
  struct spinlock lock;
  char *bitmap;
} swap_space_bitmap;


// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  num_free_pages = 0;
  for (int i = 0; i < PHYSTOP/PGSIZE; i++) {
    pages[i].pgdir = 0;
    pages[i].vaddr = 0;
    pages[i].next = 0;
    pages[i].prev = 0;
  }
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;

  // Initialize the swap space bitmap into 0
  initlock(&swap_space_bitmap.lock, "swap_space_bitmap");
  swap_space_bitmap.bitmap = kalloc();
  memset(swap_space_bitmap.bitmap, 0, PGSIZE);

  // Initialize the LRU list
  initlock(&page_lock, "page_lock");
  num_lru_pages = 0;
  page_lru_head = 0;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
try_again:
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(!r) {
    // Try to swap out a page
    if(swap_out())
      goto try_again;
    
    // If swap_out() fails, OOM
    else {
      cprintf("Out of memory\n");
      return 0;
    }
  }
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);

  num_free_pages--;
  return (char*)r;
}

// Function to get a free block number from the bitmap
int get_blkno() {

  // cprintf("freeNum_in_bitmap: %d\n", freeNum_in_bitmap);
  if (freeNum_in_bitmap == 0) {
    // No more swap space available
    return -1;
  }
  acquire(&swap_space_bitmap.lock);

  // Scan the bitmap byte-by-byte
  for (int idx = 0; idx < MAX_BITMAP_IDX; idx++) {
    if (swap_space_bitmap.bitmap[idx] != (char)0xFF) {  // Found a byte with at least one free bit
      char byte = swap_space_bitmap.bitmap[idx];
      for (uint shift = 0; shift < 8; shift++) {
        if (!(byte & (0x80 >> shift))) {  // Found a free bit (0b10000000 >> shift -> mask for the bit)
          int blkno = idx * 8 + shift;
          release(&swap_space_bitmap.lock);
          return blkno;  // Return the free block number
        }
      }
    }
  }

  release(&swap_space_bitmap.lock);
  return -1;  // Should not reach here if free_bitmap > 0
}


// Function to mark a block as used in the bitmap
void set_bitmap(int blkno) {
  acquire(&swap_space_bitmap.lock);
  freeNum_in_bitmap--; // Decrement the free block counter
  swap_space_bitmap.bitmap[blkno / 8] |= (0x80 >> (blkno % 8)); // Set the bit to 1 (used)
  release(&swap_space_bitmap.lock);
}

// Function to mark a block as free in the bitmap
void clear_bitmap(int blkno) {
  acquire(&swap_space_bitmap.lock);
  freeNum_in_bitmap++; // Increment the free block counter
  swap_space_bitmap.bitmap[blkno / 8] &= ~(0x80 >> (blkno % 8)); // Set the bit to 0 (free)
  release(&swap_space_bitmap.lock);
}


// Function to append a page to the LRU list
void append_to_lru(pde_t *pgdir, char *mem, char *va) {
  int idx = V2P(mem) >> 12; // Get the index of the page in the pages array

  acquire(&page_lock);
  num_lru_pages++;
  num_free_pages--;

  pages[idx].pgdir = pgdir;
  pages[idx].vaddr = va;

  // if the LRU list is not empty
  if (page_lru_head) {
    pages[idx].next = page_lru_head;
    pages[idx].prev = page_lru_head->prev;
    page_lru_head->prev->next = &pages[idx];
    page_lru_head->prev = &pages[idx];
  } 
  
  // if the LRU list is empty
  else {
    pages[idx].next = &pages[idx];
    pages[idx].prev = &pages[idx];
  }
  
  // update the head to the new page
  page_lru_head = &pages[idx];

  release(&page_lock);
}

// Function to remove a page from the LRU list
void remove_from_lru(char *mem) {
  int idx = V2P(mem) >> 12; // Get the index of the page in the pages array

  acquire(&page_lock);

  if (pages[idx].next != 0) {
    num_lru_pages--;
    num_free_pages++;

    // if the page is the only page in the LRU list
    if (num_lru_pages == 0) {
      page_lru_head = 0;
    } else {
      // if the page is the head of the LRU list
      if (page_lru_head == &pages[idx]) {
        page_lru_head = pages[idx].next;
      }

      // update the next and prev pointers of the surrounding pages
      pages[idx].prev->next = pages[idx].next;
      pages[idx].next->prev = pages[idx].prev;

      // handle circular references if list is circular
      if (num_lru_pages == 1) {
        page_lru_head->next = page_lru_head;
        page_lru_head->prev = page_lru_head;
      }
    }

    memset((void *)&pages[idx], 0, sizeof(struct page));
  }

  release(&page_lock);
}

// Function to swap in pages from the swap space (on page fault)
int swap_in(uint addr) {
    pte_t *pte = walkpgdir(myproc()->pgdir, (void *)PGROUNDDOWN(addr), 0);

    // Allocate memory
    char *mem = kalloc();
    if(mem == 0) {
        return -1;
    }

    // Read swapped page into memory
    int blkno = *pte >> 12;

    if (blkno < 0 || blkno >= (SWAPMAX / (PGSIZE / BSIZE))) {
        return -1;
    }

    swapread((char *)mem, blkno);
    clear_bitmap(blkno);

    // Add to LRU list
    append_to_lru(myproc()->pgdir, mem, (char *)PGROUNDDOWN(addr));

    // Update page table entry
    *pte = (*pte & ~0xFFF) | V2P(mem);
    *pte |= PTE_P;
    *pte |= PTE_A;

    return 1;
}

// Function to swap out a page to the swap space
int swap_out(void)
{ 
  // If there are no pages in the LRU list, return 0
  if (page_lru_head == 0 || num_lru_pages == 0) {
    return 0;
  }
  
  release(&kmem.lock); 
  acquire(&page_lock); 

  struct page *victim = page_lru_head;
  pte_t *pte = 0;

  while (1) {
    pte = walkpgdir(victim->pgdir, (void*)victim->vaddr, 0);

    // If PTE_A = 1, clear it and move the victim to the tail of the LRU list
    if (*pte & PTE_A) {
      *pte &= ~PTE_A;
      page_lru_head = victim->next;

      // Move to the next page
      victim = victim->next;
    } 
    
    // If not user page, remove from LRU list
    else if ((*pte & PTE_U) == 0) {
      remove_from_lru(P2V(PTE_ADDR(*pte)));
    }

    // If PTE_A = 0, evict the page
    else {
      break;
    }
  }

  release(&page_lock); 

  // Get block number of the victim page
  int blkno = get_blkno();
  if (blkno == -1) {
    return 0;
  }

  char *addr = P2V(PTE_ADDR(*pte));

  // Write the victim page to the swap space and set the bitmap
  swapwrite(addr, blkno);
  set_bitmap(blkno);

  // Remove the victim page from the LRU list
  remove_from_lru(addr);

  // Update the page table entry
  kfree(addr);
  *pte = (*pte & ~0xFFF) | (blkno << 12);
  *pte &= ~PTE_P;

  return 1;
}