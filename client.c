#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "big.h"

#define FIB_DEV "/dev/fibonacci"

void big_print(bigNum buf)
{
    int i = part_num - 1;
    while ((i >= 0) && (buf.part[i] == 0))
        i--;
    if (i < 0) {
        printf("0");
        return;
    }
    printf("%lld", buf.part[i--]);
    while (i >= 0) {
        printf("%08lld", buf.part[i]);
        i--;
    }
}

int getmiddle(int timearray[])
{
    int temp;
    int i, j;
    for (i = 0; i < 5 - 1; i++)
        for (j = 0; j < 5 - 1 - i; j++) {
            if (timearray[j] > timearray[j + 1]) {
                temp = timearray[j];
                timearray[j] = timearray[j + 1];
                timearray[j + 1] = temp;
            }
        }
    return timearray[2];
}

static int diff_in_ns(struct timespec t1, struct timespec t2)
{
    struct timespec diff;
    if (t2.tv_nsec - t1.tv_nsec < 0) {
        diff.tv_sec = t2.tv_sec - t1.tv_sec - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    } else {
        diff.tv_sec = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }

    return (diff.tv_sec * 1000000000 + diff.tv_nsec);
}

int main()
{
    int fd;
    long long sz;

    bigNum buf;
    char write_buf[] = "testing writing";
    int offset = 500;  // TODO: test something bigger than the limit
    int i = 0;

    FILE *fp = fopen("time.txt", "wb+");
    fd = open(FIB_DEV, O_RDWR);

    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    int count = 0;
    int gettime[5] = {0};
    for (i = 0; i <= offset; i++) {
        memset(&buf, 0, sizeof(bigNum));
        lseek(fd, i, SEEK_SET);

        struct timespec start, end;
        clock_gettime(CLOCK_REALTIME, &start);
        sz = read(fd, &buf, sizeof(bigNum));
        clock_gettime(CLOCK_REALTIME, &end);
        //        fprintf(fp, "%d %d %d\n", i, diff_in_ns(start, end),
        //        buf.time);
        // fprintf(fp, "%d %d\n", i, buf.time);
        if (count < 4) {
            gettime[count] = buf.time;
            count++;
        } else {
            gettime[count] = buf.time;
            count = 0;
            int midtime = getmiddle(gettime);
            fprintf(fp, "%d %d\n", i, midtime);
        }

        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        big_print(buf);
        printf(".\n");
    }

    for (i = offset; i >= 0; i--) {
        memset(&buf, 0, sizeof(bigNum));
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &buf, sizeof(bigNum));
        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        big_print(buf);
        printf(".\n");
    }

    fclose(fp);
    close(fd);
    return 0;
}
