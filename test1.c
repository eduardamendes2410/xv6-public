#include "types.h"
#include "date.h"
#include "user.h"

int main() {
    int pid = fork();
    if (pid > 0) {
        change_prio(pid, 3);
        sleep(1);
        getprio();
    	wait();
    } else {
    	change_prio(pid, 0);
        sleep(1);
        getprio();
    }
    exit();
}