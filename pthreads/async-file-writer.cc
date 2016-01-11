#include "async-file-writer.h"

AsyncFileWriter::AsyncFileWriter(const char *filename)
{
    queueProcessingInterval = 40;
    listHead = NULL;
    lastBuffer = NULL;
    writerBuffer = NULL;
    fd = -1;
    this->filename = filename;
    openFlags = O_WRONLY|O_CREAT|O_TRUNC;
    openMode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;
    submitted = 0;
    completed = 0;
    synchronous = false;
    closeCalled = false;
    writeError = false;
    opened = false;
    pthread_mutex_init(&openedLock, NULL);
    pthread_mutex_init(&writerBufferLock, NULL);
    writerStarted = false;
}

AsyncFileWriter::~AsyncFileWriter()
{
    // Canceling the writes deletes the file if there are any pending writes.
    // The destructor should not be called if there are any unless we want
    // the file discarded. In a normal destructor call after writes are
    // completed, cancelWrites() doesn't do anything. We call it just to be
    // sure all memory allocated has really been freed to avoid memory leaks.
    cancelWrites();
    closeFile();

    // Clean up the open thread attributes. The attributes will have been set
    // only if an attempt to open the file happened.
    pthread_mutex_lock(&openedLock);

    if (opened) {
        pthread_attr_destroy(&attr);
    }

    pthread_mutex_unlock(&openedLock);

    // Clean up the mutexes.
    pthread_mutex_destroy(&openedLock);
    pthread_mutex_destroy(&writerBufferLock);
}

// This is the private open thread helper method. This recieves a pointer
// to this so that it can call the right object's thr_open() method. You have
// to use a static method in pthread_create().
void *AsyncFileWriter::thr_open_helper(void *context) {
    ((AsyncFileWriter *)context)->thr_open();
    return (void *)0;
}

// The actual private thread open method.
void AsyncFileWriter::thr_open()
{
    pthread_mutex_lock(&openedLock);

    if (!opened) {
        // We don't need to check the result of open. If fd is -1 and opened
        // is true, we know there was a problem.
        fd = open(filename, openFlags, openMode);
        opened = true;
    }

    pthread_mutex_unlock(&openedLock);
}

// This is the private writer thread helper method. This recieves a pointer
// to this so that it can call the right object's thr_writer() method. You have
// to use a static method in pthread_create().
void *AsyncFileWriter::thr_writer_helper(void *context) {
    ((AsyncFileWriter *)context)->thr_writer();
    return (void *)0;
}

// The actual private thread writer method.
void AsyncFileWriter::thr_writer()
{
    int write_fd;

    while (true) {
        // Walk all of the existing buffers until we get to the end of the
        // current list of aioBuffer objects. The write() method will reset
        // writerBuffer to the listHead if it is ever set to NULL;
        pthread_mutex_lock(&writerBufferLock);

        while (writerBuffer != NULL) {
            pthread_mutex_unlock(&writerBufferLock);
            // It is safe to use writerBuffer below until we change its value
            // at the end, so it doesn't need to be locked. The write() method
            // will only set it if it is set to NULL here.
            pthread_mutex_lock(&writerBuffer->aioBufferLock);
            write_fd = writerBuffer->fd;
            pthread_mutex_unlock(&writerBuffer->aioBufferLock);

            // If the open thread has not opened the file yet, we will spin on
            // the current writerBuffer until that happens.
            if (write_fd != -1) {
                int wbytes = write(write_fd, writerBuffer->data,
                                   writerBuffer->count);
                pthread_mutex_lock(&writerBuffer->aioBufferLock);
                writerBuffer->completed = true;

                if (wbytes != writerBuffer->count) {
                    // There was either a short write or a write error. The
                    // default value is already false;
                    writerBuffer->error = true;
                }

                // We have the current writerBuffer locked so we can read its
                // next pointer safely. We need to lock writerBuffer, adjust
                // it, unlock it, then unlock the current writerBuffer.
                aioBuffer *previous = writerBuffer;
                // We don't unlock the writerBufferLock again because that
                // will happen in either breaking out of the loop or in the
                // first step in loop. It is critical to balance mutex locks
                // and unlocks of course - or really weird errors happen. :-)
                pthread_mutex_lock(&writerBufferLock);
                writerBuffer = writerBuffer->next;
                pthread_mutex_unlock(&previous->aioBufferLock);
            } else {
                // We need to ensure the mutex is locked for the while
                // condition in the case write_fd was -1. Remember - balancing
                // mutex locks and unlocks is critical.
                pthread_mutex_lock(&writerBufferLock);
            }
        }

        // Nothing can be done yet.
        pthread_mutex_unlock(&writerBufferLock);
    }
}

