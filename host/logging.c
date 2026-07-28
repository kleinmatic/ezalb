/* host/logging.rs — sink installers for common.h log_emit: bare stderr
 * (headless/graphics) or timestamped $TMPDIR/ezalb-vt.log (text --log). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "host/host.h"

static FILE *log_file;

static void stdio_sink(log_level lvl, const char *msg)
{
    (void)lvl;
    fprintf(stderr, "%s\n", msg);
}

static const char *level_name(log_level lvl)
{
    switch (lvl) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN";
    case LOG_INFO:  return "INFO";
    case LOG_DEBUG: return "DEBUG";
    default:        return "TRACE";
    }
}

static void file_sink(log_level lvl, const char *msg)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    gmtime_r(&ts.tv_sec, &tm);
    fprintf(log_file, "%04d-%02d-%02dT%02d:%02d:%02d.%06ldZ %5s %s\n",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, (long)(ts.tv_nsec / 1000),
            level_name(lvl), msg);
}

void logging_setup_stdio(log_level level)
{
    g_log_level = level;
    log_set_sink(stdio_sink);
}

void logging_setup_file(log_level level)
{
    const char *tmp = getenv("TMPDIR");
    char path[1024];

    if (!tmp || !*tmp)
        tmp = "/tmp";
    snprintf(path, sizeof path, "%s/ezalb-vt.log", tmp);
    log_file = fopen(path, "w");
    if (!log_file) {
        fprintf(stderr, "Failed to create log file %s\n", path);
        return;
    }
    setvbuf(log_file, NULL, _IOLBF, 0);
    g_log_level = level;
    log_set_sink(file_sink);
}
