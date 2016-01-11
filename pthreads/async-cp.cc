#include <iostream>
#include <stdio.h>
#include "async-file-writer.h"

#define DATA_SZ     4096

using namespace std;

void usage()
{
    cout << endl;
    cout << "Usage: %s <source> <destination>" << endl;
    cout << endl;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        usage();
        return -1;
    }

    int n;
    int source_fd;
    unsigned char data[DATA_SZ];
    const char *source = argv[1];
    const char *dest = argv[2];

    if ((source_fd = open(source, O_RDONLY)) == -1) {
        perror("open error");
        return 1;
    }

    AsyncFileWriter *asyncFileWriter = new AsyncFileWriter(dest);
    // The default queue processing interval is 40 writes.
    //asyncFileWriter->setQueueProcessingInterval(1000);
    // Disable processing the queue.
    //asyncFileWriter->setQueueProcessingInterval(0);

    if (asyncFileWriter->openFile() == -1) {
        perror("asyncFileWriter.openFile()");
        return 1;
    }

    while ((n = read(source_fd, data, DATA_SZ)) > 0) {
        if (asyncFileWriter->submitWrite(data, n) == -1) {
            perror("asyncFileWriter.submitWrite() error");
            asyncFileWriter->cancelWrites();
            delete asyncFileWriter;
            return 1;
        }
    }

    cout << "Submitted:  " << asyncFileWriter->getSubmitted() << endl;
    bool reported = false;

    // Poll on the queue until the file is written.
    while (asyncFileWriter->pendingWrites()) {
        if (asyncFileWriter->processQueue() == -1) {
            perror("asyncFileWriter.processQueue() error");
            asyncFileWriter->cancelWrites();
            delete asyncFileWriter;
            return 1;
        }

        cout << "Completed:  " << asyncFileWriter->getCompleted() << endl;
        cout << "Queue size: " << asyncFileWriter->queueSize() << endl;
        reported = true;
    }

    if (!reported) {
        cout << "Completed:  " << asyncFileWriter->getCompleted() << endl;
    }

    // The destructor will also close the file, but it's best to do so
    // explicity IMO.
    asyncFileWriter->closeFile();
    delete asyncFileWriter;
    close(source_fd);
    return 0;
}
