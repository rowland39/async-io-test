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

    if (asyncFileWriter->openFile() == -1) {
        perror("asyncFileWriter.openFile()");
        return 1;
    }

    while ((n = read(source_fd, data, DATA_SZ)) > 0) {
        if (asyncFileWriter->syncWrite(data, n) == -1) {
            perror("asyncFileWriter.syncWrite() error");
            delete asyncFileWriter;
            return 1;
        }
    }

    // The destructor will also close the file, but it's best to do so
    // explicity IMO.
    asyncFileWriter->closeFile();
    delete asyncFileWriter;
    close(source_fd);
    return 0;
}
