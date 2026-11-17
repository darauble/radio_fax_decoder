/*********************************************************************************
 *
 * Project:  FAX Decoder
 * Purpose:  measure the sample clock error of a recording from the image slant
 *
 **********************************************************************************

    If the recording's real sample rate differs from the nominal one, every line
    is a little too long or too short, so vertical features of the chart (frames,
    margins, borders) are slanted. The slant per line is the clock error.

    Method ("shift, add, measure sharpness"):
      - each demodulated line is reduced to WIDTH columns and turned into an edge
        signal |difference of neighbouring columns|, so flat areas don't count;
      - only the fine detail of its spectrum (bins FIRST_BIN.. of WIDTH/2, features
        of 2-4 columns: text, thin lines, frames) is kept, normalised to unit energy.
        Coarse structure (fronts, isobars, land) is often slightly slanted on the
        chart itself and biased the result by ~0.5 ppm on a test recording. In the
        frequency domain a horizontal shift by s is a phase ramp exp(j*2*pi*k*s/WIDTH),
        so trying a drift costs one complex multiply-add per line and bin;
      - for a candidate clock error p (ppm), line i is shifted back by i*p*1e-6 of
        a line and all lines are added up. Score = energy of the sum. Edges line up
        only for the right p, which gives the maximum;
      - lines that look nothing like their neighbours (noise, gaps between charts)
        are left out;
      - coarse to fine: the wide search (+-250 ppm) uses the 16 blocks of 256 lines
        with the most content, the fine search all lines in blocks of about one chart
        (2048 lines), then a parabola through the best 3 points. Blocks are aligned
        separately and their scores added.

    Memory: 2 KB per line. Above the budget every 2nd line is dropped (and again,
    if needed), which costs very little precision on such long recordings.
*/

#include "SlantEstimator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static_assert(SlantEstimator::FIRST_BIN + SlantEstimator::BINS <= SlantEstimator::WIDTH / 2, "bins above Nyquist");

SlantEstimator::SlantEstimator(size_t memory_budget)
{
    m_maxEntries = std::max<size_t>(2 * CHUNK, memory_budget / (2 * BINS * sizeof(float)));
    m_maxEntries &= ~1;     // even, so the line alignment survives Compact()

    m_pending.assign(2 * BINS, 0.0f);
    m_current.assign(2 * BINS, 0.0f);

    m_twr.resize(WIDTH / 2);
    m_twi.resize(WIDTH / 2);
    for (int32_t k = 0; k < WIDTH / 2; k++) {
        m_twr[k] = cos(-2 * M_PI * k / WIDTH);
        m_twi[k] = sin(-2 * M_PI * k / WIDTH);
    }
    m_rev.resize(WIDTH);
    int32_t bits = 0;
    while ((1 << bits) < WIDTH) bits++;
    for (int32_t i = 0; i < WIDTH; i++) {
        int32_t r = 0;
        for (int32_t b = 0; b < bits; b++)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        m_rev[i] = r;
    }
    m_fr.resize(WIDTH);
    m_fi.resize(WIDTH);
    m_cols.resize(WIDTH);
}

// edge spectrum of one line (bins FIRST_BIN..), normalised: out[0..BINS) real, out[BINS..2*BINS) imaginary
void SlantEstimator::Spectrum(const uint8_t *demod, int32_t spl, float *out)
{
    float *cols = m_cols.data();
    for (int32_t c = 0; c < WIDTH; c++) {
        int32_t s0 = (int64_t) spl * c / WIDTH;
        int32_t s1 = (int64_t) spl * (c + 1) / WIDTH;
        if (s1 <= s0) s1 = s0 + 1;
        int32_t sum = 0;
        for (int32_t s = s0; s < s1; s++)
            sum += demod[s];
        cols[c] = (float) sum / (s1 - s0);
    }

    // edge signal, in bit reversed order for the FFT
    for (int32_t c = 0; c < WIDTH; c++) {
        m_fr[m_rev[c]] = fabsf(cols[(c + 1) % WIDTH] - cols[c]);
        m_fi[m_rev[c]] = 0;
    }

    // radix-2 FFT
    float *xr = m_fr.data(), *xi = m_fi.data();
    for (int32_t len = 2; len <= WIDTH; len <<= 1) {
        const int32_t half = len / 2, step = WIDTH / len;
        for (int32_t i = 0; i < WIDTH; i += len) {
            for (int32_t k = 0; k < half; k++) {
                float wr = m_twr[k * step], wi = m_twi[k * step];
                float vr = xr[i + k + half] * wr - xi[i + k + half] * wi;
                float vi = xr[i + k + half] * wi + xi[i + k + half] * wr;
                xr[i + k + half] = xr[i + k] - vr;
                xi[i + k + half] = xi[i + k] - vi;
                xr[i + k] += vr;
                xi[i + k] += vi;
            }
        }
    }

    double energy = 0;
    for (int32_t k = FIRST_BIN; k < FIRST_BIN + BINS; k++)
        energy += (double) xr[k] * xr[k] + (double) xi[k] * xi[k];

    const float norm = energy > 0 ? 1.0 / sqrt(energy) : 0;
    for (int32_t k = 0; k < BINS; k++) {
        out[k] = xr[FIRST_BIN + k] * norm;
        out[BINS + k] = xi[FIRST_BIN + k] * norm;
    }
}

