#include <stdlib.h>
#include <string.h>

/* Fixed UTC offset, independent of the host timezone and NTP clock. */
static int chrony_log_offset(int *seconds, char suffix[7])
{
    const char *value = getenv("LOG_TIME_OFFSET");
    int i, hours, minutes;
    *seconds = 0;
    strcpy(suffix, "Z");
    if (!value || !*value)
        return 1;
    if (strlen(value) != 5 || (value[0] != '+' && value[0] != '-'))
        return 0;
    for (i = 1; i < 5; i++)
        if (value[i] < '0' || value[i] > '9')
            return 0;
    hours = (value[1] - '0') * 10 + value[2] - '0';
    minutes = (value[3] - '0') * 10 + value[4] - '0';
    if (hours > 14 || minutes > 59 || (hours == 14 && minutes != 0))
        return 0;
    *seconds = (hours * 60 + minutes) * 60 * (value[0] == '-' ? -1 : 1);
    if (*seconds) {
        suffix[0] = value[0];
        suffix[1] = value[1];
        suffix[2] = value[2];
        suffix[3] = ':';
        suffix[4] = value[3];
        suffix[5] = value[4];
        suffix[6] = '\0';
    }
    return 1;
}
