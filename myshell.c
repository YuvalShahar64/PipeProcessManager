#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <LineParser.h>

#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20
#define MAX_BUF 200

typedef struct process {
    cmdLine *cmd;
    pid_t pid;
    int status;
    struct process *next;
} process;

process *process_list = NULL;

// Process management functions
void addProcess(process **process_list, cmdLine *cmd, pid_t pid) {
    process *new_process = (process *)malloc(sizeof(process));
    if (new_process == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    new_process->cmd = cmd;
    new_process->pid = pid;
    new_process->status = RUNNING;
    new_process->next = *process_list;
    *process_list = new_process;
}

void updateProcessStatus(process *process_list, int pid, int status) {
    process *curr_process = process_list;
    while (curr_process != NULL) {
        if (curr_process->pid == pid) {
            curr_process->status = status;
            return;
        }
        curr_process = curr_process->next;
    }
}

void updateProcessList(process **process_list) {
    process *curr_process = *process_list;
    int status;
    pid_t result;
    
    while (curr_process != NULL) {
        result = waitpid(curr_process->pid, &status, WNOHANG);
        if (result == -1) {
            perror("waitpid failed");
            return;
        }
        if (result > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                curr_process->status = TERMINATED;
            } else if (WIFSTOPPED(status)) {
                curr_process->status = SUSPENDED;
            } else if (WIFCONTINUED(status)) {
                curr_process->status = RUNNING;
            }
        }
        curr_process = curr_process->next;
    }
}

void freeProcessList(process **process_list) {
    process *curr_process = *process_list;
    process *next_process;
    
    while (curr_process != NULL) {
        next_process = curr_process->next;
        freeCmdLines(curr_process->cmd); // Free the cmdLine structure
        free(curr_process); // Free the process structure
        curr_process = next_process;
    }
    
    *process_list = NULL; // Set process list to NULL after freeing
}

void printProcessList(process **process_list) {
    updateProcessList(process_list);
    process *curr_process = *process_list;
    process *prev = NULL;
    
    printf("PID\t\tCommand\t\tSTATUS\n");
    while (curr_process != NULL) {
        char *status;
        
        switch (curr_process->status) {
            case RUNNING:
                status = "Running";
                break;
            case SUSPENDED:
                status = "Suspended";
                break;
            case TERMINATED:
                status = "Terminated";
                break;
            default:
                status = "Unknown";
                break;
        }
        
        printf("%d\t\t%s\t%s\n", curr_process->pid, curr_process->cmd->arguments[0], status);
        
        if (curr_process->status == TERMINATED) {
            if (prev == NULL) {
                *process_list = curr_process->next;
            } else {
                prev->next = curr_process->next;
            }
            
            freeCmdLines(curr_process->cmd); // Free the cmdLine structure
            free(curr_process); // Free the process structure
            curr_process = prev ? prev->next : *process_list;
        } else {
            prev = curr_process;
            curr_process = curr_process->next;
        }
    }
}

