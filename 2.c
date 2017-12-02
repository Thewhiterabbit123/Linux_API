#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>

#define FORK_NUM 40
#define NAME 8

int main() {

    char name[FORK_NUM][NAME];
    memset(name, '\0', sizeof(char)*NAME*FORK_NUM);

    sem_t* lock[FORK_NUM];

    for(int i = 0; i < FORK_NUM; i++){
        sprintf(name[i],"%s%d","sem",i+1);

        if ((lock[i] = sem_open(name[i], O_CREAT, 0644, 0)) == SEM_FAILED) {
            int len = strlen(strerror(errno));
            write(STDERR_FILENO, strerror(errno), len);
            exit(1);
        }
    }

    sem_post((lock[0]));

    for(int i = 0; i < FORK_NUM; i++){
        if(fork() == 0){
            sem_wait(lock[i]);
            printf("%d \n",i + 1);
            sem_post(lock[i+1]);
            exit(0);
        }
    }

    for(int i = 0; i < FORK_NUM; i++){
        wait(NULL);
    }

    for(int i = 0; i < FORK_NUM; i++){
        if (sem_unlink(name[i])) {
            int len = strlen(strerror(errno));
            write(STDERR_FILENO, strerror(errno), len);
            exit(1);
        }
    }
    return EXIT_SUCCESS;
}