#include "log/log.h"

#include <stdlib.h>
#include <string.h>

struct Logger {
    FILE *fp;
    LogFormat fmt;
    LogLevel min;
};

static const char *const LEVEL_NAMES[] = {"DEBUG", "INFO", "WARN", "ERROR"};
static const char *const FORMAT_NAMES[] = {"TXT", "CSV", "JSONL"};
static const char *const DEFAULT_FILES[] = {"out/minihv.log", "out/minihv.csv",
                                            "out/minihv.jsonl"};

#define N_LEVELS 4
#define N_FORMATS 3
#define LOG_BUF_SIZE (64 * 1024)

Logger *log_create(void)
{
    Logger *lg = calloc(1, sizeof *lg);
    if (!lg)
        return NULL;
    lg->fmt = LOG_FMT_JSONL;
    lg->min = LOG_INFO;
    return lg;
}

void log_destroy(Logger *lg)
{
    if (!lg)
        return;
    if (lg->fp)
        fclose(lg->fp);
    free(lg);
}

HvStatus log_open(Logger *lg, LogFormat fmt, LogLevel min, const char *path)
{
    if (lg->fp)
        fclose(lg->fp);
    lg->fmt = fmt;
    lg->min = min;
    lg->fp = fopen(path, "w");
    if (!lg->fp) {
        fprintf(stderr, "erro: nao consegui criar o log '%s' (a pasta existe?)\n", path);
        return HV_ERR_IO;
    }
    /* buffer maior: o log tem dezenas de milhares de linhas (RNF-05) */
    setvbuf(lg->fp, NULL, _IOFBF, LOG_BUF_SIZE);
    if (fmt == LOG_FMT_CSV)
        fputs("tick,level,module,vm,event,detail\n", lg->fp);
    return HV_OK;
}

/* RFC 4180: campo com virgula, aspas ou quebra de linha vai entre aspas */
static void csv_field(FILE *fp, const char *s)
{
    if (!strpbrk(s, ",\"\n\r")) {
        fputs(s, fp);
        return;
    }
    fputc('"', fp);
    for (; *s; s++) {
        if (*s == '"')
            fputc('"', fp);
        fputc(*s, fp);
    }
    fputc('"', fp);
}

static void json_string(FILE *fp, const char *s)
{
    fputc('"', fp);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
            fprintf(fp, "\\%c", c);
        else if (c == '\n')
            fputs("\\n", fp);
        else if (c < 0x20)
            fprintf(fp, "\\u%04x", c);
        else
            fputc(c, fp);
    }
    fputc('"', fp);
}

static void write_csv(FILE *fp, unsigned long long t, const char *lv, const char *mod,
                      const char *vm, const char *ev, const char *det)
{
    fprintf(fp, "%llu,%s,", t, lv);
    csv_field(fp, mod);
    fputc(',', fp);
    csv_field(fp, vm);
    fputc(',', fp);
    csv_field(fp, ev);
    fputc(',', fp);
    csv_field(fp, det);
    fputc('\n', fp);
}

static void write_jsonl(FILE *fp, unsigned long long t, const char *lv, const char *mod,
                        const char *vm, const char *ev, const char *det)
{
    fprintf(fp, "{\"tick\":%llu,\"level\":\"%s\",\"module\":", t, lv);
    json_string(fp, mod);
    fputs(",\"vm\":", fp);
    json_string(fp, vm);
    fputs(",\"event\":", fp);
    json_string(fp, ev);
    fputs(",\"detail\":", fp);
    json_string(fp, det);
    fputs("}\n", fp);
}

void log_write(Logger *lg, uint64_t tick, LogLevel lv, const char *module,
               const char *vm, const char *event, const char *detail)
{
    if (!lg || !lg->fp || lv < lg->min)
        return;
    unsigned long long t = (unsigned long long)tick;
    const char *lname = LEVEL_NAMES[lv];
    if (!vm)
        vm = "-";
    if (!detail)
        detail = "";
    switch (lg->fmt) {
    case LOG_FMT_TXT:
        fprintf(lg->fp, "[t=%04llu] %-5s %-6s %-8s %-12s %s\n", t, lname, module, vm,
                event, detail);
        break;
    case LOG_FMT_CSV:
        write_csv(lg->fp, t, lname, module, vm, event, detail);
        break;
    case LOG_FMT_JSONL:
        write_jsonl(lg->fp, t, lname, module, vm, event, detail);
        break;
    }
}

bool log_level_parse(const char *s, LogLevel *out)
{
    for (int i = 0; i < N_LEVELS; i++) {
        if (strcmp(s, LEVEL_NAMES[i]) == 0) {
            *out = (LogLevel)i;
            return true;
        }
    }
    return false;
}

bool log_format_parse(const char *s, LogFormat *out)
{
    for (int i = 0; i < N_FORMATS; i++) {
        if (strcmp(s, FORMAT_NAMES[i]) == 0) {
            *out = (LogFormat)i;
            return true;
        }
    }
    return false;
}

const char *log_level_name(LogLevel lv)
{
    return LEVEL_NAMES[lv];
}

const char *log_format_name(LogFormat fmt)
{
    return FORMAT_NAMES[fmt];
}

const char *log_default_file(LogFormat fmt)
{
    return DEFAULT_FILES[fmt];
}
