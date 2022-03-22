#define F (1 << 14)
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))

// int to fixed point
int n_to_fp (int n){
    return n * F;
}

// fixed point to int (내림)
int fp_to_n (int x){
    return x / F;
}

// fixed point to int (반올림)
int fp_to_n_rounding (int x) {
    if (x >= 0) return (x + F/2) / F;
    else return (x - F/2) / F;
}

int add_fp_fp (int x, int y) {
    return x + y;
}

int sub_fp_fp (int x, int y) {
    return x - y;
}

int add_fp_n (int x, int n) {
    return x + n * F;
}

// subtract n from x
int sub_fp_n (int x, int n) {
    return x - n * F;
}

int mul_fp_fp (int x, int y) {
    return ((int64_t) x) * y / F;
}

int mul_fp_n (int x, int n) {
    return x * n;
}

int div_fp_fp (int x, int y) {
    return ((int64_t) x) * F / y;
}

int div_fp_n (int x, int n) {
    return x / n;
}