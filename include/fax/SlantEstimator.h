/*********************************************************************************
 *
 * Project:  FAX Decoder
 * Purpose:  measure the sample clock error of a recording from the image slant
 *
 * See SlantEstimator.cpp for the method.
 *
 **********************************************************************************
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "fax/Log.h"

namespace fax {

class SlantEstimator
{
public:
    static constexpr int32_t WIDTH = 1024;      // analysis columns per line (FFT size)
    static constexpr int32_t FIRST_BIN = 256;   // spectrum bins kept per line: FIRST_BIN..FIRST_BIN+BINS-1,
    static constexpr int32_t BINS = 256;        // i.e. only fine detail, see SlantEstimator.cpp

    struct Segment {
        int32_t first_line, last_line;
        double ppm;
    };

    struct Result {
        bool valid = false;
        double ppm = 0;             // clock error in the decoder's -s convention (ppm)
        double confidence = 0;      // peak / median of the wide search, ~1 = no slant visible
        int32_t lines = 0;          // lines analysed
        int32_t used_lines = 0;     // of them with picture content
        int32_t stride = 1;         // 1 = every line kept, 2 = every 2nd (long recordings)...
        std::vector<Segment> segments;  // the same measured on parts of a long recording
    };

    // memory_budget limits the stored line data; long recordings keep every 2nd, 4th... line
    explicit SlantEstimator(size_t memory_budget = 64u << 20);

    // one demodulated line (0..255 per sample), as the decoder produces it
    void AddLine(const uint8_t *demod, int32_t samples_per_line);

    // call once, after the last line
    Result Estimate();

    void SetLog(LogHandler log) { m_log = std::move(log); }

    static constexpr double MIN_CONFIDENCE = 2.0;

private:
    typedef std::vector<std::pair<int32_t, int32_t>> Blocks;    // [first, last) entries

    static constexpr int32_t CHUNK = 1024;      // entries per allocation
    static constexpr float MIN_COHERENCE = 0.3f;

    float *Entry(int32_t j) { return m_chunks[j / CHUNK].get() + (size_t) (j % CHUNK) * 2 * BINS; }

    void Spectrum(const uint8_t *demod, int32_t spl, float *out);
    void Store(const float *spec, bool used);
    void Compact();
    void FlushPending();
    double Score(double ppm, const Blocks &blocks);
    double Search(double center, double half_range, double step, const Blocks &blocks, double *peak_ratio);
    Blocks BestBlocks(int32_t len, int32_t count);

    int32_t m_maxEntries;
    std::vector<std::unique_ptr<float[]>> m_chunks;
    std::vector<uint8_t> m_used;
    int32_t m_entries = 0, m_stride = 1, m_lines = 0;

    // a line is judged by its similarity to both neighbours, so it is stored one line late
    std::vector<float> m_pending, m_current;
    float m_pendingCoherence = 0;
    bool m_havePending = false;

    // FFT work
    std::vector<float> m_twr, m_twi, m_fr, m_fi, m_cols;
    std::vector<int32_t> m_rev;

    LogHandler m_log;
};

} // namespace fax
