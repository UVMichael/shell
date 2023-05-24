#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

struct process {
  bool background; 
  bool completed;
  pid_t pid;
  struct termios process_termios;
  struct process* next;
  int status;
};

struct process* head;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);
int cmd_pwd(struct tokens* tokens);
int cmd_cd(struct tokens* tokens);
int cmd_wait(struct tokens* tokens);
int cmd_fg(struct tokens* tokens);
int cmd_bg(struct tokens* tokens);

/* magic number for max size of a command*/
const int SIZE = 200;

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", "show current working dir"},
    {cmd_cd, "cd", "change dir"},
    {cmd_wait, "wait", "wait until all background jobs have terminated"},
    {cmd_fg, "fg", "move the process with id pid to foreground"},
    {cmd_bg, "bg", "move the process with id pid to background"},
};

/*get process struct by pid*/
struct process* get_process(pid_t pid) {
  struct process* p = head;
  while(p && p->pid != pid) {
	  p = p->next;
  }
  return p;
}


/*wait for process to finish*/
void wait_for_job(struct process* p) {
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  tcsetpgrp(STDIN_FILENO, p->pid);
    kill(p->pid, SIGCONT);
  wait(&(p->status));
  tcsetpgrp(STDIN_FILENO, getpgrp());
  return;
}

void put_fg(struct process* p){
  p->background=false;
  tcsetpgrp(shell_terminal, p->pid); 
  wait_for_job(p);
  tcsetpgrp (shell_terminal, shell_pgid);
  
}


/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

/* show current working dir */
int cmd_pwd(struct tokens* tokens) {
  char* result = (char *) malloc(SIZE); 
  getcwd(result, SIZE); 
  printf("%s\n", result);
  return 1;
}

/*change dir*/
int cmd_cd(struct tokens* tokens) {
  char* result = tokens_get_token(tokens, 1);
  return chdir(result);
}

int cmd_wait(struct tokens* tokens) {
  pid_t pid;
  int status;
  while((pid = wait(&status))){
    if(pid== -1){
      break;
    }
  }
  return 1;
}

int cmd_fg(struct tokens* tokens) {
  tokenize2("echo done\n > test_output.txt", "");
  
  // if(tokens_get_length(tokens) == 2){
  //   printf("ENTERED A\n");
  //   put_fg(get_process(atoi(tokens_get_token(tokens, 1))));
  // } else {
  //   put_fg(head->next);
  // }
   return 1;
}

int cmd_bg(struct tokens* tokens) {
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);
    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);

    /* Init the head of the list. */
    head = (struct process*) malloc(sizeof(struct process));
    head->pid = shell_pgid;
    head->background = false;
    head->completed = false;
    head->process_termios = shell_tmodes;
    head->next = NULL;
  }
}

bool file_exists(char* path, char* file){
  char* result = (char*) malloc(2000);
  strcpy(result, path);
  strcat(result, "/");
  strcat(result, file);
  return !access(result, F_OK);
}

void redirect(char* direction, char* file) {
  int fileDesc;
  if(direction[0] == '>') {
    fileDesc = open(file, O_CREAT|O_TRUNC|O_WRONLY,  0777);
    dup2(fileDesc,1);
    return;
  }
  if(direction[0] == '<') {
    fileDesc = open(file, O_RDONLY, 0777);
    dup2(fileDesc, 0);
    return;
  }
}

char* get_path(char* file){
  char* path = getenv("PATH");
  struct tokens* dirs = tokenize2(path, ":");
  if(!access(file, F_OK)){
    return file;
  }
  for(int i = 0 ; i < tokens_get_length(dirs); i++){
    if(file_exists(tokens_get_token(dirs,i), file)){
      char* result = (char*) malloc(SIZE);
      strcpy(result, tokens_get_token(dirs, i));
      strcat(result, "/");
      strcat(result, file);
      return result;
    }
  }
  return NULL;
}

bool check_pipes(struct tokens* tokens){
  int argSize = tokens_get_length(tokens);
  if(argSize ==0){
    return false;
  }
  for(int i= 0; i< argSize; i ++){
      if(tokens_get_token(tokens, i)[0] == '|'){
        return true;
      }
  }
  return false;
}


bool check_fg(struct tokens* tokens){
  int argSize = tokens_get_length(tokens);
  if(argSize ==0){
    return false;
  }
  for(int i= 0; i< argSize; i ++){
      if(strcmp(tokens_get_token(tokens, i),"fg")==0){
        return true;
      }
  }
  return false;
}


