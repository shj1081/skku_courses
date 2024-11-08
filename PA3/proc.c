#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "elf.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

char* state_str[6];
uint weightArr[40] = {
  88761, 71755, 56483, 46273, 36291, 
  29154, 23254, 18705, 14949, 11916, 
  9548, 7620, 6100, 4904, 3906, 
  3121, 2501, 1991, 1586, 1277, 
  1024, 820, 655, 526, 423, 
  335, 272, 215, 172, 137, 
  110, 87, 70, 56, 45, 
  36, 29, 23, 18, 15};


// mmap_area Array
struct mmap_area mmap_area_arr[64];

void initialize_mmap_area(int index) {
  mmap_area_arr[index].isUsed = 0;
  mmap_area_arr[index].isPageTableExist = 0;
  mmap_area_arr[index].file = 0;
  mmap_area_arr[index].addr = 0;
  mmap_area_arr[index].length = 0;
  mmap_area_arr[index].offset = 0;
  mmap_area_arr[index].prot = 0;
  mmap_area_arr[index].flags = 0;
  mmap_area_arr[index].p = 0;
}

void ps(int pid);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // set nice value to default (20)
  p->niceValue = 20;
  p->runtime = 0;
  p->vruntime = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  // initialize the mmap_area_arr (all is not used)
  for (int i = 0; i < 64; i++) {
    initialize_mmap_area(i);
  }

  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  
  // inherit the nice value/weight/runtime/vruntime from parent
  np->niceValue = curproc->niceValue;
  np->runtime = curproc->runtime;
  np->vruntime = curproc->vruntime;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  
  // copy the mmap_area_arr
  for (int i = 0; i < 64; i++) {
    if (mmap_area_arr[i].isUsed == 1 && mmap_area_arr[i].p == curproc) {
      
      // find the first unused mmap_area from mmap_area_arr
      for (int j = 0; j < 64; j++) {
        if (mmap_area_arr[j].isUsed == 0) {

          // set the mmap_area
          mmap_area_arr[j].isUsed = 1;
          mmap_area_arr[j].isPageTableExist = 1;
          mmap_area_arr[j].file = (mmap_area_arr[i].file == 0) ? 0 : filedup(mmap_area_arr[i].file); // if not anonymous, file_to_map is not used (set to 0) 
          mmap_area_arr[j].addr = mmap_area_arr[i].addr;
          mmap_area_arr[j].length = mmap_area_arr[i].length;
          mmap_area_arr[j].offset = mmap_area_arr[i].offset;
          mmap_area_arr[j].prot = mmap_area_arr[i].prot;
          mmap_area_arr[j].flags = mmap_area_arr[i].flags;
          mmap_area_arr[j].p = np;

          // If the physical page of parent is already assigned, get the physical page assigned and map it like a parent
          if (mmap_area_arr[i].isPageTableExist == 1) {
            for (uint k = mmap_area_arr[j].addr; k < mmap_area_arr[j].addr + mmap_area_arr[j].length; k += PGSIZE) {
              // page table of parent process
              pte_t *pte = walkpgdir(curproc->pgdir, (void *)k, 0);
              
              // page table entry is not valid, continue
              if (!pte || !(*pte & PTE_P)) {
                continue;
              } 

              // page table entry is valid, child newly allocate physical page and map it
              else {
                char *physical_page;
                if ((physical_page = kalloc()) == 0) {
                  return -1;
                } 
                
                // initialize the physical memory page
                memset(physical_page, 0, PGSIZE);

                // copy the content of the physical page of parent to the physical page of child
                memmove(physical_page, P2V(PTE_ADDR(*pte)), PGSIZE);

                // set the page table entry for the mapping area
                mappages(np->pgdir, (void *)k, PGSIZE, V2P(physical_page), mmap_area_arr[j].prot|PTE_U);
              }
            }
            mmap_area_arr[j].isPageTableExist = 1;
          }   
          break;
        }
      }
    }
  }
  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  uint minVruntime = __UINT32_MAX__;
  uint totalWeight = 0;
  struct proc *chosenProc = 0;


  for(;;){

    // Enable interrupts on this processor.
    sti();
    

    acquire(&ptable.lock);
    // find the process with the minimum vruntime
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE) {
        if (p->vruntime < minVruntime) {
          minVruntime = p->vruntime;
          chosenProc = p;
        }
        totalWeight += weightArr[p->niceValue];
      }
    }

    if (chosenProc != 0) {
    
      p = chosenProc;
      p->time_slice = 10000 * weightArr[p->niceValue] / totalWeight; // time_slice = 10 ticks * weight / totalWeight

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    minVruntime = __UINT32_MAX__;
    totalWeight = 0;
    chosenProc = 0;

    release(&ptable.lock);

  }
}

