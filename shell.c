#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_LINE_LEN 512 // maximum length of a command line
#define MAX_ARGS 128     // maximum number of arguments in a command line

int main(int argc, char **argv)
{
  while (1)
  {
    // Print the shell prompt
    printf("shell~ ");
    fflush(stdout); // flush the output buffer

    // Read a command line
    char line[MAX_LINE_LEN];
    if (fgets(line, MAX_LINE_LEN, stdin) == NULL)
    {
      // End of input reached
      break;
    }

    // Parse the command line into arguments
    char *argv[MAX_ARGS]; // array of arguments
    int argc = 0;         // number of arguments
    char *token = strtok(line, " \t\n");
    while (token != NULL && argc < MAX_ARGS - 1)
    {
      argv[argc++] = token;
      token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL; // null-terminate the argument list

    if (argc == 0)
    {
      // No command given, skip to the next iteration
      continue;
    }

    if (strcmp(argv[0], "exit") == 0)
    {
      // The user has entered the "exit" command, terminate the shell
      break;
    }

    // Check if the command needs to be run in the background
    int background = 0;
    if (strcmp(argv[argc - 1], "&") == 0)
    {
      background = 1;
      argv[--argc] = NULL; // remove the "&" from the argument list
    }

    // Check if the command uses pipes or redirection
    int pipe_index = -1;      // index of the "|" operator in the argument list
    int redir_in_index = -1;  // index of the "<" operator in the argument list
    int redir_out_index = -1; // index of the ">" operator in the argument list
    for (int i = 0; i < argc; i++)
    {
      if (strcmp(argv[i], "|") == 0)
      {
        pipe_index = i;
        break;
      }
      else if (strcmp(argv[i], "<") == 0)
      {
        redir_in_index = i;
        break;
      }
      else if (strcmp(argv[i], ">") == 0)
      {
        redir_out_index = i;
        break;
      }
    }

    if (pipe_index >= 0)
    {
      // The command uses pipes, create a pipe and two child processes to execute the two commands
      int pipefd[2];
      if (pipe(pipefd) < 0)
      {
        perror("Error creating pipe");
        continue;
      }

      // Split the command into the two parts separated by the pipe operator
      char *cmd1[pipe_index + 1];
      for (int i = 0; i < pipe_index; i++)
      {
        cmd1[i] = argv[i];
      }
      cmd1[pipe_index] = NULL;
      char *cmd2[argc - pipe_index];
      for (int i = pipe_index + 1; i < argc; i++)
      {
        cmd2[i - pipe_index - 1] = argv[i];
      }
      cmd2[argc - pipe_index - 1] = NULL;

      // Create the first child process
      pid_t pid1 = fork();
      
      if (pid1 == 0)
      {
        // This is the first child process, redirect stdout to the write end of the pipe
        close(pipefd[0]);               // close the read end of the pipe
        dup2(pipefd[1], STDOUT_FILENO); // make stdout go to the write end of the pipe
        execvp(cmd1[0], cmd1);
        // If execvp returns, it means there was an error executing the command
        perror("Error executing command");
        exit(1);
      }
      else if (pid1 > 0)
      {
        // Create the second child process
        pid_t pid2 = fork();
        if (pid2 == 0)
        {
          // This is the second child process, redirect stdin to the read end of the pipe
          close(pipefd[1]);              // close the write end of the pipe
          dup2(pipefd[0], STDIN_FILENO); // make stdin come from the read end of the pipe
          execvp(cmd2[0], cmd2);
          // If execvp returns, it means there was an error executing the command
          perror("Error executing command");
          exit(1);
        }
        else if (pid2 > 0)
        {
          // This is the parent process, close both ends of the pipe and wait for the child processes to complete
          close(pipefd[0]);
          close(pipefd[1]);
          waitpid(pid1, NULL, 0);
          waitpid(pid2, NULL, 0);
        }
        else
        {
          // Error creating the second child process
          perror("Error creating child process");
        }
      }
      else
      {
        // Error creating the first child process
        perror("Error creating child process");
      }
    }
    else if (redir_in_index >= 0 || redir_out_index >= 0)
    {
      // The command uses redirection, create a child process to execute the command
      pid_t pid = fork();
      if (pid == 0)
      {
        // This is the child process, redirect stdin and/or stdout if necessary
        if (redir_in_index >= 0)
        {
          // Redirect stdin to the specified file
          int fd = open(argv[redir_in_index + 1], O_RDONLY);
          if (fd < 0)
          {
            perror("Error opening input file");
            exit(1);
          }
          dup2(fd, STDIN_FILENO);
          close(fd);
          // Remove the redirection operators and the input file from the argument list
          for (int i = redir_in_index; i < argc - 2; i++)
          {
            argv[i] = argv[i + 2];
          }
          argv[argc - 2] = NULL;
          argc -= 2;
        }
        if (redir_out_index >= 0)
        {
          // Redirect stdout to the specified file
          int fd = open(argv[redir_out_index + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
          if (fd < 0)
          {
            perror("Error opening output file");
            exit(1);
          }
          dup2(fd, STDOUT_FILENO);
          close(fd);
          // Remove the redirection operators and the output file from the argument list
          for (int i = redir_out_index; i < argc - 2; i++)
          {
            argv[i] = argv[i + 2];
          }
          argv[argc - 2] = NULL;
          argc -= 2;
        }
        execvp(argv[0], argv);
        // If execvp returns, it means there was an error executing the command
        perror("Error executing command");
        exit(1);
      }
      else if (pid > 0)
      {
        // This is the parent process, wait for the child to complete (unless it's a background process)
        if (!background)
        {
          waitpid(pid, NULL, 0);
        }
      }
      else
      {
        // Error creating the child process
        perror("Error creating child process");
      }
    }
    else
    {
      // The command does not use pipes or redirection, create a child process to execute it
      pid_t pid = fork();
      if (pid == 0)
      {
        // This is the child process, execute the command
        execvp(argv[0], argv);
        // If execvp returns, it means there was an error executing the command
        perror("Error executing command");
        exit(1);
      }
      else if (pid > 0)
      {
        // This is the parent process, wait for the child to complete (unless it's a background process)
        if (!background)
        {
          waitpid(pid, NULL, 0);
        }
      }
      else
      {
        // Error creating the child process
        perror("Error creating child process");
      }
    }
  }

  return 0;
}