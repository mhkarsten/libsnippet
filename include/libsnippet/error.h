#ifndef ERROR_H
#define ERROR_H

#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <papi.h>
#include <Zydis/Zydis.h>

typedef enum {
    EGENERIC = 1,
    ESNIPENCODE,
    ESNIPLOAD,
    ESNIPEXEC,
    ESNIPNOMEM,
    ESLIENT
} err_t;

static const char *error_strings[] = {
    [EGENERIC] = "Generic Error.",
    [ESNIPENCODE] = "Snippet Encoding Error.",
    [ESNIPLOAD] = "Snippet Loading Error.",
    [ESNIPEXEC] = "Snippet Execution Error.",
    [ESNIPNOMEM] = "No Memory available for the snippet",
    [ESLIENT]   = "",
};

#define PRINT_ERROR(val, err_fmt, err_str, fmt, ...) \
    fprintf(stderr, "[%s:%d] "err_fmt" %s "fmt"\n", __FILE__, __LINE__, val, err_str, ##__VA_ARGS__)

#define HANDLE_ERROR(f, type, errcode, err_cond, err_ret, err_fmt, err_str, fmt, ...) \
    do {                                                                \
        type _ret = (type) (f);                                         \
        if (err_cond) {                                                 \
            if (errcode != ESLIENT)                                     \
                PRINT_ERROR(_ret, err_fmt, err_str, fmt, ##__VA_ARGS__);\
            return err_ret;                                             \
        }                                                               \
    } while (0)

#define INT_ERR(f, errcode, err_ret, err_str, fmt, ...) HANDLE_ERROR(f, int, errcode, _ret < 0, err_ret, "%d", err_str, "")
#define PTR_ERR(f, errcode, err_ret, err_str, fmt, ...) HANDLE_ERROR(f, void *, errcode, _ret == NULL, err_ret, "%p", error_strings[errcode], "")

// Handle errors from other libraries
#define CHECK_LIBC(f)                       INT_ERR(f, 0, -errno, strerror(errno), "")
#define CHECK_MMAP(f)                       HANDLE_ERROR(f, void *, 0, _ret == MAP_FAILED, -errno, "%p", strerror(errno), "")
#define CHECK_PAPI(f)                       HANDLE_ERROR(f, int, 0, _ret != PAPI_OK, -abs(_ret), "%d", PAPI_strerror(_ret), "")
#define CHECK_ZYAN(f)                       HANDLE_ERROR(f, ZyanStatus, 0, ZYAN_FAILED(_ret), _ret, "%u",  FormatZyanStatus(_ret), "")

// Return ints as errors
#define CHECK_INT(f, errcode)               INT_ERR(f, errcode, -errcode, error_strings[errcode], "")
#define CHECK_PTR(f, errcode)               PTR_ERR(f, errcode, -errcode, error_strings[errcode], "")

// Return null ptrs as errors
#define CHECK_INT_NULL(f, errcode)          INT_ERR(f, errcode, NULL, error_strings[errcode], "")
#define CHECK_PTR_NULL(f, errcode)          PTR_ERR(f, errcode, NULL, error_strings[errcode], "")
#define CHECK_MMAP_NULL(f)                  HANDLE_ERROR(f, void *, 0, _ret == MAP_FAILED, NULL, "%p", strerror(errno), "")
#define CHECK_LIBC_NULL(f)                  INT_ERR(f, 0, NULL, strerror(errno), "")

#define CHECK_ZYAN_WMSG(f, fmt, ...)        HANDLE_ERROR(f, ZyanStatus, 0, ZYAN_FAILED(_ret), _ret, "%u", FormatZyanStatus(_ret), fmt, ##__VA_ARGS__)
#define CHECK_ZYAN_WERR(f, errcode)         HANDLE_ERROR(f, ZyanStatus, errcode, ZYAN_FAILED(_ret), _ret, "%u", FormatZyanStatus(_ret), "")

// This was taken from the Zydis code base, wasnt included with my install
static inline const char* FormatZyanStatus(ZyanStatus _status)
{
    const char* strings_zycore[] =
    {
        /* 00 */ "SUCCESS",
        /* 01 */ "FAILED",
        /* 02 */ "TRUE",
        /* 03 */ "FALSE",
        /* 04 */ "INVALID_ARGUMENT",
        /* 05 */ "INVALID_OPERATION",
        /* 06 */ "NOT_FOUND",
        /* 07 */ "OUT_OF_RANGE",
        /* 08 */ "INSUFFICIENT_BUFFER_SIZE",
        /* 09 */ "NOT_ENOUGH_MEMORY",
        /* 0A */ "NOT_ENOUGH_MEMORY",
        /* 0B */ "BAD_SYSTEMCALL"
    };
    const char* strings_zydis[] =
    {
        /* 00 */ "NO_MORE_DATA",
        /* 01 */ "DECODING_ERROR",
        /* 02 */ "INSTRUCTION_TOO_LONG",
        /* 03 */ "BAD_REGISTER",
        /* 04 */ "ILLEGAL_LOCK",
        /* 05 */ "ILLEGAL_LEGACY_PFX",
        /* 06 */ "ILLEGAL_REX",
        /* 07 */ "INVALID_MAP",
        /* 08 */ "MALFORMED_EVEX",
        /* 09 */ "MALFORMED_MVEX",
        /* 0A */ "INVALID_MASK",
        /* 0B */ "SKIP_TOKEN",
        /* 0C */ "IMPOSSIBLE_INSTRUCTION"
    };

    if (ZYAN_STATUS_MODULE(_status) == ZYAN_MODULE_ZYCORE)
    {
        _status = ZYAN_STATUS_CODE(_status);
        ZYAN_ASSERT(_status < ZYAN_ARRAY_LENGTH(strings_zycore));
        return strings_zycore[_status];
    }

    if (ZYAN_STATUS_MODULE(_status) == ZYAN_MODULE_ZYDIS)
    {
        _status = ZYAN_STATUS_CODE(_status);
        ZYAN_ASSERT(_status < ZYAN_ARRAY_LENGTH(strings_zydis));
        return strings_zydis[_status];
    }

    ZYAN_UNREACHABLE;
}

#endif
