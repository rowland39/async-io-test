#ifndef _ASyncFileWriter_H
#define _ASyncFileWriter_H

#include <cstddef>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <aio.h>

using namespace std;

class AsyncFileWriter {
private:
    typedef struct aioBuffer {
        bool            enqueued;
        struct aiocb    aiocb;
        aioBuffer       *next;
    } aioBuffer;

    int                 queueProcessingInterval;
    aioBuffer           *listHead;
    aioBuffer           *lastBuffer;
    int                 fd;
    const char          *filename;
    int                 openFlags;
    mode_t              openMode;
    off_t               offset;
    int                 submitted;
    int                 completed;

public:
    AsyncFileWriter(const char *);
    ~AsyncFileWriter();
    int openFile();
    int closeFile();
    int getSubmitted();
    int getCompleted();
    bool pendingWrites();
    int getQueueProcessingInterval();
    void setQueueProcessingInterval(int);
    int write(const void *buf, size_t count);
    int processQueue();
    void cancelWrites();
};

#endif
