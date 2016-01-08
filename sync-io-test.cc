#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

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
        return 1;
    }

    int count = (int)strtol(argv[1], (char **)NULL, 10);
    int openFlags = O_WRONLY|O_CREAT|O_TRUNC;
    mode_t openMode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;
    int fd;
   
    if ((fd = open("test-file.txt", openFlags, openMode)) == -1) {
        perror("open error");
        return 1;
    }

    // We do all of this to mimic what is needed for asynchronous writes with
    // respect to copying the buffer to a new buffer, etc.
    const char *message = "Hello World\n";
    char *data;

    for (int t = 0; t < count; t++) {
        if ((data = (char *)malloc(12)) == NULL) {
            perror("malloc error");
            return 1;
        }

        memcpy(data, message, strlen(message));

        if (write(fd, data, 12) == -1) {
            perror("write error");
            return 1;
        }

        free(data);
    }

    close(fd);
    return 0;
}