void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  uint minVruntime = __UINT32_MAX__;
  int noRunnable = 1;

  // check the minimum vruntime process
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE) {
      noRunnable = 0;
      if (p->vruntime < minVruntime) {
        minVruntime = p->vruntime;
      }
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      if (noRunnable) {
        p->vruntime = 0;
      } else {
        p->vruntime = minVruntime-(1000*1024/weightArr[p->niceValue]); // vruntime = minVruntime in ready queue - vruntime(1 tick)
      }
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


// Get process name
int
getpname(int pid)
{
  struct proc *p;

  if (pid <= 0) {
    return -1;
  }

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      cprintf("%s (%d)\n", p->name, p->niceValue);
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// get the process nice value
int
getnice(int pid) 
{
  struct proc *p;

  if (pid <= 0) {
    return -1;
  }

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      int niceVal = p->niceValue;
      release(&ptable.lock);
      return niceVal;
    }
  }
  release(&ptable.lock);
  return -1;
}

// set the process nice value to new one
int 
setnice(int pid, int value)
{
  struct proc *p;

  if (pid <= 0) {
    return -1;
  }

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid && 0 <= value && value <= 39){
      p->niceValue = value;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}


char* state_str[6] = {"UNUSED  ", "EMBRYO  ", "SLEEPING", "RUNNABLE", "RUNNING ", "ZOMBIE  "};

void ps(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  if(pid == 0) {
    // print header
    cprintf("PID\tName\tState\t\tpriority\truntime/Weight\tRuntime\t\tvruntime\tticks ");
    cprintf("%u\n", totalTicks * 1000);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(!p->state == UNUSED) {
        cprintf("%d\t", p->pid);
        cprintf("%s\t", p->name);
        cprintf("%s\t", state_str[p->state]);
        cprintf("%u\t\t", p->niceValue);
        cprintf("%u\t\t", p->runtime/weightArr[p->niceValue]);
        cprintf("%u\t\t", p->runtime);
        cprintf("%u\n", p->vruntime);
      }
    }
    release(&ptable.lock);
    return;
  }
  else {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->pid == pid){
        cprintf("PID\tName\t\tState\tpriority\truntime/Weight\tRuntime\t\tvruntime\tticks ");
        cprintf("%u\n", totalTicks * 1000);
        cprintf("%d\t", p->pid);
        cprintf("%s\t", p->name);
        cprintf("%s\t", state_str[p->state]);
        cprintf("%u\t\t", p->niceValue);
        cprintf("%u\t\t", p->runtime/weightArr[p->niceValue]);
        cprintf("%u\t\t", p->runtime);
        cprintf("%u\n", p->vruntime);
        release(&ptable.lock);
        return;
      }
    }
    release(&ptable.lock);
    return;
  }
}

