#include <iostream>
#include <stdio.h>
#include "async-file-writer.h"

using namespace std;

int
main(void)
{
    AsyncFileWriter *asyncFileWriter = new AsyncFileWriter("test-file.txt");
    // The default queue processing interval is 100 writes.
    asyncFileWriter->setQueueProcessingInterval(1000);
    // Disable queue processing during write() calls.
    //asyncFileWriter->setQueueProcessingInterval(0);

    if (asyncFileWriter->openFile() == -1) {
        perror("asyncFileWriter.openFile()");
        return 1;
    }

    for (int t = 0; t < 100000; t++) {
        if (asyncFileWriter->write("Hello World\n", 12) == -1) {
            perror("asyncFileWriter.write() error");
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

    cout << "Submitted:  " << asyncFileWriter->getSubmitted() <<
         endl;

    // Poll on the queue until the file is written.
    while (asyncFileWriter->pendingWrites()) {
        if (asyncFileWriter->processQueue() == -1) {
            perror("asyncFileWriter.processQueue() error");
            asyncFileWriter->cancelWrites();
            delete asyncFileWriter;
            return 1;
        }

        cout << "Completed:  " <<
             asyncFileWriter->getCompleted() << endl;
        cout << "Queue size: " << asyncFileWriter->queueSize() << endl;
    }

    // The destructor will also close the file, but it's best to do so
    // explicity IMO.    
    asyncFileWriter->closeFile();
    delete asyncFileWriter;
    return 0;
}
