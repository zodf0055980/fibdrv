// static const long long BASE = 100000000;// = 10^8,bits per part = 7
#define BASE 10000000000
#define bits_per_part 10
#define part_num 4
typedef struct bigNum {
    long long part[part_num];
} bigNum;