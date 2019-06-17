// static const long long BASE = 100000000;// = 10^8,bits per part = 7
#define BASE 100000000
#define bits_per_part 8
#define part_num 14

#define mode 2
// 0 = fib_sequence, 1 = Fast doubling, 2 = Fast doubling with clz

typedef struct bigNum {
    long long part[part_num];
    int time;
} bigNum;