void SlantEstimator::AddLine(const uint8_t *demod, int32_t samples_per_line)
{
    Spectrum(demod, samples_per_line, m_current.data());

    // similarity to the previous line: |<a, b>| of unit vectors, ~0.06 for noise
    float coherence = 0;
    if (m_havePending) {
        double re = 0, im = 0;
        const float *a = m_current.data(), *b = m_pending.data();
        for (int32_t k = 0; k < BINS; k++) {
            re += a[k] * b[k] + a[BINS + k] * b[BINS + k];
            im += a[BINS + k] * b[k] - a[k] * b[BINS + k];
        }
        coherence = sqrt(re * re + im * im);

        // the previous line is now judged by both its neighbours
        if ((m_lines - 1) % m_stride == 0)
            Store(m_pending.data(), m_pendingCoherence >= MIN_COHERENCE || coherence >= MIN_COHERENCE);
    }

    std::swap(m_pending, m_current);
    m_pendingCoherence = coherence;
    m_havePending = true;
    m_lines++;
}

void SlantEstimator::FlushPending()
{
    if (m_havePending && (m_lines - 1) % m_stride == 0)
        Store(m_pending.data(), m_pendingCoherence >= MIN_COHERENCE);
    m_havePending = false;
}

void SlantEstimator::Store(const float *spec, bool used)
{
    if (m_entries == m_maxEntries)
        Compact();

    if (m_entries / CHUNK >= (int32_t) m_chunks.size())
        m_chunks.emplace_back(new float[(size_t) CHUNK * 2 * BINS]);

    float *e = Entry(m_entries);
    if (used)
        memcpy(e, spec, 2 * BINS * sizeof(float));
    else
        memset(e, 0, 2 * BINS * sizeof(float));     // contributes nothing to the sums

    m_used.push_back(used);
    m_entries++;
}

// keep every 2nd entry, so the stored lines fit the memory budget
void SlantEstimator::Compact()
{
    const int32_t keep = m_entries / 2;
    for (int32_t j = 0; j < keep; j++) {
        memcpy(Entry(j), Entry(2 * j), 2 * BINS * sizeof(float));
        m_used[j] = m_used[2 * j];
    }
    m_entries = keep;
    m_used.resize(keep);
    m_chunks.resize((keep + CHUNK - 1) / CHUNK);
    m_stride *= 2;
    fprintf(stdout, "Slant analysis: memory budget reached, keeping every %d. line\n", m_stride);
}

/*
    Energy of the lines added up, each shifted back by its line index * ppm.
    Horner's scheme: acc = acc * conj(z) + X[j] gives |sum X[j] * z^j|, one complex
    multiply-add per line and bin; the inner loop over the bins vectorises.
*/
double SlantEstimator::Score(double ppm, const Blocks &blocks)
{
    float zr[BINS], zi[BINS], ar[BINS], ai[BINS];
    for (int32_t k = 0; k < BINS; k++) {
        double theta = 2 * M_PI * (FIRST_BIN + k) * ppm * 1e-6 * m_stride;
        zr[k] = cos(theta);
        zi[k] = sin(theta);
    }

    double total = 0;
    for (const auto &blk : blocks) {
        for (int32_t k = 0; k < BINS; k++)
            ar[k] = ai[k] = 0;

        for (int32_t j = blk.first; j < blk.second; j++) {
            const float * __restrict__ er = Entry(j);
            const float * __restrict__ ei = er + BINS;
            for (int32_t k = 0; k < BINS; k++) {
                float nr = ar[k] * zr[k] + ai[k] * zi[k] + er[k];
                float ni = ai[k] * zr[k] - ar[k] * zi[k] + ei[k];
                ar[k] = nr;
                ai[k] = ni;
            }
        }

        for (int32_t k = 0; k < BINS; k++)
            total += (double) ar[k] * ar[k] + (double) ai[k] * ai[k];
    }
    return total;
}

