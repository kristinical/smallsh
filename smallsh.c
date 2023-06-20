#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>

// GLOBAL VARIABLES
char *home;              // to replace ~/
pid_t pid;               // to replace $$
int exit_status = 0;     // to replace $?
pid_t bg_pid;            // to replace $!
char bg_str[21] = "";    // bg_pid string
bool bg_process = false;
bool bg_exit = false;
char **wordArr;
int num_words = 0;
char *input = NULL;
char *cwd;
int in_fd;
int out_fd;

// FUNCTION HEADERS
char* str_expansion(char *word);
char *str_replace(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
int exit_command();

// ERRNO CHECK HELPER FUNCTION
void errnoCheck(char *errorMsg) {
  int errorFlag = errno;
  if (errorFlag != 0) {
    exit_status = errno;
    fprintf(stderr, "%s: errno = %d\n", errorMsg, errno);
    exit_command();
  }
}

// SIGNAL HANDLER
void handle_SIGINT(int signo) {
  // DO NOTHING 
}

int main(void) {
  // Get ENV variables
  char *ps1 = getenv("PS1");
  if (!ps1) ps1 = "";
  char *ifs = getenv("IFS");
  if (!ifs) ifs = " \t\n";
  home = getenv("HOME");
  if (!home) home = "";

  input = NULL;

  // Set up custom signal handlers
  struct sigaction SIGINT_action = {0}, ignore_action = {0}, reset_SIGINT = {0}, reset_SIGTSTP = {0};
  ignore_action.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &ignore_action, &reset_SIGTSTP);
  sigaction(SIGINT, &ignore_action, &reset_SIGINT);
  errnoCheck("sigaction() error");

start:;
  // Set variables
  errno = 0;
  char *outfile = NULL;
  char *infile = NULL;
  if (in_fd) close(in_fd);
  if (out_fd) close(out_fd);
  errnoCheck("close() error");
  size_t len = 0;
  free(input);
  num_words = 0;
  pid = getpid();
  
  // minimum of 512 words shall be supported
  wordArr = malloc(512 * sizeof(*wordArr));
  if (!wordArr) {
    fprintf(stderr, "malloc error");
    exit(EXIT_FAILURE);
  }

prompt:
  // Check for un-waited-for background processes
  if (bg_process) {
    int waitStatus;
    int waitPid;
    while ((waitPid = waitpid(0, &waitStatus, WNOHANG | WUNTRACED)) > 0) {
      errnoCheck("waitpid() error");
      bg_exit = true;
      if (WIFEXITED(waitStatus)) {
        fprintf(stderr, "Child process %d done. Exit status %d\n", waitPid, WEXITSTATUS(waitStatus)); 
      }
      if (WIFSIGNALED(waitStatus)) {
        fprintf(stderr, "Child process %d done. Signaled %d\n", waitPid, WTERMSIG(waitStatus));
      }
      if (WIFSTOPPED(waitStatus)) {
        kill(waitPid, SIGCONT);
        errnoCheck("kill() error");
        fprintf(stderr, "Child process %d stopped. Continuing.\n", waitPid);
      }
    } 
    bg_process = false;
  }

  // Print prompt
  fflush(NULL);
  errnoCheck("fflush() error");
  fprintf(stderr, "%s", ps1);
  
  // Register SIGINT_action as the handler for SIGINT when reading line of input
  SIGINT_action.sa_handler = handle_SIGINT;
  SIGINT_action.sa_flags = 0;
  sigfillset(&SIGINT_action.sa_mask);
  sigaction(SIGINT, &SIGINT_action, NULL);
  errnoCheck("signal() error");
    
  // Get line of input
  int in = getline(&input, &len, stdin);
  errnoCheck("getline() error");
  
  // Handling if reading is interrupted
  if (feof(stdin) != 0) exit_command();
  
  // Re-register the ignore_action as the handler for SIGINT
  sigaction(SIGINT, &ignore_action, NULL);
  errnoCheck("sigaction() error");

  if (in == -1) {
    printf("\n");
    clearerr(stdin);
    goto prompt;
  }

  // WORD SPLITTING
  char *token = strtok(input, ifs);
  while (token != NULL) {
    *wordArr = realloc(*wordArr, sizeof **wordArr * sizeof(token) + 1);
    if (!wordArr) {
      fprintf(stderr, "realloc error");
      exit(EXIT_FAILURE);
    }
    wordArr[num_words] = strdup(token);
    errnoCheck("strdup() error");
    ++num_words;
    token = strtok(NULL, ifs);
  }
  if (num_words == 0) goto start;

  // EXPANSION
  for (size_t i = 0; i < num_words; ++i) {
    if (strlen(wordArr[i]) > 1) wordArr[i] = str_expansion(wordArr[i]);
  }
   
  // Exit command
  if (strcmp(wordArr[0], "exit") == 0) {
    if (num_words == 1) exit_command();
    else if (num_words == 2) {
      // Convert string to int
      int exit_num = 0;
      char *str = wordArr[1];
      for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] < 48 || str[i] > 57) {
          fprintf(stderr, "invalid arg for exit()\n");
          exit_status = 22;
          goto start;
        }
        exit_num = exit_num * 10 + (str[i] - 48);
      }
      exit_status = exit_num;
      exit_command();
    }
    else {
      fprintf(stderr, "too many args for exit()\n");
      exit_status = 7;
      goto start;
    }
  }
    
  // CD command
  if (strcmp(wordArr[0], "cd") == 0) {
    if (num_words == 1) chdir(home);
    else if (num_words == 2) {
      if (strcmp(wordArr[1], home) == 0) chdir(home);
      else {
        cwd = malloc(sizeof *cwd * sizeof(get_current_dir_name()) * sizeof(wordArr[1] + sizeof(char) + 1));
        if (!cwd) {
          fprintf(stderr, "realloc error");
          exit(EXIT_FAILURE);
        }
        strcat(cwd, get_current_dir_name());
        strcat(cwd, "/");
        strcat(cwd, wordArr[1]);
        chdir(cwd);
        if (errno != 0) {
          fprintf(stderr, "invalid directory or file name\n");
          exit_status = errno;
          goto start;
        }
      }
    }
    else {
      fprintf(stderr, "too many args for cd()\n");
      exit_status = 7;
      goto start;
    }
  }

  // PARSING
  // Check for comments
  int valid_words = num_words;
  for (int i = 0; i < num_words; ++i) {
    if (strcmp(wordArr[i], "#") == 0) {
      valid_words = i;
      break;
    }
  }
  if (valid_words == 0) goto start;

  // Check if background process
  if (strcmp(wordArr[valid_words - 1], "&") == 0) {
    valid_words -= 1;
    if (valid_words != 0) bg_process = true;
    else goto start;
  }
  if (valid_words == 1) goto execution;
   
  // Check for infile & outfile
  if (strcmp(wordArr[valid_words - 2], ">") == 0) {
    outfile = wordArr[valid_words - 1];
    valid_words -= 2;
  }
  else if (strcmp(wordArr[valid_words - 2], "<") == 0) {
    infile = wordArr[valid_words - 1];
    valid_words -= 2;
  }
  if (valid_words == 0) goto start;
  if (valid_words == 1) goto execution;

  if (strcmp(wordArr[valid_words - 2], "<") == 0) {
    infile = wordArr[valid_words - 1];
    valid_words -= 2;
  }
  else if (strcmp(wordArr[valid_words - 2], ">") == 0) {
    outfile = wordArr[valid_words - 1];
    valid_words -= 2;
  }
  if (valid_words == 0) goto start;

