#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

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
    if (n == 0)
      str_len = 1;
    else {
      while (n > 0)
      {
        n /= 10;
        str_len++;
      }
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

char *
state_to_str(enum procstate s)
{
  switch (s)
  {
    case UNUSED:
      return "unused";
      break;
    case USED:
      return "used";
      break;
    case SLEEPING:
      return "sleeping";
      break;
    case RUNNABLE:
      return "runnable";
      break;
    case RUNNING:
      return "running";
      break;
    case ZOMBIE:
      return "zombie";
      break;
  }
  return "unkown";
}

int
main(int argc, char *argv[])
{
  printf("Hello World!\n");
  int a = 0;
  for (long long i = 0; i < 1000000000; i++)
    a += 2;
  uint usage = cpu_usage();
  printf("my usage is %d\n", usage);

  struct top t = {0};
  int r;
  if ((r = top(&t)) < 0) {
    printf("There was a problem making the top syscall.\n");
    return 0;
  }
  
  printf("number of processes: %d\n", t.count);
  my_print_str("PID", 10);
  my_print_str("PPID", 10);
  my_print_str("STATE", 16);
  my_print_str("NAME", 16);
  my_print_str("START", 10);
  my_print_str("USAGE", 10);
  printf("\n");

  for (struct top_proc_info *inf = t.procs; inf < &t.procs[t.count]; inf++) {
    my_print_int(inf->pid, 10);
    my_print_int(inf->ppid, 10);
    my_print_str(state_to_str(inf->state), 16);
    my_print_str(inf->name, 16);
    my_print_int(inf->usage.start, 10);
    my_print_int(inf->usage.sum, 10);
    printf("\n");
  }
}
