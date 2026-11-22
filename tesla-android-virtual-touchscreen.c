#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ws.h>

#define TOUCH_DEVICE "/dev/virtual_touchscreen"

/* Longest command payload a client may send; the driver parses at most 64 bytes
 * per write(), so anything far beyond that is garbage worth discarding. */
#define TOUCH_MAX_MESSAGE 4096

/* The driver serves a single open (open() answers -EBUSY otherwise) and libws
 * runs one thread per client, so opens and writes must be serialized. */
static pthread_mutex_t g_dev_lock = PTHREAD_MUTEX_INITIALIZER;

static void send_commands(const unsigned char *msg, uint64_t size) {
  size_t left = (size > TOUCH_MAX_MESSAGE) ? (size_t) TOUCH_MAX_MESSAGE : (size_t) size;

  if (size > TOUCH_MAX_MESSAGE) {
    fprintf(stderr, "%s: dropping %" PRIu64 " bytes of an oversized message (%d)\n",
            TOUCH_DEVICE, size - TOUCH_MAX_MESSAGE, TOUCH_MAX_MESSAGE);
  }

  int fd = open(TOUCH_DEVICE, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    /* Expected while the module is unloaded or not yet loaded. */
    fprintf(stderr, "open(%s) failed: %s\n", TOUCH_DEVICE, strerror(errno));
    return;
  }

  size_t off = 0;
  while (off < left) {
    ssize_t written = write(fd, msg + off, left - off);

    if (written > 0) {
      off += (size_t) written;
      /* The driver only consumes commands terminated by '\n' that fit its
       * buffer, so a short write means the tail has to be offered again. */
      continue;
    }

    if (written < 0 && errno == EINTR) continue;

    if (written < 0) {
      fprintf(stderr, "write(%s) failed after %zu of %zu bytes: %s\n",
              TOUCH_DEVICE, off, left, strerror(errno));
    } else {
      fprintf(stderr, "write(%s) made no progress at %zu of %zu bytes\n",
              TOUCH_DEVICE, off, left);
    }
    break;
  }

  close(fd);
}

void webSocketOnConnectionOpened(ws_cli_conn_t *client) {
  char *cli;
  cli = ws_getaddress(client);
  printf("Connection opened, addr: %s\n", cli);
}

void webSocketOnConnectionClosed(ws_cli_conn_t *client) {
  char *cli;
  cli = ws_getaddress(client);
  printf("Connection closed, addr: %s\n", cli);
}

void webSocketOnMessage(__attribute__ ((unused)) ws_cli_conn_t *client,
       const unsigned char *msg, uint64_t size, __attribute__ ((unused)) int type) {
  if (msg == NULL || size == 0) return;

  pthread_mutex_lock(&g_dev_lock);
  send_commands(msg, size);
  pthread_mutex_unlock(&g_dev_lock);
}

void *pingThread(__attribute__ ((unused)) void *arg) {
  while(1) {
    sleep(1);
    ws_ping(NULL, 5);
  }
  return NULL;
}

int main(void)
{
  pthread_t ping_thread;
  pthread_create(&ping_thread, NULL, pingThread, NULL);

  struct ws_events evs;
  evs.onopen    = &webSocketOnConnectionOpened;
  evs.onclose   = &webSocketOnConnectionClosed;
  evs.onmessage = &webSocketOnMessage;
  ws_socket(&evs, 9999, 0, 1000);

  pthread_join(ping_thread, NULL);

  return (0);
}
