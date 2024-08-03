#include "dto.h"
#include <fcntl.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// bitmasks
#define BASE_FLAG_MASK 0x01
#define HELP_FLAG_MASK (BASE_FLAG_MASK << 1)
#define CUSTOM_DAEMON_FLAG_MASK (BASE_FLAG_MASK << 2)

// log severities
#define DEBUG_LOGSEVERITY 0
#define INFO_LOGSEVERITY 1
#define ERROR_LOGSEVERITY 2

// defaults
#define LOGSEVERITY_DEFAULT 3
#define DAEMON_PREFIX_DEFAULT "p3"

// technology-specific defaults
#define IPC_SHARED_MEMORY_PROYECT_ID 'a'
#define IPC_SHARED_MEMORY_SIZE 10

#define SEMAPHORE_IN_PATH "/semaphore_in"
#define SEMAPHORE_OUT_PATH "/semaphore_out"

// application state and arguments
typedef struct Arguments {
  int flags;
  char *executeCmd;
  char *daemonKeyPrefix;
  char *daemonSharedMemoryKey;
} Arguments;

typedef struct ApplicationState {
  int minimumLogSeverity;
  int binaryFileDescriptor;
  int pipeFileDescriptors[2];
  int sharedMemoryFileDescriptor;
  sem_t *inputSemaphore;
  sem_t *outputSemaphore;
  struct ipcdto_t *sharedMemory;
  Arguments arguments;
} ApplicationState;

// Global state to enable cleanup from any place of the codebase, also helps to
// limit the log severity
// TODO: this should be inyected through some IoC system
ApplicationState applicationState;

// utility functions
void formattedLog(char *origin, int severity, char *message) {
  if (severity >= applicationState.minimumLogSeverity) {
    printf("[%s:%d] %s\n", origin, severity, message);
  }
}

void formattedLogParametized(char *origin, int severity, char *format,
                             uintptr_t parameter) {
  static char processedStr[64];
  snprintf(processedStr, 64, format, parameter);
  formattedLog(origin, severity, processedStr);
}

void printHelp() {
  printf("Usage: p1 [POSIX or SHORT options] /full/path/to/file\n"
         "SHORT options\n"
         "\t--help, -h\tGet this help dialog\n"
         "POSIX options\n"
         "\t--log=<logseverity>, -l=<logseverity>\tSet custom minimum log "
         "severity (0 = DEBUG, 1 = INFO, 2 = ERROR)\n"
         "\t--key=<key>, -k=<key>\t\t\tSet custom daemon shared memory and "
         "semaphore key prefix\n");
}

// i'd like to replace this cleanup function with
// the __cleanup__ attribute on GCC but no time to refactor
void cleanup() {
  if (applicationState.arguments.daemonSharedMemoryKey != NULL) {
    free(applicationState.arguments.daemonSharedMemoryKey);
    applicationState.arguments.daemonSharedMemoryKey = NULL;
  }
  if (applicationState.arguments.daemonKeyPrefix != NULL) {
    free(applicationState.arguments.daemonKeyPrefix);
    applicationState.arguments.daemonKeyPrefix = NULL;
  }
  if (applicationState.pipeFileDescriptors[0] != 0) {
    close(applicationState.pipeFileDescriptors[0]);
    applicationState.pipeFileDescriptors[0] = 0;
  }
  if (applicationState.pipeFileDescriptors[1] != 0) {
    close(applicationState.pipeFileDescriptors[1]);
    applicationState.pipeFileDescriptors[1] = 0;
  }
  if (applicationState.binaryFileDescriptor != 0) {
    close(applicationState.binaryFileDescriptor);
    applicationState.binaryFileDescriptor = 0;
  }
  if (applicationState.sharedMemoryFileDescriptor != 0) {
    close(applicationState.sharedMemoryFileDescriptor);
    applicationState.sharedMemoryFileDescriptor = 0;
  }
  if (applicationState.inputSemaphore != 0) {
    sem_close(applicationState.inputSemaphore);
  }
  if (applicationState.outputSemaphore != 0) {
    sem_close(applicationState.outputSemaphore);
  }
  if (applicationState.sharedMemory != NULL) {
    munmap(applicationState.sharedMemory, sizeof(ipcdto_t));
  }
}

