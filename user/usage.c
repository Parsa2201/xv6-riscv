#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
  printf("Hello World!\n");
  int a = 0;
  for (int i = 0; i < 1000000000; i++)
    a += 2;
  uint usage = cpu_usage();
  printf("my usage is %d\n", usage);
}
