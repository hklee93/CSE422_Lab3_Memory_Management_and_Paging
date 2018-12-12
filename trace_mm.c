/*
 *  Finds sum of diagonal entries A
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <paging.h>
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

const int num_expected_args = 2;
const unsigned sqrt_of_UINT32_MAX = 65536;
static struct timeval start, mmap_end, mult_end;

static void *
mmap_malloc(int    fd,
            size_t bytes)
{

    void * data;

    data = mmap(0, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "Could not mmap " DEV_NAME ": %s\n", strerror(errno));
        return NULL;
    }

    return data;

    //return malloc(bytes);
}

int main (int argc, char* argv[])
{
    int fd;
	unsigned row; //loop indicies
	unsigned matrix_size, squared_size;
	double *A;
    double sum;

	if (argc != num_expected_args) {
		printf("Usage: ./trace_mm <size of matrices>\n");
		exit(-1);
	}

	matrix_size = atoi(argv[1]);

	if (matrix_size > sqrt_of_UINT32_MAX) {
		printf("ERROR: Matrix size must be between zero and %u!\n", sqrt_of_UINT32_MAX);
		exit(-1);
	}

    fd = open(DEV_NAME, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "Could not open " DEV_NAME ": %s\n", strerror(errno));
        return -1;
    }

	squared_size = matrix_size * matrix_size;

	gettimeofday(&start, NULL);
    A = (double *)mmap_malloc(fd, sizeof(double) * squared_size);
	gettimeofday(&mmap_end, NULL);

	for (row = 0; row < matrix_size; row++) {
        sum += (double)A[row*matrix_size+row];
	}
	gettimeofday(&mult_end, NULL);

    printf("Summation done\n");
	printf("Timing Results (microseconds)\n");
	printf("mmap: %ld ", (mmap_end.tv_sec * 1000000 + mmap_end.tv_usec)
		  - (start.tv_sec * 1000000 + start.tv_usec));
	printf("mult: %ld\n", (mult_end.tv_sec * 1000000 + mult_end.tv_usec)
		  - (mmap_end.tv_sec * 1000000 + mmap_end.tv_usec));

    return 0;
}
