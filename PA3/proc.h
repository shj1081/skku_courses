// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;
extern uint weightArr[40];  // Weight array for CFS

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// structure for mmap_area
struct mmap_area {
  struct file *file;  // file descriptor
  uint addr;          // start address of the mmap area
  uint length;        // length of the mmap area
  uint offset;        // offset of the mmap area
  int prot;           // protection of the mmap area
  int flags;          // flags of the mmap area
  struct proc *p;  // process that the mmap area belongs to
  int isUsed;         // check if the mmap area is used
  int isPageTableExist; // check if the page table is exist
};


// MMAPBASE of each process : 0x40000000
#define MMAPBASE 0x40000000

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  uint niceValue;               // Nice value of the process (priority)

  // for CFS
  int time_slice;              // Time slice of the process (by militicks)
  uint runtime;                 // Runtime of the process (by militicks)
  uint vruntime;                // Virtual runtime of the process (by militicks)

  // for mmaped area
};

// total ticks of the system
extern uint totalTicks;

// for page fault handler
int page_fault_handler(struct trapframe *tf);


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