int AsyncFileWriter::openFile()
{
    if (synchronous) {
        fd = open(filename, openFlags, openMode);
        return fd;
    }

    pthread_mutex_lock(&openedLock);

    if (!opened) {
        pthread_mutex_unlock(&openedLock);
        // Technically, pthread_attr_init can fail. It will never fail on
        // Linux, but this is why it is called here and not in the constructor.
        // It should only be called once, thus is protected by the fact opened
        // will only allow this to happen one time.
        if (pthread_attr_init(&attr) != 0) {
            return -1;
        }

        // Create the thread in a detached state so that its resources will
        // be automatically cleaned when it exits. We don't care about the
        // return value. We know the opened failed if fd is -1 and opened is
        // true.
        if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
            return -1;
        }

        if (pthread_create(&openTid, &attr, &AsyncFileWriter::thr_open_helper,
                           this) != 0) {
            return -1;
        }

        return 0;
    }

    pthread_mutex_unlock(&openedLock);
    return fd;
}

int AsyncFileWriter::closeFile()
{
    int ret = 0;

    // We should only close the file once. This is used in the destructor, so
    // if the user closed the file explicitly, there is nothing to do.
    if (closeCalled) {
        return ret;
    }

    if (synchronous) {
        if (fd != -1) {
            ret = close(fd);
        }

        closeCalled = true;
        return ret;
    }

    pthread_mutex_lock(&openedLock);

    if (opened) {
        if (fd == -1) {
            // There was an open() error.
            ret = -1;
        } else {
            ret = close(fd);
        }
    }

    pthread_mutex_unlock(&openedLock);
    closeCalled = true;
    return ret;
}

int AsyncFileWriter::getSubmitted()
{
    return submitted;
}

int AsyncFileWriter::getCompleted()
{
    return completed;
}

bool AsyncFileWriter::pendingWrites()
{
    return submitted != completed;
}

bool AsyncFileWriter::getSynchronous()
{
    return synchronous;
}

void AsyncFileWriter::setSynchronous(bool value)
{
    synchronous = value;
}

bool AsyncFileWriter::getWriteError()
{
    return writeError;
}

int AsyncFileWriter::getQueueProcessingInterval()
{
    return queueProcessingInterval;
}

void AsyncFileWriter::setQueueProcessingInterval(int value)
{
    queueProcessingInterval = value;
}

