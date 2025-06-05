#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]){

    char* va = (char*)malloc(4096);

    if(va == 0){
        printf("DEBUG: malloc failed - shmem_test - va\n");
        return -1;
    }

    int pid = fork();
    // parent process
    if(pid != 0){

    }
    // child
    else{
        //uint64 memaddr = map_shared_pages(pid, );
    }

}