void handleErrorGracefullyAndExit(char *origin, char *errorMessage) {
  formattedLog(origin, 2, errorMessage);
  cleanup();
  exit(EXIT_FAILURE);
}
// end of utility functions

// argument defaults and validation
Arguments validateArgv(Arguments args) {
  if (strlen(args.executeCmd) == 0) {
    args.flags |= HELP_FLAG_MASK;
  }
  if ((args.flags & CUSTOM_DAEMON_FLAG_MASK) == 0)
    memcpy(args.daemonKeyPrefix, DAEMON_PREFIX_DEFAULT,
           strlen(DAEMON_PREFIX_DEFAULT) + sizeof(char));
  memcpy(args.daemonSharedMemoryKey, args.daemonKeyPrefix,
         strlen(args.daemonKeyPrefix) + sizeof(char));
  strcat(args.daemonSharedMemoryKey, "sharedmemory");
  return args;
}

// argument struct creation and definition of development environment (log
// level)
// @param argc argc from main
// @param argv argv from main
// @returns arguments struct with all relevant data from argv
Arguments extractArgumentsFromArgv(int argc, char **argv) {
  Arguments args;
  args.flags = 0;
  args.executeCmd = "";
  args.daemonSharedMemoryKey = malloc(sizeof(char[64]));
  args.daemonKeyPrefix = malloc(sizeof(char[64]));

  if (args.daemonSharedMemoryKey == NULL || args.daemonKeyPrefix == NULL) {
    handleErrorGracefullyAndExit("extractArgumentsFromArgv",
                                 "Could not allocate args variables");
  }

  for (int idx = 1; idx < argc; idx += 1) {
		int isHelpFlag = strcmp(argv[idx], "-h") == 0 || strcmp(argv[idx], "--help") == 0;

		int isLogFlagAlias = strncmp(argv[idx], "-l=", strlen("-l=")) == 0;
		int isLogFlagFull = strncmp(argv[idx], "--log=", strlen("--log=")) == 0;

		int isKeyFlagFull = strncmp(argv[idx], "--key=", strlen("--key=")) == 0;
		int isKeyFlagAlias = strncmp(argv[idx], "-k=", strlen("-k=")) == 0;

    if (isHelpFlag) {
      args.flags |= HELP_FLAG_MASK;
    } else if (isLogFlagAlias) {
      char value[64];
			int baseFlagLength = strlen("-l=");
			int argumentLength = strlen(argv[idx]);
      memcpy(value, &argv[idx][baseFlagLength], argumentLength - baseFlagLength);
      applicationState.minimumLogSeverity = atoi(value);
    } else if (isLogFlagFull) {
      char value[64];
			int baseFlagLength = strlen("--log=");
			int argumentLength = strlen(argv[idx]);
      memcpy(value, &argv[idx][baseFlagLength], argumentLength - baseFlagLength);
      applicationState.minimumLogSeverity = atoi(value);
    } else if (isKeyFlagAlias) {
			int baseFlagLength = strlen("-k=");
			int argumentLength = strlen(argv[idx]);
      memcpy(args.daemonKeyPrefix, &argv[idx][baseFlagLength], argumentLength - baseFlagLength + sizeof(char));
      args.flags |= CUSTOM_DAEMON_FLAG_MASK;
    } else if (isKeyFlagFull) {
			int baseFlagLength = strlen("--key=");
			int argumentLength = strlen(argv[idx]);
      memcpy(args.daemonKeyPrefix, &argv[idx][baseFlagLength], argumentLength - baseFlagLength + sizeof(char));
      args.flags |= CUSTOM_DAEMON_FLAG_MASK;
    } else {
      args.executeCmd = argv[idx];
    }
  }

  return validateArgv(args);
}

// @param pid pid of the process to wait for
// @returns exit status of the process
int waitForChildProcess(int pid) {
  formattedLogParametized("waitForChildProcess", DEBUG_LOGSEVERITY,
                          "Waiting for process with pid: %d", pid);

  int waitStatus;
  pid_t waitHandle;

  do {
    waitHandle = waitpid(pid, &waitStatus, WCONTINUED | WUNTRACED);
    if (waitHandle == -1) {
      handleErrorGracefullyAndExit("waitForChildProcess",
                                   "Could not wait child process");
    }
    if (WIFEXITED(waitStatus)) {
      break;
    }
  } while (!WIFEXITED(waitStatus) || !WIFCONTINUED(waitStatus));

  formattedLogParametized("waitForChildProcess", INFO_LOGSEVERITY,
                          "Child process exited with status code: %d",
                          WEXITSTATUS(waitStatus));

  return WEXITSTATUS(waitStatus);
}