int AsyncFileWriter::submitWrite(const void *data, size_t count)
{
    // Do a simple pwrite() if in synchronous mode.
    if (synchronous) {
        int wbytes;

        if ((wbytes = write(fd, data, count)) != count) {
            // This could be because of an error (-1 return value) or a short
            // write. Neither of those should happen, so we just return an
            // error.
            return -1;
        }

        return wbytes;
    }

    int current_fd;
    // Check if there was an error in open().
    pthread_mutex_lock(&openedLock);

    if (opened && fd == -1) {
        pthread_mutex_unlock(&openedLock);
        return -1;
    }

    current_fd = fd;
    pthread_mutex_unlock(&openedLock);
    aioBuffer *aio_buffer;
    void *aio_data;

    if ((aio_buffer = (aioBuffer *)malloc(sizeof(aioBuffer))) == NULL) {
        return -1;
    }

    if ((aio_data = malloc(count)) == NULL) {
        free(aio_buffer);
        return -1;
    }

    if (pthread_mutex_init(&aio_buffer->aioBufferLock, NULL) != 0) {
        free(aio_buffer);
        free(aio_data);
        return -1;
    }

    memcpy(aio_data, data, count);
    aio_buffer->completed = false;
    aio_buffer->error = false;
    aio_buffer->fd = current_fd;
    aio_buffer->data = aio_data;
    aio_buffer->count = count;
    // Set the next buffer to be NULL.
    aio_buffer->next = NULL;

    // Set the listHead and advance lastBuffer.
    if (listHead == NULL) {
        listHead = aio_buffer;
        lastBuffer = aio_buffer;
    } else {
        pthread_mutex_lock(&lastBuffer->aioBufferLock);
        lastBuffer->next = aio_buffer;
        pthread_mutex_unlock(&lastBuffer->aioBufferLock);
        lastBuffer = aio_buffer;
    }

    // If the writerBuffer is NULL, reset it to the head of the list so the
    // writer thread can start over. This only happens if the writer has
    // caught up before we added a new item.
    pthread_mutex_lock(&writerBufferLock);

    if (writerBuffer == NULL) {
        writerBuffer = listHead;
    }

    pthread_mutex_unlock(&writerBufferLock);
    // Increment the offset for the next write and the submitted write count.
    submitted += 1;

    // Process the queue every queueProcessingInterval requests. This will
    // free up memory as new writes are added to the queue. Before finishing,
    // processQueue() should be called by the caller while pendingWrites()
    // returns true. Setting the queueProcessingInterval to 0 cancels this
    // behavior. If the writer hasn't started yet, start the writer first.
    if (writerStarted) {
        if (queueProcessingInterval > 0 &&
            submitted % queueProcessingInterval == 0) {
            if (processQueue() == -1) {
                return -1;
            }
        }
    } else {
        // Start the writer thread in a non-detached state so we can kill it
        // later.
        if (pthread_create(&writerTid, NULL,
                           &AsyncFileWriter::thr_writer_helper, this) != 0) {
            return -1;
        }

        writerStarted = true;
    }

    return 0;
}

int AsyncFileWriter::processQueue()
{
    // No processing is done unless the file has been opened.
    pthread_mutex_lock(&openedLock);
    
    if (!opened) {
        pthread_mutex_unlock(&openedLock);
        return 0;
    }

    pthread_mutex_unlock(&openedLock);
    bool write_completed;
    bool error;
    aioBuffer *removal = NULL;
    aioBuffer *current = listHead;

    while (current != NULL) {
        pthread_mutex_lock(&current->aioBufferLock);
        write_completed = current->completed;
        error = current->error;
        // Set the file descriptor now that the file has been opened.
        current->fd = fd;
        pthread_mutex_unlock(&current->aioBufferLock);

        // We don't need to worry about locking the aioBuffer using its
        // aioBufferLock mutex because the writer is done with it.
        if (write_completed) {
            // Flag that there was a write error if one is ever found. The
            // caller should give up, cancel writes, etc. if there are any
            // write errors.
            if (error) {
                writeError = true;
            }

            // Writes are always completed in the order they were submitted.
            // We are always looking at the listHead. We can only advance
            // until we find an incomplete request or hit the end of the list.
            // We don't change lastBuffer as we won't hit the end of the
            // queue until everything is written.
            listHead = current->next;
            removal = current;
            current = current->next;
            pthread_mutex_destroy(&removal->aioBufferLock);
            free((void *)removal->data);
            free(removal);
            completed++;
        } else {
            // Stop processing aioBuffer objects if we found one which has not
            // been written.
            current = NULL;
        }
    }

    if (writeError) {
        return -1;
    }

    return 0;
}

int AsyncFileWriter::queueSize()
{
    aioBuffer *previous;
    aioBuffer *current = listHead;
    int count = 0;

    while (current != NULL) {
        previous = current;
        pthread_mutex_lock(&current->aioBufferLock);
        current = current->next;
        pthread_mutex_unlock(&previous->aioBufferLock);
        count++;
    }

    return count;
}

void AsyncFileWriter::cancelWrites()
{
    // Stop the writer thread.
    pthread_mutex_lock(&writerBufferLock);
    pthread_cancel(writerTid);
    pthread_mutex_unlock(&writerBufferLock);

    if (listHead != NULL) {
        // Unlink the file.
        unlink(filename);
    }
}
