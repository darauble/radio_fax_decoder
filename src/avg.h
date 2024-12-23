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

#if defined(__AVX512F__) && defined(__AVX512DQ__)

float int16_float_avx512_average(const int16_t *data, const size_t size);

double int16_avx512_average(const int16_t *data, const size_t size);
#define FLOAT_AVERAGE(d, s) int16_float_avx512_average(d, s)
#elif defined(__ARM_NEON)
float int16_float_neon_average(const int16_t *data, const size_t size);
#define FLOAT_AVERAGE(d, s) int16_float_neon_average(d, s)
#else
#define FLOAT_AVERAGE(d, s) int16_float_average(d, s)
#endif