void execute(cmdLine *pCmdLine, int debugMode) {
    int runInBackground = pCmdLine->blocking == 0 ? 1 : 0;

    // Check for pipes
    if (pCmdLine->next) {
        int pipefd[2];
        pid_t pid1, pid2;

        if (pipe(pipefd) == -1) {
            perror("pipe failed");
            return;
        }

        pid1 = fork();
        if (pid1 == -1) {
            perror("fork failed");
            return;
        }

        if (pid1 == 0) {
            // Child process 1 (left-hand side of the pipe)
            close(pipefd[0]);  // Close unused read end
            if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
                perror("dup2 failed");
                _exit(EXIT_FAILURE);
            }
            close(pipefd[1]);  // Close the write end of the pipe

            // Handle input redirection
            if (pCmdLine->inputRedirect) {
                int inputFd = open(pCmdLine->inputRedirect, O_RDONLY);
                if (inputFd == -1) {
                    perror("open inputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(inputFd, STDIN_FILENO) == -1) {
                    perror("dup2 inputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                close(inputFd);
            }

            if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
                perror("execvp failed");
                _exit(EXIT_FAILURE);
            }
        }

        // Parent continues to the next part of the pipe
        pid2 = fork();
        if (pid2 == -1) {
            perror("fork failed");
            return;
        }

        if (pid2 == 0) {
            // Child process 2 (right-hand side of the pipe)
            close(pipefd[1]);  // Close unused write end
            if (dup2(pipefd[0], STDIN_FILENO) == -1) {
                perror("dup2 failed");
                _exit(EXIT_FAILURE);
            }
            close(pipefd[0]);  // Close the read end of the pipe

            // Handle output redirection
            if (pCmdLine->next->outputRedirect) {
                int outputFd = open(pCmdLine->next->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (outputFd == -1) {
                    perror("open outputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(outputFd, STDOUT_FILENO) == -1) {
                    perror("dup2 outputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                close(outputFd);
            }

            if (execvp(pCmdLine->next->arguments[0], pCmdLine->next->arguments) == -1) {
                perror("execvp failed");
                _exit(EXIT_FAILURE);
            }
        }

        // Parent process
        close(pipefd[0]);
        close(pipefd[1]);

        // Wait for the last child if not in background
        if (!runInBackground) {
            int status;
            waitpid(pid2, &status, 0);
        }

    } else {
        // No pipes, execute single command
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            _exit(EXIT_FAILURE);
        }

        // Child process
        if (pid == 0) {
            if (debugMode) {
                fprintf(stderr, "PID: %d\n", getpid());
                fprintf(stderr, "Executing command: %s\n", pCmdLine->arguments[0]);
            }

            // Handle input redirection
            if (pCmdLine->inputRedirect) {
                int inputFd = open(pCmdLine->inputRedirect, O_RDONLY);
                if (inputFd == -1) {
                    perror("open inputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(inputFd, STDIN_FILENO) == -1) {
                    perror("dup2 inputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                close(inputFd);
            }

            // Handle output redirection
            if (pCmdLine->outputRedirect) {
                int outputFd = open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (outputFd == -1) {
                    perror("open outputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                if (dup2(outputFd, STDOUT_FILENO) == -1) {
                    perror("dup2 outputRedirect failed");
                    _exit(EXIT_FAILURE);
                }
                close(outputFd);
            }

            if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
                perror("execvp failed");
                _exit(EXIT_FAILURE);
            }
        } else {
            // Parent process
            if (!runInBackground) {
                int status;
                waitpid(pid, &status, 0);
            } else {
                if (debugMode) {
                    fprintf(stderr, "Background process started: %s (PID: %d)\n", pCmdLine->arguments[0], pid);
                }
                addProcess(&process_list, pCmdLine, pid);
            }
        }
    }
}

void alarm_command(int pid, cmdLine *pCmdLine) {
    if (kill(pid, SIGCONT) == -1) {
        perror("kill failed");
    } else {
        printf("Process with PID %d resumed successfully.\n", pid);
        updateProcessStatus(process_list, pid, RUNNING);
    }
}

void blast_command(int pid, cmdLine *pCmdLine) {
    if (kill(pid, SIGINT) == -1) {
        perror("kill failed");
    } else {
        printf("Process with PID %d terminated successfully.\n", pid);
        updateProcessStatus(process_list, pid, TERMINATED);
    }
}

void sleep_command(int pid, cmdLine *pCmdLine) {
    if (kill(pid, SIGTSTP) == -1) {
        perror("kill failed");
    } else {
        printf("Process with PID %d suspended successfully.\n", pid);
        updateProcessStatus(process_list, pid, SUSPENDED);
    }
}

char *get_history_file_path() {
    char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        perror("getenv HOME failed");
        exit(EXIT_FAILURE);
    }
    
    static char history_file_path[PATH_MAX];
    snprintf(history_file_path, PATH_MAX, "%s/.shell_history", home_dir);
    
    return history_file_path;
}

void load_history(char history[HISTLEN][MAX_BUF]) {
    char *history_file_path = get_history_file_path();
    FILE *fp = fopen(history_file_path, "r");
    if (fp == NULL) {
        return; // No history file yet
    }
    
    char line[MAX_BUF];
    int i = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (i >= HISTLEN) {
            break;
        }
        strcpy(history[i], line);
        i++;
    }
    
    fclose(fp);
}

void save_history(char history[HISTLEN][MAX_BUF], int count) {
    char *history_file_path = get_history_file_path();
    FILE *fp = fopen(history_file_path, "w");
    if (fp == NULL) {
        perror("failed to open history file for writing");
        return;
    }
    
    int start = (count > HISTLEN) ? count - HISTLEN : 0;
    for (int i = start; i < count; i++) {
        fputs(history[i % HISTLEN], fp);
    }
    
    fclose(fp);
}

void print_history(char history[HISTLEN][MAX_BUF], int count) {
    int start = (count > HISTLEN) ? count - HISTLEN : 0;
    for (int i = start; i < count; i++) {
        printf("%4d  %s\n", i , history[i % HISTLEN]);
    }
}

void add_to_history(char history[HISTLEN][MAX_BUF], char *command, int *count) {
    int pos = *count % HISTLEN;
    strcpy(history[pos], command);
    *count = *count + 1;
}

void free_history(char history[HISTLEN][MAX_BUF]) {
    // Nothing to free in a static array
}

int main(int argc, char **argv) {
    char buffer[PATH_MAX];
    cmdLine *line;
    int debugMode = 0;
    char history[HISTLEN][MAX_BUF];
    int hist_count = 0;
    load_history(history);
    hist_count = 0; 
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debugMode = 1;
        }
    }
    
    while (1) {
        if (getcwd(buffer, sizeof(buffer)) != NULL) {
            printf("%s$ ", buffer);
        } else {
            perror("getcwd error");
            return 1;
        }
        
        char userInput[2048];
        fgets(userInput, sizeof(userInput), stdin);
        
        // Remove newline character from userInput
        userInput[strcspn(userInput, "\n")] = 0;
        
        // Handle history commands
        if (strcmp(userInput, "quit") == 0) {
            break;
        } else if (strcmp(userInput, "history") == 0) {
            print_history(history, hist_count);
            continue;
        } else if (strncmp(userInput, "!!", 2) == 0) {
            int idx = hist_count - 1;
            if (idx >= 0) {
                strcpy(userInput, history[idx % HISTLEN]);
            } else {
                fprintf(stderr, "No commands in history.\n");
                continue;
            }
        } else if (userInput[0] == '!') {
            int idx = atoi(&userInput[1]) - 1;
            if (idx >= 0 && idx < hist_count) {
                strcpy(userInput, history[idx % HISTLEN]);
            } else {
                fprintf(stderr, "No such command in history.\n");
                continue;
            }
        }


        add_to_history(history, userInput, &hist_count);
        
        line = parseCmdLines(userInput);
        if (line == NULL) {
            continue;
        }
        
        if (strcmp(line->arguments[0], "procs") == 0) {
            printProcessList(&process_list);
            freeCmdLines(line);
            continue;
        }
        
        if (strcmp(line->arguments[0], "alarm") == 0) {
            if (line->argCount < 2) {
                fprintf(stderr, "Usage: alarm <pid>\n");
            } else {
                int pid = atoi(line->arguments[1]);
                alarm_command(pid, line);
            }
            freeCmdLines(line);
            continue;
        }
        
        if (strcmp(line->arguments[0], "blast") == 0) {
            if (line->argCount < 2) {
                fprintf(stderr, "Usage: blast <pid>\n");
            } else {
                int pid = atoi(line->arguments[1]);
                blast_command(pid, line);
            }
            freeCmdLines(line);
            continue;
        }
        
        if (strcmp(line->arguments[0], "sleep") == 0) {
            if (line->argCount < 2) {
                fprintf(stderr, "Usage: sleep <pid>\n");
            } else {
                int pid = atoi(line->arguments[1]);
                sleep_command(pid, line);
            }
            freeCmdLines(line);
            continue;
        }
        
        execute(line, debugMode);
    }
    
    // Save history to file
    save_history(history, hist_count);
    
    freeProcessList(&process_list);
    free_history(history);
    return 0;
}