// validates that the binary specified by binaryPath exists
// if not then handles the error gracefully and exits
void validateBinaryPath(char *binaryPath) {
  formattedLogParametized("validateBinaryPath", DEBUG_LOGSEVERITY,
                          "Executing with binary path: %s",
                          (uintptr_t)binaryPath);

  applicationState.binaryFileDescriptor = open(binaryPath, O_RDONLY);

  if (applicationState.binaryFileDescriptor < 0) {
    char *responsePacket = "Binary path does not exist";
    int responseSizePacket = strlen(responsePacket);
    if (write(applicationState.pipeFileDescriptors[1], &responseSizePacket, sizeof(int)) == -1) {
			handleErrorGracefullyAndExit("validateBinaryPath",
																	 "Couldn't write to pipe");
		}
		if (write(applicationState.pipeFileDescriptors[1], responsePacket, responseSizePacket) == -1) {
			handleErrorGracefullyAndExit("validateBinaryPath",
																	 "Couldn't write to pipe");
		}
    close(applicationState.pipeFileDescriptors[1]);
    handleErrorGracefullyAndExit("validateBinaryPath",
                                 "Binary path does not exist");
  }
  close(applicationState.binaryFileDescriptor);
  applicationState.binaryFileDescriptor = 0;
}

// validates that the shared memory with the name specified by
// daemonSharedMemoryKey exists if not then handles the error gracefully and
// exits
void validateDaemonHealth(char *daemonSharedMemoryKey) {
  formattedLogParametized("validateBinaryPath", DEBUG_LOGSEVERITY,
                          "Executing with daemon shared key: %s",
                          (uintptr_t)daemonSharedMemoryKey);

  applicationState.sharedMemoryFileDescriptor =
      shm_open(applicationState.arguments.daemonSharedMemoryKey, O_RDWR, 0);
  if (applicationState.sharedMemoryFileDescriptor == -1) {
    char *responsePacket = "Daemon is not healthy";

    // accounting for null termination char
    int responseSizePacket = strlen(responsePacket) + sizeof(char);

    if (write(applicationState.pipeFileDescriptors[1], &responseSizePacket, sizeof(int)) == -1) {
			handleErrorGracefullyAndExit("validateDaemonHealth",
																	 "Couldn't write to pipe");
		}
		if (write(applicationState.pipeFileDescriptors[1], responsePacket, responseSizePacket)) {
			handleErrorGracefullyAndExit("validateDaemonHealth",
																	 "Couldn't write to pipe");
		}
    close(applicationState.pipeFileDescriptors[1]);
    handleErrorGracefullyAndExit("validateDaemonHealth",
                                 "Daemon is not healthy");
  }
}

