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
#include <pthread.h>

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
    bool                synchronous;
    bool                closeCalled;

    // This flag indicates if the file we are working on has been opened.
    // The open() method executes in a separate thread because open() itself
    // can block. We never want to block the caller.
    bool                opened;
    pthread_mutex_t     openedLock;
    pthread_t           ntid;
    pthread_attr_t      attr;

public:
    AsyncFileWriter(const char *);
    ~AsyncFileWriter();
    // The private open thread helper method. The argument is the this pointer
    // so that it can call thr_open(). You have to use a static method in
    // pthread_create().
    static void *thr_open_helper(void *);
    // The actual threaded open method called by the private helper.
    void thr_open();
    int openFile();
    int closeFile();
    int getSubmitted();
    int getCompleted();
    bool pendingWrites();
    bool getSynchronous();
    void setSynchronous(bool);
    int getQueueProcessingInterval();
    int queueSize();
    void setQueueProcessingInterval(int);
    int write(const void *, size_t);
    int processQueue();
    void cancelWrites();
};

#endif