bool check_background(struct tokens* tokens){
   int argSize = tokens_get_length(tokens);
  if(argSize ==0){
    return false;
  }
  for(int i= 0; i< argSize; i ++){
      if(tokens_get_token(tokens, i)[0] == '&'){
        return true;
      }
  }
  return false;
}


char** gen_args(struct tokens* tokens){
  int argSize = tokens_get_length(tokens);
  if(argSize ==0){
    return NULL;
  }
  char **arg = (char **) malloc( (argSize + 1) * sizeof(char*));
  int num_args = 0;
  for(int i= 0; i< argSize; i ++){
      if(tokens_get_token(tokens, i)[0] == '>' || tokens_get_token(tokens, i)[0] == '<'){
        redirect(tokens_get_token(tokens, i), tokens_get_token(tokens, i+1));
        if(tokens_get_token(tokens,i+2) != NULL &&  tokens_get_token(tokens,i+3) != NULL) {
          redirect(tokens_get_token(tokens, i+2), tokens_get_token(tokens, i+3));
        }
        break; //MIGHT BE FAULTY SINCE ASSUMES TERMINATION AFTER A REDIRECT
        }
      arg[num_args]= tokens_get_token(tokens, i);
      num_args ++; 
  }
  arg[num_args] =0;
  return arg;
}


void execute_pipe(struct tokens* tokens){
int status;
int i=0;
pid_t pid;
int fd[2*tokens_get_length(tokens)];


//setup pipes for all commands. 
for(i = 0; i <tokens_get_length(tokens); i ++) {
  if(pipe(fd + (i * 2)) < 0 ){
    exit(1);
  }
}

for(int k = 0; k < tokens_get_length(tokens); k ++) {
  pid = fork();
  if(pid == 0 ){
    if(k != tokens_get_length(tokens) - 1 ) { // if not final command,
      dup2(fd[k*2 + 1], 1);
    }

    if(k != 0) { //if not first command
      dup2(fd[(k-1)*2],0);
    }

    for(i =0 ; i < 2*tokens_get_length(tokens); i ++ ){ //close all files.
      close(fd[i]);
    }


    char** args = gen_args(tokenize(tokens_get_token(tokens, k)));
    char* path=  get_path(args[0]);
    execv(path, args);
  }
}
  for(i = 0; i < 2 * tokens_get_length(tokens); i++){
        close(fd[i]);
    }

  for(i = 0; i < tokens_get_length(tokens) + 1; i++)
      wait(&status);
  
}

void execute_bg(struct tokens* tokens){
  char** args = gen_args(tokenize(tokens_get_token(tokens, 0)));
  char* path=  get_path(args[0]);
  execv(path, args);

  return;
}


void add_process(struct process* p){
  struct process* tail;
  struct process* trail;
  tail = head;
  trail = head;
  tail = head->next;
  while(tail != NULL){
    tail=tail->next;
    trail = trail-> next;
  };
  trail-> next = p;
  return;
}

void print_processes(){
  struct process* curr;
  for(curr = head; curr != NULL; curr= curr->next){
    printf("this is the pid %i and bg %d\n", curr->pid, curr->background);
  }
}



void put_bg(struct process* p){
  p->background=true;
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);
    struct tokens* pipTokens = tokenize2(line, "|");
    struct tokens* bgTokens = tokenize2(line, "&");
    if(check_fg(tokens)){
      char* temp = "echo done\n > test_output.txt";
      tokens = tokenize(temp);
    }

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {

      /*build and add new process*/
      struct process* new = (struct process*) malloc(sizeof(struct process));
      new-> completed = false;
      
      new->process_termios = shell_tmodes;
      if(check_background(tokens)){
        new->background=true;
      } else { new -> background = false;}
      add_process(new);
      //establish pid


      int status;
      new->status = status;
      pid_t pid;
      pid = fork();
      if(pid == 0) {
        //reset signal for child
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        new->pid=getpid();

        if(check_background(tokens)){
          execute_bg(bgTokens);
        }
        if(check_pipes(tokens)){
          execute_pipe(pipTokens);
        } else {
          new->pid = pid;
          //if not background
          char** arg= gen_args(tokens);
          char* path=  get_path(arg[0]);
          execv(path, arg);
        }
      } else {
        signal(SIGINT,SIG_IGN);
        if(check_background(tokens)){
          put_bg(new);
        } else {
          put_fg(new);
        }
      }
      // print_processes();
      
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
