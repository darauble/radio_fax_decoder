#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__AVX512F__) && defined(__AVX512DQ__)
#include <immintrin.h>
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

float int16_float_average(const int16_t *data, const size_t size);

double int16_average(const int16_t *data, const size_t size);

void int16_subtract(int16_t *data, const size_t size, int16_t avg);

#if defined(__AVX512F__) && defined(__AVX512DQ__)

float int16_float_avx512_average(const int16_t *data, const size_t size);

double int16_avx512_average(const int16_t *data, const size_t size);

#define FLOAT_AVERAGE(d, s) int16_float_avx512_average(d, s)

void int16_avx512_subtract(int16_t *data, const size_t size, int16_t avg);

#define SAMPLES_SUBTRACT(d, s, a) int16_avx512_subtract(d, s, a)

#elif defined(__ARM_NEON)
float int16_float_neon_average(const int16_t *data, const size_t size);
#define FLOAT_AVERAGE(d, s) int16_float_neon_average(d, s)
#define SAMPLES_SUBTRACT(d, s, a) int16_subtract(d, s, a) // TODO: add support for NEON
#else
#define FLOAT_AVERAGE(d, s) int16_float_average(d, s)
#define SAMPLES_SUBTRACT(d, s, a) int16_subtract(d, s, a)
#endif
