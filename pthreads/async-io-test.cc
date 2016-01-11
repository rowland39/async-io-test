#include <iostream>
#include <stdio.h>
#include "async-file-writer.h"

using namespace std;

void usage()
{
    cout << endl;
    cout << "Usage: %s <write count>" << endl;
    cout << endl;
    cout << "The \"write count\" value indicates how many lines of \"Hello World\" will be" << endl;
    cout << "written to ./test-file.txt asynchronously." << endl;
    cout << endl;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage();
        return -1;
    }

    int count = (int)strtol(argv[1], (char **)NULL, 10);
    AsyncFileWriter *asyncFileWriter = new AsyncFileWriter("test-file.txt");
    // The default queue processing interval is 40 writes.
    asyncFileWriter->setQueueProcessingInterval(1000);
    // Disable processing the queue.
    //asyncFileWriter->setQueueProcessingInterval(0);

    if (asyncFileWriter->openFile() == -1) {
        perror("asyncFileWriter.openFile()");
        return 1;
    }

    for (int t = 0; t < count; t++) {
        if (asyncFileWriter->submitWrite("Hello World\n", 12) == -1) {
            perror("asyncFileWriter.submitWrite() error");
            asyncFileWriter->cancelWrites();
            delete asyncFileWriter;
            return 1;
        }
    }

    // Destructor test (freeing AIO buffer list before completed). This will
    // automatically cancel any pending writes. This will also unlink the
    // file because we destroy the object before writes are completed (which
    // is the same as calling cancelWrites().
    //delete asyncFileWriter;
    //return 0;

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
    return 0;
}
