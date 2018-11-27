#include <stdio.h>
#include <string.h>
#include "line_parser.h"
#include <linux/limits.h>
#include <unistd.h>
#include <stdlib.h>
#define max_chars_to_read 2048
#include <sys/wait.h>
#include <signal.h>
#include "job_control.h"
#include <termios.h>
int execute(cmd_line* line, int* leftPipe, int* rightPipe, pid_t pid);
void handle_pipe(cmd_line* line, int* fd);
void handle_child(int* fd, cmd_line* line);
void handle_parent(pid_t pid1, int* fd, cmd_line* line);
void handle_child2(int* fd2, cmd_line* line);
void handle_parent2(pid_t pid1, pid_t pid2, int* fd);
void sig_handler(int signo);
void sig_handler2(int signo);
int execute_child(cmd_line* line);
char* get_dynamically_copy(char* arr);
int str_cut(char *str, int begin, int len);

int main(){

    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGQUIT, sig_handler);
    signal(SIGCHLD, sig_handler);
    
    setpgid(getpid(), getpid());
    
    struct termios* tmodes = malloc(sizeof(struct termios)*1);
    tcgetattr(STDIN_FILENO, (struct termios*)tmodes);
    
    job** job_list = malloc(sizeof(job));
    int job_list_index=-1;
    char working_directory[PATH_MAX];
    char line[2048];
    getcwd(working_directory, PATH_MAX);
    pid_t pid;
    int status;
    int fd[2];
    while(1){
        printf("%s>", working_directory);
        fgets(line, max_chars_to_read, stdin);
    
        if(strcmp(line,"quit\n")==0){
            _exit(1);
        }
        if(strcmp(line,"\n")==0){
            continue;
        }
        if(strcmp(line,"jobs\n")==0){
            print_jobs(job_list);
            continue;
        }
        
        cmd_line* parsed_line = parse_cmd_lines(line);
        
        if(!strcmp(parsed_line->arguments[0],"fg")){
            tcgetattr(STDIN_FILENO, (struct termios*)tmodes);
            job* j = find_job_by_index(job_list[0], atoi(parsed_line->arguments[1]) - 1);
            run_job_in_foreground(job_list, j, 0, tmodes, getpid());
            continue;
        }
        
        if(!strcmp(parsed_line->arguments[0],"bg")){
            job* j = find_job_by_index(job_list[0], atoi(parsed_line->arguments[1]) - 1);
            run_job_in_background(j, 0);
            continue;
        }
        
        job* current_job = add_job(job_list, get_dynamically_copy(line));
        current_job->status = RUNNING;
        
        switch(pid = fork()){
            case -1: perror("fork failed!"); break;
            case 0 :  
                    signal(SIGTTIN, SIG_DFL);
                    signal(SIGTTOU, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);
                    current_job->pgid = getpid();;
                     pipe(fd);
                    if(execute(parsed_line, NULL, fd, getpid())<0){ //child process
                        perror("an error occured\n");
                    } _exit(1);
            default:  //waitpid(pid, &status, 0);
                      if(parsed_line->blocking==1){
                      //    while(waitpid(pid, &status, WNOHANG)<0){}
                        tcgetattr(STDIN_FILENO, (struct termios*)tmodes);
                        run_job_in_foreground(job_list, current_job, 0, tmodes, getpgrp());
                      }
        }
    }
    
    free_job_list(job_list);
    return 0;
}

void sig_handler(int signo){
 //   printf("\nThe signal %s was ignored\n", strsignal(signo));
}

/*
 * we handle each case alone to insure that we close the stdin/out
 * in the right order and openning the corresponding files descriptors
 * in a right way
 */


int execute(cmd_line* line, int* leftPipe, int* rightPipe, pid_t first_child_pid){
    int status;
    pid_t pid;
    switch(pid = fork()){
        case -1: 
                perror("fork failed\n");
                return -1;
        case 0: 
                
                setpgid(getpid(), first_child_pid);
                
                
                if(rightPipe!=NULL && line->next!=NULL){ //if there is not next pipe, keep the output to stdout
                    close(1);
                    dup(rightPipe[1]);
                    close(rightPipe[1]);
                }
                if(leftPipe!=NULL){
                    close(0);
                    dup(leftPipe[0]);
                    close(leftPipe[0]);
                }
                return execute_child(line);
        default: 
                setpgid(pid, first_child_pid);
                
                if(rightPipe!=NULL){
                    close(rightPipe[1]);
                }
                if(leftPipe!=NULL){
                    close(leftPipe[0]);
                }
                if(line->next!=NULL){
                    int new_pipe[2];
                    pipe(new_pipe);
                    return execute(line->next, rightPipe, new_pipe, first_child_pid);
                }
                if(line->blocking==1){
                    waitpid(pid, &status, 0);
                }
    }
    return 1;
}

int execute_child(cmd_line* line){
    FILE* input; 
    FILE* output;
    if(line->input_redirect != NULL){
        close(0);
        input = fopen(line->input_redirect, "r");
        if(input==NULL){
          perror("open failed: No such file or directory\n");
            return -1;
        }
    }
    if(line->output_redirect != NULL){
        close(1);
        output = fopen(line->output_redirect, "w");
    }
    
    int returned_value = execvp(line->arguments[0],line->arguments);
    
    fclose((FILE*)input);
    fclose((FILE*)output);
    return returned_value;
}

char* get_dynamically_copy(char* arr){
    int length = strlen(arr);
    char* copy = malloc(sizeof(char)*length);
    int i=0;
    while(arr[i]!='\0'){
        copy[i] = arr[i];
        i++;
    }
    return copy;
}

int str_cut(char *str, int begin, int len)
{
    int l = strlen(str);

    if (len < 0) len = l - begin;
    if (begin + len > l) len = l - begin;
    memmove(str + begin, str + begin + len, l - len + 1);

    return len;
}