// connects to the daemon shared memory segment
// passes through the contents of segment to stdout
// TODO: this should implement some resillience pattern
void connectDaemonAndStdoutPassthrough(char *daemonSharedMemoryKey) {
  applicationState.sharedMemoryFileDescriptor =
      shm_open(applicationState.arguments.daemonSharedMemoryKey, O_RDWR, 0);
  if (applicationState.sharedMemoryFileDescriptor == -1) {
    handleErrorGracefullyAndExit(
        "connectDaemonAndStdoutPassthrough",
        "Could not retrieve shared memory file descriptor");
  }

  if (ftruncate(applicationState.sharedMemoryFileDescriptor,
                sizeof(ipcdto_t)) == -1) {
    handleErrorGracefullyAndExit("connectDaemonAndStdoutPassthrough",
                                 "Could not truncate shared memory");
  }

  applicationState.sharedMemory =
      mmap(NULL, sizeof(ipcdto_t), PROT_READ | PROT_WRITE, MAP_SHARED,
           applicationState.sharedMemoryFileDescriptor, 0);

  if (applicationState.sharedMemory == MAP_FAILED) {
    handleErrorGracefullyAndExit("connectDaemonAndStdoutPassthrough",
                                 "Failed to map shared memory");
  }

  applicationState.inputSemaphore =
      sem_open(SEMAPHORE_IN_PATH, O_CREAT, 0644, 0);
  applicationState.outputSemaphore =
      sem_open(SEMAPHORE_OUT_PATH, O_CREAT, 0644, 0);

  strcpy(applicationState.sharedMemory->buffer,
         applicationState.arguments.executeCmd);

  if (sem_post(applicationState.inputSemaphore) == -1) {
    handleErrorGracefullyAndExit("connectDaemonAndStdoutPassthrough",
                                 "Could not post on input semaphore");
  }

  if (!sem_wait(applicationState.outputSemaphore)) {
    if (write(applicationState.pipeFileDescriptors[1],
          applicationState.sharedMemory->buffer,
          strlen(applicationState.sharedMemory->buffer)) == -1) {
			handleErrorGracefullyAndExit("connectDaemonAndStdoutPassthrough",
																	 "Couldn't write to pipe");
		}
  } else {
    handleErrorGracefullyAndExit("connectDaemonAndStdoutPassthrough",
                                 "Could not wait on output semaphore");
  }
}

// executes the child process with the specified arguments and
// communicates with it using an anonymous pipe
// then forwards the pipe contents to stdout
void execute(Arguments args) {
  if (pipe(applicationState.pipeFileDescriptors) == -1) {
    handleErrorGracefullyAndExit("createChildProcess",
                                 "Could not create anonymous pipe");
  }

  int childPid = fork();

  if (childPid == 0) {
    close(applicationState.pipeFileDescriptors[0]);

    validateBinaryPath(args.executeCmd);

    formattedLog("execute@p2", INFO_LOGSEVERITY,
                 "Binary path was validated sucessfully");

    validateDaemonHealth(args.daemonSharedMemoryKey);

    formattedLog("execute@p2", INFO_LOGSEVERITY,
                 "Daemon health was validated sucessfully");

    connectDaemonAndStdoutPassthrough(args.daemonSharedMemoryKey);
    cleanup();

    exit(EXIT_SUCCESS);
  } else {
    close(applicationState.pipeFileDescriptors[1]);

    formattedLog("execute@p1", INFO_LOGSEVERITY,
                 "Created child process with binary path");
    formattedLog("execute@p1", DEBUG_LOGSEVERITY,
                 "Waiting for child process to exit...");

    int childProcessStatusCode = waitForChildProcess(childPid);

    char response[BUFFER_CAPACITY];
    if (read(applicationState.pipeFileDescriptors[0], response, O_NONBLOCK) == -1) {
			handleErrorGracefullyAndExit("execute@p1", "Could't read from pipe");
		}

    if (childProcessStatusCode == 0) {
      formattedLogParametized("execute@p1", INFO_LOGSEVERITY,
                              "Retrieved response from child: %s",
                              (uintptr_t)response);

      printf("Pipe response: %s\n", response);
    } else {
      handleErrorGracefullyAndExit("execute@p1",
                                   "Could not wait for child process");
    }
  }
}

int main(int argc, char **argv) {
  applicationState.minimumLogSeverity = LOGSEVERITY_DEFAULT;
  applicationState.arguments = extractArgumentsFromArgv(argc, argv);

  formattedLogParametized("main", DEBUG_LOGSEVERITY, "Executing with flags: %d",
                          applicationState.arguments.flags);
  formattedLogParametized("main", DEBUG_LOGSEVERITY,
                          "Executing with minimum log severity: %d",
                          applicationState.minimumLogSeverity);

  if ((applicationState.arguments.flags & CUSTOM_DAEMON_FLAG_MASK) > 0) {
    formattedLogParametized(
        "main", DEBUG_LOGSEVERITY,
        "Executing with custom daemon key prefix: %s",
        (uintptr_t)applicationState.arguments.daemonKeyPrefix);
  }

  if ((applicationState.arguments.flags & HELP_FLAG_MASK) > 0) {
    printHelp();
  } else {
    execute(applicationState.arguments);
    cleanup();
  }

  return EXIT_SUCCESS;
}
