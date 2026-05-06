#ifndef RANDOMBYTES_H
#define RANDOMBYTES_H

#include <stddef.h>
#include <stdint.h>

// Deklarasi fungsi agar bisa dipanggil oleh sign.c
void randombytes(uint8_t *out, size_t outlen);

#endif