// mmap system call
// success return cases :
//    - return the start address of mapping area
// fail return cases :
//    1. It's not anonymous, but when the fd is -1
//    2. The prot of the parameter should be within the protection of the file
//    3. The addr and length are not page-aligned
//    4. The (physical) memory cannot be allocated
//    5. The mapping of virtual address to physical address fails
//    6. If MAP_ANONYMOUS, but fd is not -1 or offset is not 0
//    - The situation in which the mapping area is overlapped is not considered
//    - If additional errors occur, we will let you know by writing notification
uint mmap(uint addr, int length, int prot, int flags, int fd, int offset) {
  // fail return case 1 : It's not anonymous, but when the fd is -1
  if (!(flags & MAP_ANONYMOUS) && fd == -1) { // & : bitwise AND (cannot use != bcs options are given in bit form)
    return 0;
  }

  // fail return case 3 : The addr and length are not page-aligned
  if (addr % PGSIZE != 0 || length % PGSIZE != 0) {
    return 0;
  }

  // fail return case 6 : If MAP_ANONYMOUS, but fd is not -1 or offset is not 0
  if ((flags & MAP_ANONYMOUS) && (fd != -1 || offset != 0)) {
    return 0;
  }
  
  // get current process
  struct proc *current_proc = myproc();
  struct file *file_to_map = 0; // if MAP_ANONYMOUS, file_to_map is not used (set to 0)
  
  // addr and length should be multiples of PGSIZE
  // start address of mapping
  uint start_addr = MMAPBASE + addr; // 0x40000000 + addr

  // end address of mapping
  uint end_addr = start_addr + length;
  
  // if not ANONYMOUS, check the file descriptor
  if (!(flags & MAP_ANONYMOUS)){
    file_to_map = current_proc->ofile[fd]; // get file with fd from current process
    
    // fail return case 2 : The prot of the parameter should be within the protection of the file
    // case 2-1 : file has no read permission but prot has read permission
    if ((prot & PROT_READ) && !(file_to_map->readable)) {
      return 0;
    }

    // case 2-2 : file has no write permission but prot has write permission (only)
    if ((prot & PROT_WRITE) && !(file_to_map->writable)) { 
      return 0;
    }
  }

  // find the first unused mmap_area from mmap_area_arr
  // and set the mmap_area
  int target_idx;
  for (target_idx = 0; target_idx < 64; target_idx++) {
    if (mmap_area_arr[target_idx].isUsed == 0) {
      mmap_area_arr[target_idx].isUsed = 1;
      mmap_area_arr[target_idx].file = (file_to_map == 0) ? 0 : filedup(file_to_map); // if not anonymous, file_to_map is not used (set to 0) 
      mmap_area_arr[target_idx].addr = start_addr;
      mmap_area_arr[target_idx].length = length;
      mmap_area_arr[target_idx].offset = offset;
      mmap_area_arr[target_idx].prot = prot;
      mmap_area_arr[target_idx].flags = flags;
      mmap_area_arr[target_idx].p = current_proc;
      break;
    }
  }

  // if MAP_POPULATE
  if (flags & MAP_POPULATE) {
    // allocate physical memory for the mapping area
    for (uint i = start_addr; i < end_addr; i += PGSIZE) {
      char *phyical_page; // for storing physical memory pointer

      // kalloc : allocate 1 page of physical memory
      // fail return case 4 : The (physical) memory cannot be allocated
      if ((phyical_page = kalloc()) == 0) { 
        return 0;
      } 
      // initialize the physical memory page
      else {
        memset(phyical_page, 0, PGSIZE);
      }
      
      // set the page table entry for the mapping area ()
      // mappages : set the page table entry for the mapping area
      /*
      @params:
        pde_t *pgdir: page directory
        void *va: virtual address
        uint size: size of the memory
        uint pa: physical address
        int perm: permission
      */
      // V2P : virtual address to physical address
      // fail return case 5 : The mapping of virtual address to physical address fails (mappages() returns -1 when fails)
      if (mappages(current_proc->pgdir, (void *)i, PGSIZE, V2P(phyical_page), prot|PTE_U) < 0) { // PTE_U : user permission for user accessible memory (mmu.h)
        kfree(phyical_page);
        return 0;
      }

      // if not anonymous (and yes MAP_POPULATE), read the file and write to the physical memory
      if (!(flags & MAP_ANONYMOUS)) {
        // set the file offset
        file_to_map->off = offset;
        // read the file and write to the physical memory (fileread() in file.c)
        /*
        @params:
          struct file *f: file 
          char *addr: memory address to write
          int n: size of the memory to read & write
        */
        // no need to check error case (fileread() panics when error occurs)
        fileread(file_to_map, phyical_page, PGSIZE); // read and write for 1 page (iterate)
      }
    }
    mmap_area_arr[target_idx].isPageTableExist = 1;
  }
  
  // if MAP_POPULATE, allocate physical memory for the mapping area and set the page table entry and return the start address
  // if not MAP_POPULATE, just set the mmap_area and return the start address
  return start_addr;
}

