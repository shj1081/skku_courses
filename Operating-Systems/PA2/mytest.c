#include "types.h"
#include "stat.h"
#include "user.h"

void busy_work() {
    double val = 1.0;
    while (1) {
        val = val * 1.000001 + 0.000001;  // Just a random calculation
    }
}

int main(void) {
    int pid;
    printf(1, "Testing runnable state\n");

    pid = fork();
    if (pid < 0) {
        printf(2, "Fork failed\n");
        exit();
    }

    // Child process
    if (pid == 0) {
        setnice(getpid(), 5);  // Set nice value to 10
        busy_work();
        exit();
    }

    // Parent process
    setnice(getpid(), 0);  // Set nice value to 1
    uint j = 0;
    int count = 0;
    while (1) {
        j++;
        if (j % 10000000000 == 0) {  // swui에서 돌릴거면 10^9, lab에서 돌릴거면 10^10
            ps(0);
            printf(1, "\n");
            count++;
        }
        if (count == 5) {
            kill(pid);
            wait();
            printf(1, "\nTest complete\n");
            break;
        }
    }

    exit();
}
