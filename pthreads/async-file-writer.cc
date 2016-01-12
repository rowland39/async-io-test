#include "async-file-writer.h"

AsyncFileWriter::AsyncFileWriter(const char *filename)
{
    listHead = NULL;
    lastBuffer = NULL;
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
    pthread_mutex_init(&listHeadLock, NULL);
    pthread_mutex_init(&writeErrorLock, NULL);
    pthread_mutex_init(&completedLock, NULL);
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
    pthread_mutex_destroy(&listHeadLock);
    pthread_mutex_destroy(&writeErrorLock);
    pthread_mutex_destroy(&completedLock);
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
    int old_state;

    // Allow this thread to be canceled immediately from cancelWrites().
    if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_state) != 0) {
        pthread_mutex_lock(&writeErrorLock);
        writeError = true;
        pthread_mutex_unlock(&writeErrorLock);
    }

    while (true) {
        // Walk all of the existing buffers until we get to the end of the
        // current list of aioBuffer objects. The submitWrite() method will
        // reset listHead if it is ever set to NULL here.
        pthread_mutex_lock(&listHeadLock);

        while (listHead != NULL) {
            pthread_mutex_unlock(&listHeadLock);
            // It is safe to use listHead because we are the only method that
            // alters it once it has been set until it is NULL again. This is
            // the only place the aioBuffer attributes besides the next pointer
            // are used as well.
            if (listHead->fd != -1) {
                int wbytes = write(listHead->fd, listHead->data,
                                   listHead->count);

                if (wbytes != listHead->count) {
                    // There was either a short write or a write error. Set
                    // the writeError flag.
                    pthread_mutex_lock(&writeErrorLock);
                    writeError = true;
                    pthread_mutex_unlock(&writeErrorLock);
                }

                // Lock the listHead and its aioBuffer so that listHead can
                // be modified to the value of its aioBuffer next pointer.
                // We advance the listHead because we are now removing its
                // aioBuffer.
                pthread_mutex_lock(&listHeadLock);
                pthread_mutex_lock(&listHead->aioBufferLock);
                aioBuffer *removal = listHead;
                listHead = listHead->next;
                pthread_mutex_unlock(&removal->aioBufferLock);
                // Free the written aioBuffer.
                pthread_mutex_destroy(&removal->aioBufferLock);
                free(removal);
                pthread_mutex_unlock(&listHeadLock);
                // Update the completed count.
                pthread_mutex_lock(&completedLock);
                completed++;
                pthread_mutex_unlock(&completedLock);
            } else {
                // Check if the file has been opened and set the file
                // descriptor properly if it has. We will come around again in
                // the while loop to check this same buffer until its fd has
                // been set properly.
                pthread_mutex_lock(&openedLock);

                if (opened) {
                    // This is the only place the aioBuffer fd structure member
                    // is modified, so we don't need to lock the aioBuffer.
                    listHead->fd = fd;
                }

                pthread_mutex_unlock(&openedLock);
            }
        }

        // Nothing can be done yet because listHead was NULL. We balance the
        // mutex lock done previously and try again.
        pthread_mutex_unlock(&listHeadLock);
        // Sleep for 1 milisecond.
        usleep(1000);
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
    int num_completed;

    pthread_mutex_lock(&completedLock);
    num_completed = completed;
    pthread_mutex_unlock(&completedLock);

    return num_completed;
}

bool AsyncFileWriter::pendingWrites()
{
    bool pending;
    pthread_mutex_lock(&completedLock);
    pending = submitted != completed;
    pthread_mutex_unlock(&completedLock);

    return pending;
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

    // Check if there was a write error.
    pthread_mutex_lock(&writeErrorLock);

    if (writeError) {
        pthread_mutex_unlock(&writeErrorLock);
        return -1;
    }

    pthread_mutex_unlock(&writeErrorLock);

    // Check if there was an error in open() and get the file descriptor if
    // there was no error.
    int current_fd;
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
    aio_buffer->fd = current_fd;
    aio_buffer->data = aio_data;
    aio_buffer->count = count;
    // Set the next buffer to be NULL.
    aio_buffer->next = NULL;

    // Set the listHead and advance lastBuffer.
    pthread_mutex_lock(&listHeadLock);

    if (listHead == NULL) {
        listHead = aio_buffer;
        lastBuffer = aio_buffer;
    } else {
        pthread_mutex_lock(&lastBuffer->aioBufferLock);
        lastBuffer->next = aio_buffer;
        pthread_mutex_unlock(&lastBuffer->aioBufferLock);
        lastBuffer = aio_buffer;
    }

    pthread_mutex_unlock(&listHeadLock);
    // Increment the offset for the next write and the submitted write count.
    submitted += 1;

    // The writer will process the aioBuffer list itself because it does
    // writes in the order they were submitted.
    if (!writerStarted) {
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
    pthread_mutex_lock(&listHeadLock);
    pthread_cancel(writerTid);
    pthread_join(writerTid, NULL);
    // Free any remaining aioBuffers.
    aioBuffer *removal;
    aioBuffer *current = listHead;

    while (current != NULL) {
        removal = current;
        pthread_mutex_destroy(&current->aioBufferLock);
        current = current->next;
        free(removal);
    }

    pthread_mutex_unlock(&listHeadLock);

    if (listHead != NULL) {
        // Unlink the file.
        unlink(filename);
    }
}
