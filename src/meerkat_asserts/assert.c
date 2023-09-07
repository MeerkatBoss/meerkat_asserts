#include "meerkat_asserts/assert.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#define GDB_FORK_FAILED  "<gdbForkFailed/>"
#define GDB_NO_GDB       "<gdbNotFound/>"
#define GDB_BT_START     "<gdbBacktrace>"
#define GDB_BT_END       "</gdbBacktrace>"
#define GDB_LOCALS_START "<gdbLocals>"
#define GDB_LOCALS_END   "</gdbLocals>"

static void process_gdb_messages(int out_fd, int gdb_fd,
                                 const char* function_name);
static int run_gdb(int pipe_write_fd, pid_t tracee);

// TODO: handle absence of gdb

void __assert_print(int fd, const char* file, const char* function, size_t line,
                   const char* condition, const char* message)
{
  // Print original message
  FILE* stream = fdopen(fd, "w");
  setvbuf(stream, NULL, _IONBF, 0);
  fprintf(stream, "Failed assertion '%s': %s\n"
              "\tat %s:%zu\n"
              "\tin function %s\n",
              condition, message, file, line, function);

  // Create pipe for gdb
  int pipes[2] = { -1, -1 };
  errno = 0;
  int pipe_status = pipe(pipes);
  // If failed to create pipe
  if (pipe_status != 0)
  {
    // Report error
    fprintf(stream, "assert failed to create anonymous pipe: %s\n",
            strerror(errno));
    // Finish printing
    return;
  }
  const int read_pipe = pipes[0];
  const int write_pipe = pipes[1];

  // Fork process
  errno = 0;
  const int gdb_pid = fork();
  // If fork error
  if (gdb_pid < 0)
  {
    // Report error
    fprintf(stream, "assert failed to run fork(): %s\n",
            strerror(errno));
    // Finish printing
    return;
  }

  // If in parent process
  if (gdb_pid != 0)
  {
    // Close write pipe
    close(write_pipe);
    // Process gdb output
    process_gdb_messages(fd, read_pipe, function);
    // Reap gdb
    int gdb_status = 0;
    waitpid(gdb_pid, &gdb_status, 0);
    // Finish printing
    return;
  }

  // Fork process
  errno = 0;
  const int tracee_pid = fork();
  // If fork error
  if (tracee_pid < 0)
  {
    // Report error
    write(write_pipe, GDB_FORK_FAILED, sizeof(GDB_FORK_FAILED) - 1);
    // Terminate process
    _exit(1);
  }

  // If in parent process
  if (tracee_pid > 0)
  {
    // Close read pipe
    close(read_pipe);
    // Try to start gdb
    int gdb_start_status = run_gdb(write_pipe, tracee_pid);
    // If failed to start gdb
    if (gdb_start_status < 0)
    {
      // Report absence of gdb
      write(write_pipe, GDB_NO_GDB, sizeof(GDB_NO_GDB) - 1);
      // Terminate process
      _exit(1);
    }
    // UNREACHABLE
    _exit(0);
  }

  // Wait for debugger to attach
  raise(SIGSTOP);
  // Exit process
  _exit(0);
}

static int run_gdb(int pipe_write_fd, pid_t tracee)
{
  enum { pid_str_len = 32, log_file_str_len = 64 };
  static char pid_str[pid_str_len] = "";
  static char log_file_str[log_file_str_len] = "";

  // Convert tracee pid to string
  snprintf(pid_str, pid_str_len, "%d", tracee);

  // Build log file command
  pid_t my_pid = getpid();
  snprintf(log_file_str, log_file_str_len,
      "set logging file /proc/%d/fd/%d",
      my_pid, pipe_write_fd);
  
  // Redirect stderr to /dev/null
  int dev_null = open("/dev/null", O_WRONLY);
  dup2(dev_null, STDERR_FILENO);

  // Execute gdb
  int result = execlp("gdb",
    "gdb", "-p", pid_str, "-q", "-batch-silent",
    "-ex", "set pagination off",              // Disable pagination
    "-ex", "return",                          // Return from raise()
    "-ex", "return",                          // Return from __assert_print
    "-ex", log_file_str,                      // Set pipe as log file
    "-ex", "set logging redirect off",        // Do not redirect logs to stdout
    "-ex", "set logging on",                  // Start logging
    "-ex", "echo " GDB_BT_START "\\n",        // Mark start of backtrace
    "-ex", "bt -entry-values both",           // Print backtrace
    "-ex", "echo " GDB_BT_END "\\n",          // Mark end of backtrace
    "-ex", "echo " GDB_LOCALS_START "\\n",    // Mark start of local vars
    "-ex", "info locals",                     // Print local variables
    "-ex", "echo " GDB_LOCALS_END "\\n",      // Mark end of local vars
    "-ex", "set logging off",                 // End logging
    "-ex", "kill",
    NULL);

  // If failed to execute
  if (result < 0)
  {
    // Return error
    return result;
  }

  // UNREACHABLE: return sucess
  return 0;
}

