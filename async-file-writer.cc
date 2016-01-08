#include "async-file-writer.h"

AsyncFileWriter::AsyncFileWriter(const char *filename)
{
    queueProcessingInterval = 40;
    listHead = NULL;
    lastBuffer = NULL;
    fd = -1;
    this->filename = filename;
    openFlags = O_WRONLY|O_CREAT|O_TRUNC;
    openMode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;
    offset = 0;
    submitted = 0;
    completed = 0;
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
}

int AsyncFileWriter::openFile()
{
    if (fd == -1) {
        fd = open(filename, openFlags, openMode);
        return fd;
    }

    return -1;
}

int AsyncFileWriter::closeFile()
{
    int ret = 0;

    if (fd != -1) {
        ret = close(fd);
        fd = -1;
    }
    
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

int AsyncFileWriter::getQueueProcessingInterval()
{
    return queueProcessingInterval;
}

void AsyncFileWriter::setQueueProcessingInterval(int value)
{
    queueProcessingInterval = value;
}

int AsyncFileWriter::write(const void *data, size_t count)
{
    aioBuffer *aio_buffer;
    void *aio_data;

    if ((aio_buffer = (aioBuffer *)malloc(sizeof(aioBuffer))) == NULL) {
        return -1;
    }

    if ((aio_data = malloc(count)) == NULL) {
        free(aio_buffer);
        return -1;
    }

    memcpy(aio_data, data, count);
    aio_buffer->aiocb.aio_fildes = fd;
    aio_buffer->aiocb.aio_offset = offset;
    aio_buffer->aiocb.aio_buf = aio_data;
    aio_buffer->aiocb.aio_nbytes = count;
    aio_buffer->aiocb.aio_reqprio = 0;
    aio_buffer->aiocb.aio_sigevent.sigev_notify = SIGEV_NONE;
    aio_buffer->aiocb.aio_lio_opcode = LIO_WRITE;

    // Issue the AIO write request.
    if (aio_write(&aio_buffer->aiocb) == 0) {
        aio_buffer->enqueued = true;
    } else {
        if (errno == EAGAIN) {
            aio_buffer->enqueued = false;
        } else {
            free(aio_data);
            free(aio_buffer);
            return -1;
        }
    }

    // Set the next buffer to be NULL.
    aio_buffer->next = NULL;

    // Set the listHead and advance lastBuffer.
    if (listHead == NULL) {
        listHead = aio_buffer;
        lastBuffer = aio_buffer;
    } else {
        lastBuffer->next = aio_buffer;
        lastBuffer = aio_buffer;
    }

    // Increment the offset for the next write and the submitted write count.
    offset += count;
    submitted += 1;

    // Process the queue every queueProcessingInterval requests. This will
    // free up memory as new writes are added to the queue. Before finishing,
    // processQueue() should be called by the caller while pendingWrites()
    // returns true. Setting the queueProcessingInterval to 0 cancels this
    // behavior.
    if (queueProcessingInterval > 0 &&
        submitted % queueProcessingInterval == 0) {
        if (processQueue() == -1) {
            return -1;
        }
    }

    return 0;
}

int AsyncFileWriter::syncWrite(const void *data, size_t count)
{
    int wbytes;
    void *sync_data;

    if ((sync_data = malloc(count)) == NULL) {
        return -1;
    }

    memcpy(sync_data, data, count);

    if ((wbytes = pwrite(fd, sync_data, count, offset)) != count) {
        // This could be because of an error (-1 return value) or a short
        // write. Neither of those should happen, so we just return an error.
        free(sync_data);
        return -1;
    }

    // Increment the offset for the next write and the submitted write count.
    offset += count;

    return 0;
}

int AsyncFileWriter::processQueue()
{
    int ret;
    aioBuffer *previous = NULL;
    aioBuffer *removal = NULL;
    aioBuffer *current = listHead;

    while (current != NULL) {
        if (current->enqueued == true) {
            ret = aio_error(&current->aiocb);

            if (ret == 0) {
                aio_return(&current->aiocb);
                completed++;

                // If we are at the head of the list, advance the head. This
                // is fine even if current->next is NULL.
                if (current == listHead) {
                    listHead = current->next;
                    // If this is the last buffer in the queue, lastBuffer will
                    // also be set to NULL just like listHead above.
                    lastBuffer = current->next;
                    removal = current;
                    current = current->next;
                    free((void *)removal->aiocb.aio_buf);
                    free(removal);
                } else {
                    // If this is the last buffer in the queue, lastBuffer will
                    // point to the last valid buffer after removal below.
                    lastBuffer = previous;
                    previous->next = current->next;
                    removal = current;
                    current = current->next;
                    free((void *)removal->aiocb.aio_buf);
                    free(removal);
                }
            } else if (ret != EINPROGRESS) {
                return -1;
            }
        } else {
            if (aio_write(&current->aiocb) == 0) {
                current->enqueued = true;
            } else {
                // Do nothing if there still are no resources, otherwise there
                // is a failure from which we cannot recover.
                if (errno != EAGAIN) {
                    return -1;
                }
            }

            // Advance lastBuffer in case it was set in the removal of buffers
            // above due to AIO completions.
            lastBuffer = current;
            previous = current;
            current = current->next;
        }
    }

    return 0;
}

int AsyncFileWriter::queueSize()
{
    aioBuffer *current = listHead;
    int count = 0;

    while (current != NULL) {
        current = current->next;
        count++;
    }

    return count;
}

void AsyncFileWriter::cancelWrites()
{
    if (listHead != NULL) {
        // Cancel any outstanding AIO requets if any exist. Keep trying
        // until they are all canceled. This should only repeat if there is
        // one or a couple of outstanding requests in the process of writing.
        // It will not make this a long blocking call.
        while (aio_cancel(fd, NULL) == AIO_NOTCANCELED);

        // Free any remaining AIO blocks.
        aioBuffer *removal;
        aioBuffer *current = listHead;

        while (current != NULL) {
            removal = current;
            current = current->next;
            free((void *)removal->aiocb.aio_buf);
            free(removal);
        }

        // Unlink the file.
        unlink(filename);
    }
}