execution:;
  int childStatus;
  pid_t childPid = fork();
  if (childPid == -1) {
    fprintf(stderr, "fork() failed\n");
    exit_status = errno;
    goto start;
  }
  else if (childPid == 0) {
    // Reset signals
    sigaction(SIGINT, &reset_SIGINT, NULL);
    sigaction(SIGTSTP, &reset_SIGTSTP, NULL);
    errnoCheck("sigaction() error");
    
    // Input/Output redirection
    if (infile != NULL) {
      in_fd = open(infile, O_RDONLY | O_CREAT);
      if (errno != 0) {
        fprintf(stderr, "open() infile error\n");
        exit_status = errno;
        goto start;
      }
      dup2(in_fd, 0);
      errnoCheck("dup2() infile error\n");
    }
    if (outfile != NULL) {
      out_fd = open(outfile, O_WRONLY | O_CREAT, 0777);
      if (errno != 0) {
        fprintf(stderr, "open() outfile error\n");
        exit_status = errno;
        goto start;
      }
      dup2(out_fd, 1);
      errnoCheck("dup2() outfile error\n");
    }

    // Exec()
    char *execArr[valid_words + 1];
    for (int i = 0; i < valid_words; ++i) execArr[i] = wordArr[i];
    execArr[valid_words] = NULL;

    char *slash = strstr(execArr[0], "/");
    if (slash)execv(execArr[0], execArr);
    else execvp(execArr[0], execArr);
    if (errno != 0) {
      fprintf(stderr, "exec() error\n");
      exit_status = errno;
      goto start;
    }
  }
  else {
    if (bg_process) {
      bg_pid = childPid;
      sprintf(bg_str, "%d", bg_pid);
      waitpid(childPid, &childStatus, WNOHANG | WUNTRACED);
      errnoCheck("waitpid() error");
    }
    else {
      waitpid(childPid, &childStatus, 0);
      errnoCheck("waitpid() error");
      if (WIFEXITED(childStatus)) exit_status = WEXITSTATUS(childStatus);
      if (WIFSIGNALED(childStatus)) {
        exit_status = 128 + WTERMSIG(childStatus);
      }
      if (WIFSTOPPED(childStatus)) {
        kill(childPid, SIGCONT);
        errnoCheck("kill() error");
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) childPid);
        bg_pid = childPid;
        sprintf(bg_str, "%d", bg_pid);
        bg_process = true;
        waitpid(childPid, &childStatus, WNOHANG | WUNTRACED);
        errnoCheck("waitpid() error");
      }
    }
  }
  goto start;
}