static void process_backtrace(FILE* out_stream, FILE* gdb_stream);
static void process_locals(FILE* out_stream, FILE* gdb_stream,
                           const char* function_name);

static void process_gdb_messages(int out_fd, int gdb_fd,
                                 const char* function_name)
{
  // Create stream for output
  FILE* out_stream = fdopen(out_fd, "w");

  // Create stream for gdb
  FILE* gdb_stream = fdopen(gdb_fd, "r");
  // Enable line buffering for gdb stream
  setlinebuf(gdb_stream);
  
  // Read loop
  while(1)
  {
    // Try to read line from buffer
    char* line = NULL;
    size_t line_size = 0;   
    ssize_t line_len = getline(&line, &line_size, gdb_stream);
    // If EOF reached
    if (line_len < 0)
    {
      // Stop reading
      break;
    }

    // If line matches ForkFailed tag
    if (strcmp(line, GDB_FORK_FAILED "\n") == 0)
    {
      // Report error
      fputs("assert failed to run fork", out_stream);
      // Stop reading
      break;
    }
    // If line matches NoGdb tag
    if (strcmp(line, GDB_NO_GDB "\n") == 0)
    {
      // Report error
      fputs("Install gdb to get more detailed assert info", out_stream);
      // Stop reading
      break;
    }
    // If line matches Backtrace opening tag
    if (strcmp(line, GDB_BT_START "\n") == 0)
    {
      // Process Backtrace tag
      process_backtrace(out_stream, gdb_stream);
    }
    // If line matches Locals openining tag
    if (strcmp(line, GDB_LOCALS_START "\n") == 0)
    {
      // Process Locals tag
      process_locals(out_stream, gdb_stream, function_name);
    }
    free(line);
  }

  // Close read stream
  fclose(gdb_stream);
}

static void process_backtrace(FILE* out_stream, FILE* gdb_stream)
{
  // Print preamble
  fprintf(out_stream, "BACKTRACE:\n");

  // Read loop
  while(1)
  {
    // Try read line
    char* line = NULL;
    size_t line_size = 0;   
    ssize_t line_len = getline(&line, &line_size, gdb_stream);
    // If EOF reached
    if (line_len < 0)
    {
      // Stop processing
      return;
    }

    // If closing tag
    if (strcmp(line, GDB_BT_END "\n") == 0)
    {
      // Stop processing
      return;
    }

    // Print line with \t
    fprintf(out_stream, "\t%s", line);
    free(line);
  }
}

static void process_locals(FILE* out_stream, FILE* gdb_stream,
                           const char* function_name)
{
  // Print preamble
  fprintf(out_stream, "LOCAL VARIABLES OF %s:\n", function_name);

  // Read loop
  while(1)
  {
    // Try read line
    char* line = NULL;
    size_t line_size = 0;   
    ssize_t line_len = getline(&line, &line_size, gdb_stream);
    // If EOF reached
    if (line_len < 0)
    {
      // Stop processing
      return;
    }

    // If closing tag
    if (strcmp(line, GDB_LOCALS_END "\n") == 0)
    {
      // Stop processing
      return;
    }

    // Print line with \t
    fprintf(out_stream, "\t%s", line);
    free(line);
  }
}
