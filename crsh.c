#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BUF_SIZE 128
#define DEFAULT_DIR getenv("HOME") 
extern int errno;

static const char * PS1 = "crsh> ";

static char * arg_buf[BUF_SIZE];
static char * path[BUF_SIZE];

/* Static function declarations. */
static char * alloc_str(char * str_ptr, const char * token);
static void clear_str_arr(char ** str_arr);
static void concat_path();
static bool run_builtin();
static void run_child_proc();
static void run_cmd_str(char * str);
static void run_from_file();
static void tokenize(char ** str_arr, char * str, char delim);

/**
 * Tokenizes the string passed in the first buffer so that
 * it can fill the second buffer with an array of strings.
 */
static void tokenize(char ** str_arr, char * buf, char delim) {
  int i = 0;

  // Buffer for the current token (cleared of junk initially).
  char token[BUF_SIZE];

  // If any of these are null, simply return.
  if (!buf || !str_arr) {
    return;
  }

  bzero(token, BUF_SIZE);
  for (i = 0; *buf; buf++) {
    // Onces the delimiter is encountered, store the
    // preceding string that has been built up inside
    // of the buffer, and continue to tokenize the rest
    // of the string.
    if (*buf == delim) {
      str_arr[i] = alloc_str(str_arr[i], token);
      // Clear the buffer.
      bzero(token, BUF_SIZE);
      ++i;
    } else {
      // If the delimiter has not been encountered, keep filling
      // the buffer.
      strncat(token, buf, 1);
    }
  }
  str_arr[i] = alloc_str(str_arr[i], token);
}

static char * alloc_str(char * str_ptr, const char * token) {
  // Make space to place the token onto the heap.
  // (don't forget to add the null terminator).
  str_ptr = (char *) malloc((strlen(token) + 1) * sizeof(char));
  if (!str_ptr) 
    exit(EXIT_FAILURE);
  strncpy(str_ptr, token, strlen(token) + 1);
  return str_ptr;
}

/**
 * Forks a process to be run via the OS.  And then patiently
 * waits for said process to finish.
 */
static void run_child_proc() {
  int status;
  pid_t ret = fork();
  if (ret == 0) {
    status= execv(arg_buf[0], arg_buf);
    // If there was some sort of error, leave the
    // forked env.
    if (status < 0) {
      printf("%s: no such command\n", arg_buf[0]);
      exit(1);
    }
  } else {
    wait(NULL);
  }
}

static void run_from_file() {
  int fd;
  unsigned int file_offset = 0;
  int file_status = 0;
  unsigned int i;

  // The read buffer, and the command buffer (the command
  // buffer is filled as the read buffer is filled).
  char rbuf[BUF_SIZE];
  char cbuf[BUF_SIZE];
  fd = open(arg_buf[1], O_RDONLY);
  if (fd < 0) {
    switch(errno) {
      case EACCES:
        printf("%s: read access denied.\n", \
            arg_buf[1]);
        break;
      case EEXIST:
        printf("%s: no such file.\n", \
            arg_buf[1]);
        break;
      case EISDIR:
        printf("%s: is a directory.\n", \
            arg_buf[1]);
        break;
      default:
        printf("%s: read access error.\n", \
            arg_buf[1]);
    }
  } else {
    // Read a full buffer's worth of text, then
    // execute the line within said buffer.
    // Then go to the next line and repeat.
    do {
      bzero(rbuf, BUF_SIZE);
      bzero(cbuf, BUF_SIZE);
      file_status = pread(fd, rbuf, BUF_SIZE, file_offset);
      if (file_status < 0) {
        printf("error while reading file.\n");
        break;
      } 		

      for(i = 0; rbuf[i] != EOF \
          && rbuf[i] != '\0' \
          && rbuf[i] != '\n' \
          && i < BUF_SIZE; i++) {

        cbuf[i] = rbuf[i];
      }

      if (rbuf[i] == '\n')
        i++;

      file_offset += i; 

      // Make sure to clear the arg buffer
      // BEFORE each command is to be run.
      // This is so that the actual "run from file"
      // command can then be cleared, and when this
      // string of commands is finished running,
      // the arg buffer will then be cleared
      // in the main loop.
      clear_str_arr(arg_buf);	
      run_cmd_str(cbuf);	
    } while (file_status != 0 && i != 0);

    close(fd);
  }
}

