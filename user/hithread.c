#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int a = 0, b = 0, c = 0;

void * hello_world(void *arg)
{
    printf("Hello World (thread) %lu\n", (uint64)arg);
    // while(1)
    //   ;
    int *number = (int *) &a;
    for (int i = 0; i < 100; i++) {
      (*number)++;

      if (number == &a)
        printf("thread a: %d\n", *number);
      else if (number == &b)
        printf("thread b: %d\n", *number);
      else
        printf("thread c: %d\n", *number);
    }
    return 0;
}

int main(int argc, char *argv)
{
  printf("Hello World (main)\n");
  uint tid1;
  uint tid2;
  void *stack1 = malloc(400);
  void *stack2 = malloc(400);
  printf("&a = %ld\n", (uint64) &a);
  if (create_thread(&tid1, hello_world, (void *) &a, stack1, 400) < 0)
    printf("Error making thread\n");
  else
      printf("Succeeded!\n");

  printf("&b = %ld\n", (uint64) &b);
  if (create_thread(&tid2, hello_world, (void *) &b, stack2, 400) < 0)
      printf("Error making thread\n");
  else
      printf("Succeeded!\n");

  join_thread(tid1);
  join_thread(tid2);
  // while(1)
  //     ;
  printf("I'm awake :)))\n");

  // uint t1;
  // // uint t2;
  // // uint t3;
  // void *stack1 = malloc(1000);
  // // void *stack2 = malloc(300);
  // // void *stack3 = malloc(300);
  // printf("making thread a\n");
  // if (create_thread(&t1, hello_world, (void *)&a, stack1, 1000) < 0)
  //   printf("Error making thread\n");

  // // printf("making thread b\n");
  // // if (create_thread(&t2, hello_world, (void *)&b, stack2, 150) < 0)
  // //   printf("Error making thread\n");

  // // printf("making thread c\n");
  // // if (create_thread(&t3, hello_world, (void *)&c, stack3, 150) < 0)
  // //   printf("Error making thread\n");

  // join_thread(t1);
  // // join_thread(t2);
  // // join_thread(t3);
  exit(0);
}
