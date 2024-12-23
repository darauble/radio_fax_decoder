#include "avg.h"
#include <cstdio>

float int16_float_average(const int16_t *data, const size_t size) {
    float avg = 0;

    for (size_t i = 0; i < size; i++) {
        avg += (data[i] - avg) / (i + 1);
    }

    return avg;
}

double int16_average(const int16_t *data, const size_t size) {
    double avg = 0;

    for (size_t i = 0; i < size; i++) {
        avg += (data[i] - avg) / (i + 1);
    }

    return avg;
}

#if defined(__AVX512F__) && defined(__AVX512DQ__)
float int16_float_avx512_average(const int16_t *data, const size_t size) {
    if (size < 256) {
        return int16_float_average(data, size);
    }

    __m512 avg_vec = _mm512_setzero_ps();
    __m512i count_vec = _mm512_set1_epi32(0);

    float averages[16];

    for (size_t i = 0; i < size; i += 16) {
        __m256i data_vec_16 = _mm256_loadu_si256((__m256i*)&data[i]);
        __m512i data_vec = _mm512_cvtepi16_epi32(data_vec_16);
        __m512 values = _mm512_cvtepi32_ps(data_vec);

        count_vec = _mm512_add_epi32(count_vec, _mm512_set1_epi32(1));
        avg_vec = _mm512_add_ps(avg_vec, _mm512_div_ps(_mm512_sub_ps(values, avg_vec), _mm512_cvtepi32_ps(count_vec)));
    }

    const float one_sixteenth[16] = {
        0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625,
        0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625, 0.0625
    };

    __m512 one_sixteenth_vec = _mm512_loadu_ps(one_sixteenth);

    float avg = _mm512_reduce_add_ps(_mm512_mul_ps(avg_vec, one_sixteenth_vec));

    size_t remainder = size % 16;

    if (remainder > 0) {
        for (size_t i {size - remainder}; i < size; i++) {
            avg += (data[i] - avg) / (i + 1);
        }
    }

    return avg;
}

double int16_avx512_average(const int16_t *data, const size_t size) {
    if (size < 256) {
        return int16_average(data, size);
    }

    __m512d avg_vec = _mm512_setzero_pd();
    __m512i count_vec = _mm512_set1_epi64(0);

    double averages[8];

    for (size_t i = 0; i < size; i += 8) {
        __m128i data_vec_16 = _mm_loadu_si128((__m128i*)&data[i]);
        __m256i data_vec = _mm256_cvtepi16_epi32(data_vec_16);
        __m512d values = _mm512_cvtepi32_pd(data_vec);

        count_vec = _mm512_add_epi64(count_vec, _mm512_set1_epi64(1));
        avg_vec = _mm512_add_pd(avg_vec, _mm512_div_pd(_mm512_sub_pd(values, avg_vec), _mm512_cvtepi64_pd(count_vec)));
    }

    const double one_eighth[8] = {0.125, 0.125, 0.125, 0.125, 0.125, 0.125, 0.125, 0.125};
    __m512d one_eighth_vec = _mm512_loadu_pd(one_eighth);

    double avg = _mm512_reduce_add_pd(_mm512_mul_pd(avg_vec, one_eighth_vec));

    size_t remainder = size % 8;

    if (remainder > 0) {
        for (size_t i {size - remainder}; i < size; i++) {
            avg += (data[i] - avg) / (i + 1);
        }
    }

    return avg;
}
#elif defined(__ARM_NEON)
float int16_float_neon_average(const int16_t *data, const size_t size) {
    if (size < 256) {
        return int16_float_average(data, size);
    }

    float32x4_t avg_vec = vdupq_n_f32(0.0f);
    uint32x4_t count_vec	= vdupq_n_u32(0);

    for (size_t i = 0; i < size; i += 4) {
        int16x4_t data_vec_16 = vld1_s16(&data[i]);
        int32x4_t data_vec = vmovl_s16(data_vec_16);
        float32x4_t values = vcvtq_f32_s32(data_vec);

        count_vec = vaddq_u32(count_vec, vdupq_n_u32(1));
        avg_vec = vaddq_f32(avg_vec, vdivq_f32(vsubq_f32(values, avg_vec), vcvtq_f32_u32(count_vec)));
    }

    const float one_fourth[4] = {0.25, 0.25, 0.25, 0.25};
    
    float32x4_t one_fourth_vec = vld1q_f32(one_fourth);
    float avg = vaddvq_f32(vmulxq_f32(avg_vec, one_fourth_vec));

    size_t remainder = size % 4;

    if (remainder > 0) {
        for (size_t i {size - remainder}; i < size; i++) {
            avg += (data[i] - avg) / (i + 1);
        }
    }

    return avg;
}
#endif