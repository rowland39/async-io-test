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
    cout << "written to ./test-file.txt synchronously." << endl;
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

    if (asyncFileWriter->openFile() == -1) {
        perror("asyncFileWriter.openFile()");
        return 1;
    }

    for (int t = 0; t < count; t++) {
        if (asyncFileWriter->syncWrite("Hello World\n", 12) == -1) {
            perror("asyncFileWriter.syncWrite() error");
            delete asyncFileWriter;
            return 1;
        }
    }

    // The destructor will also close the file, but it's best to do so
    // explicity IMO.
    asyncFileWriter->closeFile();
    delete asyncFileWriter;
    return 0;
}
