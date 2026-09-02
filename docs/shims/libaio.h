// Minimal Linux libaio stand-in for MrDocs parsing of folly/io/async/AsyncIO.h.
//
// Not real libaio. AsyncIO.h holds an `iocb` by value and an `io_context_t`,
// so those must be complete/declared; no members are accessed from headers.
#ifndef LIBAIO_SHIM_H_
#define LIBAIO_SHIM_H_

struct iocb {
    void* data;
    short aio_lio_opcode;
    int aio_fildes;
};

struct io_event {
    void* data;
    struct iocb* obj;
    long long res;
    long long res2;
};

typedef struct io_context* io_context_t;

#ifdef __cplusplus
extern "C" {
#endif
int io_submit(io_context_t ctx, long nr, struct iocb* ios[]);
int io_setup(int maxevents, io_context_t* ctxp);
int io_destroy(io_context_t ctx);
int io_getevents(io_context_t ctx, long min_nr, long nr, struct io_event* events, struct timespec* timeout);
#ifdef __cplusplus
}
#endif

#endif // LIBAIO_SHIM_H_
