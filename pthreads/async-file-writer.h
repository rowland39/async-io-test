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
#include <pthread.h>
#include <thread>
#include <mutex>

using namespace std;

class AsyncFileWriter {
private:
    typedef struct aioBuffer {
        pthread_mutex_t aioBufferLock;
        bool            completed;
        bool            error;
        int             fd;
        void            *data;
        size_t          count;
        aioBuffer       *next;
    } aioBuffer;

    int                 queueProcessingInterval;
    aioBuffer           *listHead;
    aioBuffer           *lastBuffer;
    aioBuffer           *writerBuffer;
    int                 fd;
    const char          *filename;
    int                 openFlags;
    mode_t              openMode;
    int                 submitted;
    int                 completed;
    bool                synchronous;
    bool                closeCalled;
    bool                writeError;

    // This flag indicates if the file we are working on has been opened.
    // The open() method executes in a separate thread because open() itself
    // can block. We never want to block the caller.
    bool                opened;
    pthread_mutex_t     openedLock;
    pthread_mutex_t     writerBufferLock;
    pthread_t           openTid;
    pthread_t           writerTid;
    pthread_attr_t      attr;
    // This flag indicates the writer thread has started. It doesn't need to
    // be protected. The write() calls happen in the writer thread because we
    // never want to block the caller.
    bool                writerStarted;

public:
    AsyncFileWriter(const char *);
    ~AsyncFileWriter();
    // The private open thread helper method. The argument is the this pointer
    // so that it can call thr_open(). You have to use a static method in
    // pthread_create().
    static void *thr_open_helper(void *);
    // The actual threaded open method called by the private helper.
    void thr_open();
    // The private writer thread helper method. The argument is the this pointer
    // so that it can call thr_writer(). You have to use a static method in
    // pthread_create().
    static void *thr_writer_helper(void *);
    // The actual threaded write method called by the private helper.
    void thr_writer();
    int openFile();
    int closeFile();
    int getSubmitted();
    int getCompleted();
    bool pendingWrites();
    bool getSynchronous();
    void setSynchronous(bool);
    bool getWriteError();
    int getQueueProcessingInterval();
    void setQueueProcessingInterval(int);
    int submitWrite(const void *, size_t);
    int processQueue();
    int queueSize();
    void cancelWrites();
};

#endif