// Function to help with expansion
char *str_expansion(char *word) {
  char *str = word;
  int iter = 0;
  if (strncmp(word, "~/", 2) == 0) {
    str = str_replace(&word, "~/", home);
    iter = strlen(home);
  }
 
  // Look for matches & initialize sub and needle strings for str_replace()
  char sub[21] = "";
  char needle[3] = {};
  for (size_t i = iter; i < strlen(str) - 1; ++i) {
    bool match = false;
    if (str[i] == '$') {
      needle[0] = '$';
      // $$
      if (str[i + 1] == '$') {
        needle[1] = '$';
        sprintf(sub, "%d", pid);
        match = true;
      }
      // $?
      else if (str[i + 1] == '?') {
        needle[1] = '?';
        sprintf(sub, "%d", exit_status);
        match = true;
      }
      // $!
      else if (str[i + 1] == '!') {
        needle[1] = '!';
        strcpy(sub, bg_str);
        match = true;
      }
    }

    if (match) {
      str = str_replace(&word, needle, sub);
      if (strlen(str) == 0) break;
      i += strlen(sub) - 1;
    }
  }
  return str;
}

// Function to replace needle with sub
char *str_replace(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub) {
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle),
         sub_len = strlen(sub);

  for (; (str = strstr(str, needle));) {
    ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) exit(EXIT_FAILURE);
      errnoCheck("realloc() error");
      *haystack = str;
      str = *haystack + off;
    }
    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }
  str = *haystack;
  
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) exit(EXIT_FAILURE);
    errnoCheck("realloc() error");
    *haystack = str;
  }
  return str;
}

// EXIT COMMAND
int exit_command() {
  //free(input);
  for (size_t i = 0; i < num_words; ++i) {
    free(wordArr[i]);
    wordArr[i] = NULL;
  }
  free(wordArr);
  fprintf(stderr, "\nexit\n");
  kill(0, SIGINT);
  exit(exit_status);
}

