#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

typedef struct { int ip_addr; unsigned int port; } NTP_Remote_Address;
typedef struct { struct timespec ts; } NTP_Local_Timestamp;
typedef int NTP_Leap;
static const char *UTI_IPToString(int *ip) { (void)ip; return "192.0.2.1"; }
static void REF_GetReferenceParams(struct timespec *t, int *s, NTP_Leap *l,
    int *stratum, uint32_t *id, struct timespec *rt, double *d, double *disp)
{
  (void)t; (void)l; (void)id; (void)rt; (void)d; (void)disp;
  *s = 1; *stratum = 2;
}
#include "../request_log.h"

int main(void)
{
  int fds[2], i, n;
  char buffer[8192], *field;
  NTP_Remote_Address remote = {0, 45678};
  NTP_Local_Timestamp rx = {{1700000000, 123456789}};
  assert(chrony_log_synchronized(&rx));
  alarm(5);
  assert(pipe(fds) == 0);
  assert(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
  close(fds[1]);
  assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  for (i = 0; i < 1000; i++)
    chrony_log_request(&remote, &rx, 48, "response_sent");
  while (read(fds[0], buffer, sizeof(buffer)) > 0) {}
  chrony_log_request(&remote, &rx, 48, "response_sent");
  n = read(fds[0], buffer, sizeof(buffer) - 1);
  assert(n > 0);
  buffer[n] = '\0';
  assert(strstr(buffer, "\"sequence\":1001"));
  field = strstr(buffer, "\"log_dropped_total\":");
  assert(field && strtoull(field + strlen("\"log_dropped_total\":"), NULL, 10) > 0);
  close(fds[0]);
  chrony_log_request(&remote, &rx, 48, "response_sent");
  fprintf(stderr, "PASS: full pipe, recovery counter, broken pipe\n");
  return 0;
}
