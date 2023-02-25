#include <syscall.h>
#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE 100

int main(void) {
// Open a file for reading
char file_name[] = "/home/dev/uwe_os/pintos-project/src/examples/test_file.txt";
int fd = open(file_name);

if (fd < 0) {
printf("Error opening file: %s\n", file_name);
return -1;
}

// Read data from the file
char buffer[BUFFER_SIZE];
int bytes_read = read(fd, buffer, BUFFER_SIZE);

if (bytes_read < 0) {
printf("Error reading from file: %s\n", file_name);
return -1;
}

// Print the data that was read
printf("Read %d bytes from file: %s\n", bytes_read, file_name);
printf("Data: %s\n", buffer);

return 0;
}