#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// #define NPROC        64  // maximum number of processes

// enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc_info {
  char name[16];
  int pid;
  int ppid;
  enum procstate state;
};

struct child_processes
{
  int count;
  struct proc_info processes[NPROC];
};

void my_print_str(char *str, int output_size)
{
    printf(str);
    int str_len = strlen(str);
    int num_spaces = output_size - str_len;
    for (int i = 0; i < num_spaces; i++)
        printf(" ");
}

void my_print_int(int n, int output_size)
{
    printf("%d", n);
    int str_len = 0;
    while (n > 0){
        n /= 10;
        str_len++;
    }
    int num_spaces = output_size - str_len;
    for (int i = 0; i < num_spaces; i++)
        printf(" ");
}

void my_print_hex(int n, int output_size)
{
    printf("0x%xd", n);
    int str_len = 0;
    while (n > 0){
        n /= 10;
        str_len++;
    }
    int num_spaces = output_size - str_len - 2;
    for (int i = 0; i < num_spaces; i++)
        printf(" ");
}

# define MAX_REPORT_BUFFER_SIZE 10

struct report
{
  char pname[16];       // Process Name
  int pid;              // Process ID
  uint64 scause;        // Supervisor Trap Cause
  uint64 sepc;          // Supervisor Exception Program Counter
  uint64 stval;         // Supervisor Trap Value
  int parents[4];   // Grand parents pid
  int parents_count;    // Number of grand parents
};

struct report_traps
{
  struct report reports[MAX_REPORT_BUFFER_SIZE];
  int count;
};

void print_report()
{
    struct report_traps rt = {0};
    if(report_traps(&rt) < 0)
        printf("reporting traps failed.");
    printf("number of exceptions: %d\n", rt.count);
    printf("PID      PNAME    ");
    my_print_str("scause", 21);
    my_print_str("sepc", 21);
    my_print_str("stval", 21);
    printf("\n");
    struct report *r;
    for (int i = 0; i < rt.count; i++)
    {
        r = &(rt.reports[i]);
        my_print_int(r->pid, 9);
        my_print_str(r->pname, 9);
        my_print_hex(r->scause, 21);
        my_print_hex(r->sepc, 21);
        my_print_hex(r->stval, 21);
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    // int fd = open("parsa's file", O_CREATE | O_WRONLY);
    // write(fd, "Hallo", 5);
    // close(fd);
    // printf("Hello Parsa!\n");

    // load reports.bin file for test
    printf("loading reports.bin\n");
    int fd;
    if((fd = open("reports.bin", O_RDWR | O_CREATE)) < 0)
        printf("file did not open!");
    struct report rpt;
    read(fd, &rpt, sizeof(struct report));
    
    my_print_int(rpt.pid, 9);
    my_print_str(rpt.pname, 9);
    my_print_hex(rpt.scause, 21);
    my_print_hex(rpt.sepc, 21);
    my_print_hex(rpt.stval, 21);
    printf("\n");

    struct child_processes cp = {0};

    // Some tests
    int pid = fork();
    if(pid < 0) {
        printf("fork failed\n");
        exit(1);
    } else if(pid == 0) {
        // child
        int pid2 = fork();
        sleep(100);
        if(pid2 > 0){
            wait(0);
            printf("Hello from child 2! (pid = %d)\n", pid2);
            int *a = 0;
            *a = 0;
        } else {
            printf("Hello from child 1! (pid = %d)\n", pid2);
            int *a = 0;
            *a = 0;
        }
    }
    else
    {
        sleep(50);
        if(child_processes(&cp) < 0)
        {
            printf("child process counting failed\n");
            exit(1);
        }
        printf("child processes count = %d\n", cp.count);
        printf("PID      PPID     STATE    NAME\n");
        static char *states[] = {
        [UNUSED]    "unused",
        [USED]      "used",
        [SLEEPING]  "sleep ",
        [RUNNABLE]  "runble",
        [RUNNING]   "run   ",
        [ZOMBIE]    "zombie"
        };
        struct proc_info *child;
        for (int i = 0; i < cp.count; i++)
        {
            child = &(cp.processes[i]);
            char *state = states[child->state];
            my_print_int(child->pid, 9);
            my_print_int(child->ppid, 9);
            my_print_str(state, 9);
            my_print_str(child->name, 9);
            printf("\n");
        }
        if (wait(0) < 0)
        {
            printf("wait failed\n");
            exit(1);
        }
        printf("Hello from parent! (pid = %d)\n", pid);
        sleep(100);

        print_report();
    }
}
