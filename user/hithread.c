#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void * hello_world(void *arg)
{
    printf("Hello World (thread) %lu\n", (uint64)arg);
    // while(1)
    //     ;
    return 0;
}

int main(int argc, char *argv)
{
    printf("Hello World (main)\n");
    uint tid1;
    uint tid2;
    void *stack1 = malloc(500);
    void *stack2 = malloc(500);
    if (create_thread(&tid1, hello_world, (void *)1, stack1, 500) < 0)
        printf("Error making thread\n");
    else
        printf("Succeeded!\n");
    if (create_thread(&tid2, hello_world, (void *) 2, stack2, 500) < 0)
        printf("Error making thread\n");
    else
        printf("Succeeded!\n");

    // while(1)
    //     ;
    join_thread(tid1);
    join_thread(tid2);
    printf("I'm awake :)))\n");
}
