#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log_offset.h"

static void fail(const char *message)
{
    fprintf(stderr, "chrony-log: %s\n", message);
    exit(EXIT_FAILURE);
}

static int valid_network(const char *token)
{
    char address[256], *slash, *end;
    unsigned char bytes[16];
    long prefix;
    int family;
    strcpy(address, token);
    slash = strchr(address, '/');
    if (slash)
        *slash++ = '\0';
    family = strchr(address, ':') ? AF_INET6 : AF_INET;
    if (inet_pton(family, address, bytes) != 1)
        return 0;
    if (!slash)
        return 1;
    if (!*slash || strspn(slash, "0123456789") != strlen(slash))
        return 0;
    errno = 0;
    prefix = strtol(slash, &end, 10);
    return !errno && !*end && prefix <= (family == AF_INET6 ? 128 : 32);
}

static void append_list(FILE *file, const char *variable, const char *fallback,
                        const char *directive, const char *options, int network)
{
    const char *value = getenv(variable), *start;
    char token[256];
    size_t length;
    if (!value)
        value = fallback;
    while (*value) {
        while (isspace((unsigned char)*value))
            value++;
        if (!*value)
            break;
        start = value;
        while (*value && !isspace((unsigned char)*value))
            value++;
        length = (size_t)(value - start);
        if (length >= sizeof(token))
            fail("Environment address is too long");
        memcpy(token, start, length);
        token[length] = '\0';
        if (network) {
            if (!valid_network(token))
                fail("Invalid ALLOW_CIDR address or prefix");
        } else if (token[0] == '-' || strspn(token,
                   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-") != length) {
            fail("Invalid NTP source address");
        }
        if (fprintf(file, "%s %s%s\n", directive, token, options) < 0)
            fail("Cannot write configuration");
    }
}

int main(int argc, char **argv)
{
    const char *custom = getenv("CHRONY_CONFIG");
    const char *config = custom && *custom ? custom : "/etc/chrony/chrony.conf";
    struct stat st;
    FILE *input, *output;
    char buffer[4096];
    size_t size;
    int i;
    int offset_seconds;
    char offset_suffix[7];
    char *args[argc + 7];

    if (!chrony_log_offset(&offset_seconds, offset_suffix))
        fail("LOG_TIME_OFFSET must be +HHMM or -HHMM, between -1400 and +1400");
    umask(0077);
    if (mkdir("/tmp/chrony", 0700) < 0 && errno != EEXIST)
        fail("Cannot create /tmp/chrony");
    if (stat(config, &st) < 0) {
        if (errno != ENOENT || (custom && *custom))
            fail("Cannot access configuration file");
        config = "/tmp/chrony/chrony.conf";
        input = fopen("/usr/share/chrony-log/chrony.conf", "r");
        output = fopen(config, "w");
        if (!input || !output)
            fail("Cannot generate configuration");
        while ((size = fread(buffer, 1, sizeof(buffer), input)) > 0)
            if (fwrite(buffer, 1, size, output) != size)
                fail("Cannot copy configuration");
        if (ferror(input) || fclose(input))
            fail("Cannot read configuration template");
        append_list(output, "NTP_POOLS", "pool.ntp.org", "pool", " iburst maxsources 4", 0);
        append_list(output, "NTP_SERVERS", "", "server", " iburst", 0);
        append_list(output, "ALLOW_CIDR", "", "allow", "", 1);
        if (fclose(output))
            fail("Cannot save configuration");
    } else if (!S_ISREG(st.st_mode)) {
        fail("Configuration path is not a regular file");
    }
    args[0] = "/usr/sbin/chronyd";
    args[1] = "-d";
    args[2] = "-x";
    args[3] = "-U";
    args[4] = "-f";
    args[5] = (char *)config;
    for (i = 1; i < argc; i++)
        args[i + 5] = argv[i];
    args[argc + 5] = NULL;
    execv(args[0], args);
    fail("Cannot execute chronyd");
    return EXIT_FAILURE;
}
