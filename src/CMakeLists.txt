# The fax library. Its own compile options are PRIVATE, so projects using it
# are not built with -Ofast or AVX-512 flags.

# DSP code: speed first. -Ofast (fast-math) also lets GCC vectorise reductions and
# use glibc's vector sin/cos (libmvec).
set(FAX_DSP_OPTIONS -Ofast -ftree-loop-vectorize -ftree-vectorize)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|i[3-6]86)$")
    # Use AVX-512 only if the compiler supports it AND this machine's CPU can run it.
    # A compile-only test would always pass with GCC, but the binary would then
    # crash with "Illegal instruction" on CPUs without AVX-512, so the test
    # program is also run. When cross-compiling it can't be run and AVX-512 stays off.
    include(CheckCXXSourceRuns)
    set(AVX512_FLAGS -mavx512bw -mavx512f -mavx512dq)
    set(CMAKE_REQUIRED_FLAGS "-mavx512bw -mavx512f -mavx512dq")
    check_cxx_source_runs("
        #include <immintrin.h>
        int main() {
            volatile float in[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            __m512 a = _mm512_loadu_ps((const float *)in);
            __m512i b = _mm512_cvtepi16_epi32(_mm256_set1_epi16(1));    // AVX512F
            __m512i c = _mm512_add_epi16(b, b);                         // AVX512BW
            __m512d d = _mm512_cvtepi64_pd(_mm512_set1_epi64(1));       // AVX512DQ
            float s = _mm512_reduce_add_ps(a) + _mm512_reduce_add_epi32(c) + _mm512_reduce_add_pd(d);
            return s == 136 + 32 + 8 ? 0 : 1;
        }" HAVE_AVX512)
    unset(CMAKE_REQUIRED_FLAGS)

    if(HAVE_AVX512)
        message(STATUS "AVX-512 supported, enabling it")
        list(APPEND FAX_DSP_OPTIONS ${AVX512_FLAGS})
    else()
        message(STATUS "AVX-512 not supported by this CPU or compiler, using generic code")
    endif()
endif()

configure_file(${PROJECT_SOURCE_DIR}/cmake/Version.h.in ${PROJECT_BINARY_DIR}/include/fax/Version.h @ONLY)

add_library(fax STATIC
    Decoder.cpp
    FileDecoder.cpp
    PgmWriter.cpp
    Wav.cpp
    FaxDecoder.cpp
    SlantEstimator.cpp
    avg.cpp
)
add_library(fax::fax ALIAS fax)

target_include_directories(fax
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(fax PUBLIC cxx_std_20)
set_target_properties(fax PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(fax PRIVATE ${FAX_DSP_OPTIONS})
# WAV files over 2 GB (e.g. 6 hours at 48 kHz) on 32-bit systems like Raspberry Pi OS
target_compile_definitions(fax PRIVATE _FILE_OFFSET_BITS=64)