/**
 * Attempt to run one of the builtin commands.
 * If a command was not run, then return false.
 */
static bool run_builtin() {
  int ret;	

  if (strncmp(arg_buf[0], "cd", 3) == 0) {
    // If no args are supplied, go to the default
    // directory.
    if (arg_buf[1])
      ret = chdir(arg_buf[1]);
    else
      ret = chdir(DEFAULT_DIR);

    // If there was some sort of error, deal with
    // whichever ones are likely to show up.
    if (ret < 0) {
      switch (errno) {
        case ENOENT:
          printf("%s: no such directory\n", arg_buf[1]);
          break;
        case ENOTDIR:
          printf("%s: not a directory\n", arg_buf[1]);
          break;
        case ELOOP:
          printf("%s: symbolic loop encountered\n", arg_buf[1]);
          break;
        default:
          printf("%s: error encountered\n", arg_buf[1]);
          break;
      }
    }	
    return true;
  }

  if (strncmp(arg_buf[0], "exit", 5) == 0) {
    clear_str_arr(arg_buf);
    clear_str_arr(path);
    exit(EXIT_SUCCESS);
  }

  if (strncmp(arg_buf[0], ".", BUF_SIZE) == 0) {
    run_from_file();		
    return true;
  }

  return false;
}

/**
 * Searches through the list of paths in the path variable
 * (as hopefully determined by the getenv command), and if
 * an image is found, stores the absolute path where the arg variable
 * is pointing.
 */
static void concat_path(char ** path, char ** argv) {
  int i;
  // File handle for checking if a given file exists.
  int fd;
  char buf[BUF_SIZE];
  for (i = 0; path[i]; i++) {
    bzero(buf, BUF_SIZE);
    // Concatenate the current path to the variable,
    // then check to see if the image file exists.
    // If not, move to the next candidate.
    strncat(buf, path[i], strlen(path[i])); 
    strncat(buf, "/", 1);
    strncat(buf, argv[0], strlen(argv[0]));
    fd = open(buf, O_RDONLY);
    if (fd > 0) {
      free(argv[0]);
      argv[0] = alloc_str(argv[0], buf);
      close(fd);
      return;
    }
  }
}

/**
 * Clears the allocated values within a string
 * array.  The array must have all values filled
 * sequentially o be fully cleared (i.e. no empty NULL
 * values).
 */
static void clear_str_arr(char ** str_arr) {
  int i;
  for (i = 0; str_arr[i]; i++) {
    free(str_arr[i]);
    str_arr[i] = NULL;
  }
}

static void run_cmd_str(char * str) {
  if (str[0] != '\0') {
    // Tokenize the command.  And then
    // store the tokens in the argument
    // buffer.
    tokenize(arg_buf, str, ' ');

    // Execute the command and then
    // free any arguments.
    if (!run_builtin()) {	
      // If the main argument doesn't
      // appear to be an absolute
      // path, then search for the value
      // within the path and run it.
      if (!index(arg_buf[0], '/'))
        concat_path(path, arg_buf);
      run_child_proc();
    }

    // Clear the arg buffer.
    clear_str_arr(arg_buf);
  }
}

int main(int argc, char ** argv, char ** envp) {
  char buf[BUF_SIZE];
  char input = '\0';
  // Grab the path from the environment.
  tokenize(path, getenv("PATH"), ':');

  // Grab all characters typed in until a newline
  // is encountered.
  printf(PS1);
  bzero(buf, BUF_SIZE);
  while (input != EOF) {
    input = getchar();

    // Once the user has hit enter,
    // process the command entered.
    // Else keep building the command within
    // the buffer.
    switch (input) {
      case '\n':
        // Run the command, clear the buffer,
        // then print the prompt again.
        run_cmd_str(buf);				
        bzero(buf, BUF_SIZE);
        printf(PS1);
        break;
      default:
        // If there is no new line,
        // simply concatenate the string
        // typed thus far.
        strncat(buf, &input, 1);	
        break;
    }

  }
  clear_str_arr(path);
  printf("\n");
  return EXIT_SUCCESS;
}
