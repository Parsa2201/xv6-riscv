#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void * hello_world(void *arg)
{
    printf("Hello World (thread) %lu\n", (uint64)arg);
    return 0;
}

int main(int argc, char *argv)
{
    printf("Hello World (main)\n");
    uint tid;
    void *stack = malloc(500);
    if (create_thread(&tid, hello_world, (void *) 1, stack, 500) < 0)
        printf("Error making thread\n");
    else
        printf("Succeeded!\n");

    while(1)
        ;
    join_thread(tid);
}