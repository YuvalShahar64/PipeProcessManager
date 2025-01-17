#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    int pipefd[2];
    pid_t child1, child2;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    fprintf(stderr, "(parent_process>forking...)\n");
    child1 = fork();

    if (child1 == -1) {
        perror("fork");
        exit(1);
    }
    if (child1 == 0) { 
        close(STDOUT_FILENO);
        fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe...)\n");
        dup(pipefd[1]);        
        close(pipefd[1]);                     // Close duplicate
        char *args[] = {"ls", "-l", NULL};    // Array of arguments
        fprintf(stderr, "(child1>going to execute cmd: ls -l)\n");
        execvp("ls", args);                   
        perror("execvp");
        exit(1);
    }
    fprintf(stderr, "(parent_process>created process with id: %d)\n", child1);

    fprintf(stderr, "(parent_process>closing the write end of the pipe…)\n");
    close(pipefd[1]);     

    fprintf(stderr, "(parent_process>forking...)\n");
    child2 = fork();

    if (child2 == -1) {
        perror("fork");
        exit(1);
    }

    if (child2 == 0) { 
        close(STDIN_FILENO); 
        fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe...)\n");
        dup(pipefd[0]);                       
        close(pipefd[0]);                     // Close duplicate
        char *args[] = {"tail", "-n", "2", NULL}; // Array of arguments
        fprintf(stderr, "(child2>going to execute cmd: tail -n 2)\n");
        execvp("tail", args);                     
        perror("execvp");
        exit(1);
    }
    fprintf(stderr, "(parent_process>created process with id: %d)\n", child2);

    fprintf(stderr, "(parent_process>closing the read end of the pipe…)\n");
    close(pipefd[0]);     

    fprintf(stderr, "(parent_process>waiting for child processes to terminate...)\n");
    waitpid(child1, NULL, 0);
    waitpid(child2, NULL, 0);

    fprintf(stderr, "(parent_process>exiting...)\n");
    return 0;
}