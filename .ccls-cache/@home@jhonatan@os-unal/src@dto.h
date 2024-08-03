#include <semaphore.h>
#include <stdlib.h>

#define BUFFER_CAPACITY 2048

typedef struct ipcdto_t {
  char buffer[BUFFER_CAPACITY];
} ipcdto_t;