// grid search around center, refined with a parabola through the best 3 points
double SlantEstimator::Search(double center, double half_range, double step, const Blocks &blocks, double *peak_ratio)
{
    const int32_t n = (int32_t) ceil(half_range / step);
    std::vector<double> score(2 * n + 1);
    for (int32_t i = -n; i <= n; i++)
        score[i + n] = Score(center + i * step, blocks);

    const int32_t m = std::max_element(score.begin(), score.end()) - score.begin();
    double best = center + (m - n) * step;

    if (m > 0 && m < 2 * n) {
        double y0 = score[m - 1], y1 = score[m], y2 = score[m + 1];
        double den = y0 - 2 * y1 + y2;
        if (den < 0)
            best += std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) * step;
    }

    if (peak_ratio) {
        std::vector<double> sorted(score);
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        double median = sorted[sorted.size() / 2];
        *peak_ratio = median > 0 ? score[m] / median : 0;
    }
    return best;
}

// up to 'count' blocks of 'len' entries with the most picture content, in time order
SlantEstimator::Blocks SlantEstimator::BestBlocks(int32_t len, int32_t count)
{
    std::vector<std::pair<int32_t, int32_t>> used;     // (used entries, block start)
    for (int32_t a = 0; a + len <= m_entries; a += len) {
        int32_t u = 0;
        for (int32_t j = a; j < a + len; j++)
            u += m_used[j];
        if (u >= len / 4)
            used.push_back({u, a});
    }
    std::stable_sort(used.begin(), used.end(), [](auto &x, auto &y) { return x.first > y.first; });
    if ((int32_t) used.size() > count)
        used.resize(count);

    Blocks blocks;
    for (auto &u : used)
        blocks.push_back({u.second, u.second + len});
    std::sort(blocks.begin(), blocks.end());
    return blocks;
}

SlantEstimator::Result SlantEstimator::Estimate()
{
    FlushPending();

    Result r;
    r.lines = m_lines;
    r.stride = m_stride;
    r.used_lines = std::count(m_used.begin(), m_used.end(), 1) * m_stride;
    if (std::count(m_used.begin(), m_used.end(), 1) < 32)
        return r;

    const double MAX_PPM = 250;
    // drift resolution of L lines: the peak is ~1e6 / (highest bin * L) ppm wide, sample it 4x
    auto step_for = [](double lines) { return 1e6 / (4.0 * (FIRST_BIN + BINS) * lines); };

    const int32_t total_lines = m_entries * m_stride;
    const int32_t WIDE_LINES = 256, WIDE_BLOCKS = 16;   // wide search: short blocks with the most content
    const int32_t CHART_LINES = 2048;                   // fine search: blocks of about one chart

    // Lines are aligned (added coherently) only within blocks of about one chart; the
    // blocks' scores are added. Features don't continue from one chart to the next, and
    // coherent sums across charts let their cross terms move the peak.
    auto chart_blocks = [&](int32_t a, int32_t b) {
        int32_t n = std::max<int32_t>(1, lround((double) (b - a) * m_stride / CHART_LINES));
        Blocks blocks;
        for (int32_t i = 0; i < n; i++)
            blocks.push_back({a + (int64_t) (b - a) * i / n, a + (int64_t) (b - a) * (i + 1) / n});
        return blocks;
    };

    double center = 0, prev_step = 0;
    bool wide = true;
    if (total_lines > 2 * WIDE_LINES) {
        Blocks blocks = BestBlocks(std::max(1, WIDE_LINES / m_stride), WIDE_BLOCKS);
        if (!blocks.empty()) {
            prev_step = step_for(WIDE_LINES);
            center = Search(0, MAX_PPM, prev_step, blocks, &r.confidence);
            wide = false;
        }
    }

    Blocks blocks = chart_blocks(0, m_entries);
    double step = step_for((double) m_entries * m_stride / blocks.size());
    center = Search(center, wide ? MAX_PPM : std::max(3 * prev_step, 3 * step), step, blocks,
                    wide ? &r.confidence : nullptr);
    r.ppm = center;
    r.valid = r.confidence >= MIN_CONFIDENCE;

    // the same on parts of the recording: shows a clock that drifts (e.g. warming up)
    const int32_t MIN_SEGMENT = 1000, SEGMENT = 7200;   // lines; 7200 = 1 hour at 120 LPM
    if (r.valid && total_lines >= 2 * MIN_SEGMENT) {
        int32_t nseg = std::clamp((int32_t) lround((double) total_lines / SEGMENT), 2, 24);
        for (int32_t i = 0; i < nseg; i++) {
            int32_t a = (int64_t) m_entries * i / nseg, b = (int64_t) m_entries * (i + 1) / nseg;
            Blocks sb = chart_blocks(a, b);
            double sstep = step_for((double) (b - a) * m_stride / sb.size());
            double p = Search(r.ppm, 20 * sstep, sstep, sb, nullptr);
            r.segments.push_back({a * m_stride, b * m_stride - 1, p});
        }
    }
    return r;
}
