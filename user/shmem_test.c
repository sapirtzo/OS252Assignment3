#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int keep_mapping = 0;
    if (argc > 1 && strcmp(argv[1], "--keep") == 0)
        keep_mapping = 1;

    char *va = (char *)malloc(4096);
    if (va == 0)
    {
        printf("malloc failed\n");
        return 1;
    }

    printf("Parent allocated va: %p\n", va);

    int pid = getpid();
    int f = fork();

    if (f == -1)
    {
        printf("fork failed\n");
        return 1;
    }

    if (f != 0)
    {
        // parent
        wait(0);
        printf("parent says va contains: %s\n", va);
    }
    else
    {
        // child
        printf("Child memory before mapping: %d\n", sbrk(0));
        uint64 p = map_shared_pages(pid, (uint64)va, 4096);
        printf("Child memory after mapping: %d\n", sbrk(0));

        if ((uint64)p < 0)
        {
            printf("map_shared_pages failed\n");
            exit(1);
        }

        strcpy((char *)p, "Hello daddy");

        if (!keep_mapping)
        {
            unmap_shared_pages(p, 4096);
            printf("Child memory after unmapping: %d\n", sbrk(0));
        }

        void *ptr = malloc(100000);
        printf("Child memory after malloc: %d\n", sbrk(0));
        free(ptr);
    }

    return 0;
}
