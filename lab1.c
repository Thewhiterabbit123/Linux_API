//Блокировать файловый дескриптор пока процесс пишет или читает его

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#define SIZEOFARRAY 16
#define SIZEOFBUFFER 8

char* write_int(int var, int fd) {
	char  tmpChar[SIZEOFARRAY];
	char* result = (char*) malloc (SIZEOFARRAY*sizeof(char));

	if (!result) {
		int len = strlen(strerror(errno));
		write(2, strerror(errno), len);
		return NULL;
	}

	int pos = 0;
	int i = 0;

    while (var != 0) {
        tmpChar[pos++] = var % 10 + '0';
        var /= 10;
    }

    while (pos != 0) {
    	result[i++] = tmpChar[--pos];
    }
    result[i++] = '\0';

    return result;
}

int find_last_number(char* arr) {
	int number = 0;
	for (int i = 0; i < SIZEOFBUFFER; ++i) {
		if(isdigit(arr[i])) {
			number = number*10 + (arr[i] - '0');
		}
		if (arr[i] == '\n') {
			number = 0;
			continue;
		} 
	}

	return number;
}

int main (int argc, char* argv[]) {
	int fd = open(argv[1], O_RDWR);

	if (fd == -1) {
		int len = strlen(strerror(errno));
		write(2, strerror(errno), len);
		return EXIT_FAILURE;
	}

	struct flock lock;
	memset(&lock, 0, sizeof(lock));
	
	fcntl(fd, F_GETLK, &lock);
	lock.l_type = F_WRLCK;
	fcntl (fd, F_SETLKW, &lock);

	lseek(fd, -SIZEOFBUFFER, SEEK_END);
	char buff[SIZEOFBUFFER + 1];
	read(fd, buff, SIZEOFBUFFER);

	int result = find_last_number(buff);

	result++;
	

    char* temp = write_int(result, fd);
    if (!temp) {
    	int len = strlen(strerror(errno));
		write(2, strerror(errno), len);
		return EXIT_FAILURE;
    }
    int len = strlen(temp);
    write(fd, "\n", 1);

    if(write(fd, temp, len) == -1) {
 	    int templen = strlen(strerror(errno));
    	write(1, strerror(errno), templen);
		return EXIT_FAILURE;
    }
 

    free(temp);

	printf ("\nLocked; \nEnter to unlock ");
	getchar ();
	printf ("Unlocking\n");

	lock.l_type = F_UNLCK;
	fcntl (fd, F_SETLKW, &lock);
	close (fd);
	return EXIT_SUCCESS;
}