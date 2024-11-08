#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

int main() {
    int a, b;
    swapstat(&a, &b);
    printf(1, "Initial Swap space: %d/%d\n", a, b);

    // Allocate a significant amount of memory in the parent process
    void *first = sbrk(4096);
    memset(first, 1, 4096);

    for (int i = 0; i < 30000; i++) {
        void *p = sbrk(4096);
        if (p == (void *)-1) {
            printf(1, "sbrk failed\n");
            break;
        }
    }

    swapstat(&a, &b);
    printf(1, "After parent allocation, Swap space: %d/%d\n", a, b);

    int pid = fork();
    if (pid == 0) {  // Child process
        printf(1, "Child process started\n");

        // Allocate memory in the child process to force swapping
        for (int i = 0; i < 20000; i++) {
            void *p = sbrk(4096);
            if (p == (void *)-1) {
                printf(1, "sbrk failed in child\n");
                break;
            }
        }

        swapstat(&a, &b);
        printf(1, "Child Swap space: %d/%d\n", a, b);
        exit();
    } else {
        wait();  // Wait for child process to finish

        // Access the first allocated memory to trigger swap-in
        printf(1, "Accessing first allocated memory: %d\n", *(int *)first);

        swapstat(&a, &b);
        printf(1, "Final Swap space: %d/%d\n", a, b);

        exit();
    }
}
