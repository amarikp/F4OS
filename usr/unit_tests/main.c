/* The main function in this file is called by the scheduler
 * before beginning task switching.  The end user should use
 * this function to create tasks that should run at boot, or
 * to perform any other actions that need to take place at the
 * end of boot. 
 *
 * main() is defined in kernel/sched.h */

#include <stddef.h>
#include <stdio.h>
#include <dev/resource.h>
#include <dev/shared_mem.h>
#include <kernel/sched.h>
#include <kernel/semaphore.h>

struct semaphore abandoned_sem = {
    .lock = 0,
    .held_by = NULL,
    .waiting = NULL
};

struct semaphore deadlock_sem1 = {
    .lock = 0,
    .held_by = NULL,
    .waiting = NULL
};

struct semaphore deadlock_sem2 = {
    .lock = 0,
    .held_by = NULL,
    .waiting = NULL
};

void abandon(void);
void attempt_acquire(void);

void deadlock(void);
void deadlock2(void);

static void unit_tests(void);
static void rd_test(void);
static void ipctest(void);
static void memreader(void);
void infinite_ipc(void);

int ipc_success = 0;

void main(void) {
    new_task(&unit_tests, 1, 0);
}

void unit_tests(void) {
    printf("Press any key to start tests. (You passed the print test, by the way).\r\n");
    fprintf(stderr, "Press any key on stderr to begin tests. (You passed the stderr test).\r\n");
    getc();

    //printf("Abandoned semaphore test...");
    //new_task(&abandon, 1, 0);

    //printf("Deadlock test...");
    //deadlock();

    printf("rd_test...\r\n");
    rd_test();

    printf("Press any key to continue to infinite IPC test.\r\n");
    getc();

    printf("IPC Test...");
    infinite_ipc();
}

void abandon(void) {
    acquire(&abandoned_sem);
    new_task(&attempt_acquire, 1, 0);
}

void attempt_acquire(void) {
    acquire(&abandoned_sem);
    printf("Abandoned semaphore test passed.\r\n");
    release(&abandoned_sem);
}

void deadlock(void) {
    acquire(&deadlock_sem1);
    new_task(&deadlock2, 1, 0);

    printf("Deadlock 1 is spending ages printing to the UART so that deadlock 2 can steal my other semaphore.\r\n");
    acquire(&deadlock_sem2);
    printf("Deadlock 1 acquired both semaphores.\r\n");
}

void deadlock2(void) {
    acquire(&deadlock_sem2);
    acquire(&deadlock_sem1);
    printf("Deadlock 2 acquired both semaphores.\r\n");
}

void rd_test(void) {
    rd_t rd[RESOURCE_TABLE_SIZE] = {0};
    int max = RESOURCE_TABLE_SIZE - curr_task->task->top_rd;

    for (int i = 0; i < max; i++) {
        rd[i] = open_shared_mem();

        fprintf(rd[i], "Hello from resource %d.\r\n", rd[i]);
    }

    fprintf(rd[max-1], "Test passed.\r\n");

    printf("Opened %d resources.\r\nClosing resource %d.\r\n", max, rd[6]);
    close(rd[6]);
    
    rd[6] = open_shared_mem();
    fprintf(rd[6], "Hello from new resource %d.  You passed the test!\r\n", rd[6]);

    for (int i = 0; i < max; i++) {
        char buf[64] = {'\0'};

        read(rd[i], buf, 64);

        printf("%s", buf);

        close(rd[i]);
    }
}

void ipctest() {
    rd_t memrd = open_shared_mem();
    swrite(memrd, "IPC Test passed.\r\n");
    new_task(&memreader, 1, 0);
}

void memreader(void) {
    char buf[20];
    rd_t memrd = curr_task->task->top_rd - 1;

    read(memrd, buf, 18);
    buf[19] = 0x00;

    puts(buf);

    close(memrd);
}

void infinite_ipc(void) {
    int count = 1;
    while (1) {
        new_task(&ipctest, 1, 0);
        printf("Loop: %d\r\n", ++count);

        /* Wait for number of tasks to come down */
        while (approx_num_tasks() > 7);
    }
}
