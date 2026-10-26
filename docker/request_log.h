#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log_offset.h"

static int chrony_log_synchronized(NTP_Local_Timestamp *rx)
{
  int synced, stratum;
  NTP_Leap leap;
  uint32_t reference_id;
  struct timespec reference_time;
  double delay, dispersion;
  REF_GetReferenceParams(&rx->ts, &synced, &leap, &stratum,
                        &reference_id, &reference_time, &delay, &dispersion);
  return synced;
}

/* Container stdout is a nonblocking pipe. Never wait for the log collector. */
static void
chrony_log_request(NTP_Remote_Address *remote, NTP_Local_Timestamp *rx,
                 int length, const char *result)
{
  static int ready;
  static int offset_seconds;
  static char offset_suffix[7];
  static unsigned long long sequence, dropped;
  char line[1024], date[32];
  struct stat st;
  struct tm utc;
  struct timespec reference_time;
  time_t display_time;
  int flags, size, synced, stratum;
  NTP_Leap leap;
  uint32_t reference_id;
  double delay, dispersion;

  sequence++;
  if (!ready) {
    if (!chrony_log_offset(&offset_seconds, offset_suffix)) {
      dropped++;
      return;
    }
    flags = fcntl(STDOUT_FILENO, F_GETFL);
    if (flags < 0 || fstat(STDOUT_FILENO, &st) < 0 ||
        !S_ISFIFO(st.st_mode) ||
        fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
      dropped++;
      return;
    }
    signal(SIGPIPE, SIG_IGN);
    ready = 1;
  }
  REF_GetReferenceParams(&rx->ts, &synced, &leap, &stratum,
                        &reference_id, &reference_time, &delay, &dispersion);
  display_time = rx->ts.tv_sec + offset_seconds;
  if (!gmtime_r(&display_time, &utc) ||
      !strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc)) {
    dropped++;
    return;
  }
  size = snprintf(line, sizeof(line),
    "{\"timestamp\":\"%s.%09ld%s\",\"service\":\"chrony\","
    "\"event\":\"ntp_request\",\"client_ip\":\"%s\",\"client_port\":%u,"
    "\"request_bytes\":%d,\"result\":\"%s\",\"server_stratum\":%d,"
    "\"synchronized\":%s,\"sequence\":%llu,\"log_dropped_total\":%llu}\n",
    date, rx->ts.tv_nsec, offset_suffix, UTI_IPToString(&remote->ip_addr),
    (unsigned int)remote->port, length, result, stratum,
    synced ? "true" : "false", sequence, dropped);
  /* One write below PIPE_BUF preserves record boundaries under backpressure. */
  if (size < 0 || size >= (int)sizeof(line) ||
      write(STDOUT_FILENO, line, size) != size)
    dropped++;
}
