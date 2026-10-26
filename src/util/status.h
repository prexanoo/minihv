#ifndef UTIL_STATUS_H
#define UTIL_STATUS_H

/* Codigos de retorno usados por todos os modulos. Somente main chama exit. */
typedef enum HvStatus {
    HV_OK = 0,
    HV_ERR_NOMEM,
    HV_ERR_PARSE,
    HV_ERR_IO,
    HV_ERR_INVALID,
    HV_ERR_NOSPACE,
    HV_ERR_PROTECTION,
    HV_ERR_NOTFOUND
} HvStatus;

#endif
