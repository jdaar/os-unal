#include "dto.h"
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// log severities
#define DEBUG_LOGSEVERITY 0
#define INFO_LOGSEVERITY 1
#define ERROR_LOGSEVERITY 2

// defaults
#define SHARED_MEMORY_KEY_DEFAULT "p3sharedmemory"
#define LOGSEVERITY_DEFAULT 0

// technology-specific defaults
#define IPC_SHARED_MEMORY_PROYECT_ID 'a'
#define IPC_SHARED_MEMORY_SIZE 2048

#define SEMAPHORE_IN_PATH "/semaphore_in"
#define SEMAPHORE_OUT_PATH "/semaphore_out"

typedef struct ApplicationState {
  int minimumLogSeverity;
  sem_t *inputSemaphore;
  sem_t *outputSemaphore;
  struct ipcdto_t *sharedMemory;
  int sharedMemoryFileDescriptor;
} ApplicationState;

// Global state to enable cleanup from any place of the codebase, also helps to
// limit the log severity
// TODO: this should be inyected through some IoC system
ApplicationState applicationState;

void cleanup() {
  sem_close(applicationState.inputSemaphore);
  sem_close(applicationState.outputSemaphore);

  sem_unlink(SEMAPHORE_IN_PATH);
  sem_unlink(SEMAPHORE_OUT_PATH);

  munmap(applicationState.sharedMemory, sizeof(ipcdto_t));

  close(applicationState.sharedMemoryFileDescriptor);
  shm_unlink(SHARED_MEMORY_KEY_DEFAULT);
}

void formattedLog(char *origin, int severity, char *message) {
  if (severity >= applicationState.minimumLogSeverity) {
    printf("[%s:%d] %s\n", origin, severity, message);
  }
}

void handleErrorGracefullyAndExit(char *origin, char *errorMessage) {
  formattedLog(origin, 2, errorMessage);
  cleanup();
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  applicationState.minimumLogSeverity = LOGSEVERITY_DEFAULT;
  applicationState.sharedMemoryFileDescriptor =
      shm_open(SHARED_MEMORY_KEY_DEFAULT, O_CREAT | O_RDWR, 0666);
  if (applicationState.sharedMemoryFileDescriptor == -1) {
    handleErrorGracefullyAndExit(
        "main", "Could not retrieve shared memory file descriptor");
  }

  if (ftruncate(applicationState.sharedMemoryFileDescriptor,
                sizeof(ipcdto_t)) == -1) {
    handleErrorGracefullyAndExit("main", "Could not truncate shared memory");
  }

  applicationState.sharedMemory =
      mmap(NULL, sizeof(ipcdto_t), PROT_READ | PROT_WRITE, MAP_SHARED,
           applicationState.sharedMemoryFileDescriptor, 0);
  if (applicationState.sharedMemory == MAP_FAILED) {
    handleErrorGracefullyAndExit("main", "Could not map shared memory");
  }

  applicationState.inputSemaphore =
      sem_open(SEMAPHORE_IN_PATH, O_CREAT, 0644, 0);
  applicationState.outputSemaphore =
      sem_open(SEMAPHORE_OUT_PATH, O_CREAT, 0644, 0);

  if (applicationState.inputSemaphore == (void *)-1) {
    handleErrorGracefullyAndExit("main", "Could not instantiate semaphore");
    exit(1);
  }
  if (applicationState.outputSemaphore == (void *)-1) {
    handleErrorGracefullyAndExit("main", "Could not instantiate semaphore");
    exit(1);
  }

  do {
    if (!sem_wait(applicationState.inputSemaphore)) {
      int pipeFileDescriptors[2];

      if (pipe(pipeFileDescriptors) == -1) {
        handleErrorGracefullyAndExit("main", "Could not create pipe");
      }

      int childPid = fork();

      if (childPid == 0) {
        close(pipeFileDescriptors[0]);
        if (dup2(pipeFileDescriptors[1], 1) == -1) {
          handleErrorGracefullyAndExit("main",
                                       "Could not forward stdout to pipe");
        }
        char *args[] = {applicationState.sharedMemory->buffer, NULL};
        execv(applicationState.sharedMemory->buffer, args);
        close(pipeFileDescriptors[1]);
      } else if (childPid < 0) {
        handleErrorGracefullyAndExit("main", "Could not fork process");
      } else {
        close(pipeFileDescriptors[1]);

        wait(NULL);
        if (read(pipeFileDescriptors[0], applicationState.sharedMemory->buffer,
                 O_NONBLOCK) == -1) {
          handleErrorGracefullyAndExit("main", "Could not read from pipe");
        }

        if (sem_post(applicationState.outputSemaphore) == -1) {
          handleErrorGracefullyAndExit("main",
                                       "Could not post on output semaphore");
        }

        close(pipeFileDescriptors[0]);
      }
    } else {
      handleErrorGracefullyAndExit("main", "Could wait for input semaphore");
      exit(1);
    }
  } while (1);

  cleanup();

  return EXIT_SUCCESS;
}
