#ifndef LOG_LOG_H
#define LOG_LOG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "util/status.h"

typedef enum LogLevel { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

typedef enum LogFormat { LOG_FMT_TXT, LOG_FMT_CSV, LOG_FMT_JSONL } LogFormat;

typedef struct Logger Logger;

/* Cria o logger sem arquivo. Ate log_open ser chamado nada e gravado. */
Logger *log_create(void);
void log_destroy(Logger *lg);

/* RF-LOG-01/02: abre o arquivo no formato escolhido, com nivel minimo. */
HvStatus log_open(Logger *lg, LogFormat fmt, LogLevel min, const char *path);

void log_write(Logger *lg, uint64_t tick, LogLevel lv, const char *module,
               const char *vm, const char *event, const char *detail);

bool log_level_parse(const char *s, LogLevel *out);
bool log_format_parse(const char *s, LogFormat *out);
const char *log_level_name(LogLevel lv);
const char *log_format_name(LogFormat fmt);
const char *log_default_file(LogFormat fmt);

#endif
