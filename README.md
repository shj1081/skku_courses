# SKKU Operating Systems Assignments

This repository contains implementations of several operating systems projects from the SWE3004 course, each organized into separate folders. Each project folder includes its implementation files and documentation, meeting course requirements and coding standards.

# Projects Included

## PA 0: Introduction to xv6 (integrated to PA2)
- **Objective:** Provide hands-on experience with the xv6 operating system.
- **Grade:** 100/100
- **Key Features:** 
  - Clone the xv6 repository.
  - Boot xv6.
  - Customize the boot message to display student ID, name, and an optional message.

## PA 1: System Call
- **Objective:** Implement three new system calls (`getnice`, `setnice`, `ps`) in the xv6 operating system.
- **Grade:** 100/100
- **Key Features:** 
  - `getnice(int pid)`: Retrieves the nice value of a process.
  - `setnice(int pid, int value)`: Sets the nice value of a process.
  - `ps(int pid)`: Prints process information, including name, pid, state, and nice value.

## PA 2: CPU Scheduling
- **Objective:** Implement the Completely Fair Scheduler (CFS) in the xv6 operating system and modify the `ps` system call to display scheduling information.
- **Grade:** 100/100
- **Key Features:**
  - Implement CFS to manage process scheduling based on priority.
  - Modify `ps` system call to output runtime, weight, vruntime, and other relevant scheduling information.

## PA 3: Virtual Memory
- **Objective:** Implement virtual memory support in the xv6 operating system.
- **Grade:** 84/100
- **Key Features:**
  - Implement `mmap()`, `munmap()`, and `freemem()` system calls.
  - Implement a page fault handler to manage memory accesses to mapped regions.

## PA 4: Page Replacement
- **Objective:** Implement page-level swapping and manage swappable pages using the LRU (Least Recently Used) list.
- **Grade:** 84/100
- **Key Features:**
  - Implement swap-in and swap-out operations to move pages between main memory and backing store.
  - Manage swappable pages with an LRU list.
  - Implement the clock algorithm for page replacement.
 
# Notes
In order to run on mac, the following changes need to be made to the `Makefile`:
1. Remove `-Werror` from `CFLAGS`
```shell
CFLAGS = -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -MD -ggdb -m32 -fno-omit-frame-pointer
```
2. Uncomment the following two sections
```shell
# If the makefile can't find QEMU, specify its path here
QEMU = qemu-system-i386

# Using native tools (e.g., on X86 Linux)
TOOLPREFIX = i686-elf-
```