// NOTE: Unlike mmap(), munmap()'s argument addr is the start address of the mapping region
// munmap system call
// success return cases : 1
// fail return cases :
//   1. If the mmap_area with the addr is not found, return -1
//   2. If not page-aligned, return 0 
int munmap(uint addr) {

  // failure return case 2 : If not page-aligned
  if (addr % PGSIZE != 0) {
    return 0;
  }

  struct proc *current_proc = myproc();

  // find the mmap_area with the addr
  // addr will be always given with the start address of mapping region, which is page aligned
  int target_index;
  for (target_index = 0; target_index < 64; target_index++) {
    if (mmap_area_arr[target_index].isUsed == 1 && mmap_area_arr[target_index].addr == addr) {
      break;
    }
  }

  // fail return case 1 : if the mmap_area with the addr is not found
  if (target_index == 64) {
    return -1;
  }

  // If physical page is allocated & page table is constructed, should free physical page & page table
  if (mmap_area_arr[target_index].isPageTableExist == 1) {
    for (uint i = addr; i < addr + mmap_area_arr[target_index].length; i += PGSIZE) {
      // get the page table entry
      pte_t *pte = walkpgdir(current_proc->pgdir, (void *)i, 0);

      // Notice) In one mmap_area, situation of some of pages are allocated and some are not can happen.
      // If physical page is not allocated or present bit is not set(page table entry is not valid), continue
      if (!pte || !(*pte & PTE_P)) { // PTE_P : present bit (mmu.h), use the same logic with copyuvm() 
        continue; 
      } 
      // If physical page is allocated,
      else {
        // find the physical page pointer from the page table entry
        char *physical_page = P2V(PTE_ADDR(*pte)); // P2V : physical address to virtual address (mmu.h) for kfree()
        
        // When freeing the physical page should fill with 1
        memset(physical_page, 1, PGSIZE);
        
        // free the physical page
        kfree(physical_page);

        // clear the page table entry
        *pte = 0;
      }
    }
  }

  // decrease the reference count of the file, if the file is not anonymous
  if (mmap_area_arr[target_index].file != 0  && mmap_area_arr[target_index].file->ref > 0) {
    mmap_area_arr[target_index].file->ref--;
  }

  // If physical page is allocated, free the physical page and remove mmap_area structure
  // If physical page is not allocated, just remove mmap_area structure
  initialize_mmap_area(target_index);
  return 1; // success
}

// freemem system call
int freemem() {
  return get_free_page_count();
}

// NOTE: Don't know why the compile error occurs when the function is located in the trap.c
// page fault handler
// success case : 
//   - Physical pages and page table entries are created normally, and the process works without any problems
// fail case : return -1 and terminate the process
//   1. If the page fault address is not in the mmap_area
//   2. If the access is write and the protection of the mmap_area is write prohibited
//   3. If the physical page allocation fails
//   4. If the page table mapping fails
int page_fault_handler(struct trapframe *tf) {
  struct proc *current_proc = myproc();
  // Get the address that caused the page fault
  // rc2() returns the linear address of the most recent page fault (x86.h)
  uint page_fault_addr = rcr2();

  // determine the access was read or write
  int isWrite = tf->err & 0x2; // 0 : read, 1 : write

  // find the mmap_area that contains the page fault address from mmap_area_arr
  int target_index;
  for (target_index = 0; target_index < 64; target_index++) {
    if (mmap_area_arr[target_index].isUsed == 1 && mmap_area_arr[target_index].p == current_proc) {
      if (page_fault_addr >= mmap_area_arr[target_index].addr && page_fault_addr < mmap_area_arr[target_index].addr + mmap_area_arr[target_index].length) {
        break;
      }
    }
  }

  // fail case 1 : If the page fault address is not in the mmap_area
  if (target_index == 64) {
    cprintf("Page fault address is not in the mmap_area\n");
    current_proc->killed = 1;
    return -1;
  }

  // fail case 2 : If the access is write and the protection of the mmap_area is write prohibited
  if (isWrite == 1 && (mmap_area_arr[target_index].prot & PROT_WRITE) == 0) {
    cprintf("Write prohibited\n");
    current_proc->killed = 1;
    return -1;
  }

  // For only one page according to faulted address, allocate a physical page and map it to the page table
  
  // Allocate a physical page
  char *physical_page;

  // fail case 3 : If the physical page allocation fails
  if ((physical_page = kalloc()) == 0) {
    cprintf("kalloc failed\n");
    current_proc->killed = 1;
    return -1;
  } 
  // initialize the physical page
  else {
    memset(physical_page, 0, PGSIZE);
  }

  // Map the physical page to the page table (same as the mmap() function)
  // page dowon align address
  uint aligned_addr = mmap_area_arr[target_index].addr & 0xFFFFF000;
  // fail case 4 : If the page table mapping fails
  if (mappages(current_proc->pgdir, (void *)(aligned_addr), PGSIZE, V2P(physical_page), mmap_area_arr[target_index].prot | PTE_U) < 0) {
    cprintf("mappages failed\n");
    kfree(physical_page);
    current_proc->killed = 1;
    return -1;
  }

  // If not anonymous, read the file and write to the physical memory (same as the mmap() function)
  if (mmap_area_arr[target_index].file != 0) {
    // get the file to map from mmap_area_arr
    struct file *file_to_map = mmap_area_arr[target_index].file;
    int original_offset = mmap_area_arr[target_index].offset;
    file_to_map->off = mmap_area_arr[target_index].offset + (page_fault_addr - aligned_addr);
    fileread(file_to_map, physical_page, PGSIZE);
    file_to_map->off = original_offset;
  }

  // flag for page table existence is set to 1
  mmap_area_arr[target_index].isPageTableExist = 1;
  return 0; // success
}