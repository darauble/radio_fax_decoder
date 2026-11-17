// fax library: streaming decoder (wraps the internal FaxDecoder)

#include "fax/Decoder.h"

#include <climits>

#include "FaxDecoder.h"

namespace fax {

static FaxDecoder::firfilter::Bandwidth to_impl(Bandwidth b)
{
    switch (b) {
    case Bandwidth::Narrow: return FaxDecoder::firfilter::NARROW;
    case Bandwidth::Wide:   return FaxDecoder::firfilter::WIDE;
    default:                return FaxDecoder::firfilter::MIDDLE;
    }
}

Decoder::Decoder(const DecoderOptions &options, double sample_rate, LogHandler log)
    // the same expression the CLI always used for -s, so results stay bit-identical
    : Decoder(options, sample_rate, options.clock_ppm / 1000000 + 1, std::move(log))
{
}

Decoder::Decoder(const DecoderOptions &options, double sample_rate, double clock_factor, LogHandler log)
    : m_options(options), m_impl(std::make_unique<FaxDecoder>())
{
    m_impl->SetLog(std::move(log));
    m_impl->Configure(
        options.lpm,
        options.width,
        8,                              // bits per pixel
        (int32_t) options.carrier,
        (int32_t) options.deviation,
        to_impl(options.bandwidth),
        15.0,                           // minus_saturation_threshold (unused)
        options.include_headers,
        options.phasing,
        options.autostop,
        false,                          // debug
        false,                          // reset
        sample_rate,
        clock_factor,
        options.line_limit,
        options.decimate);
}

Decoder::~Decoder() = default;

void Decoder::set_line_sink(LineSink sink)
{
    m_impl->SetLineSink(std::move(sink));
}

void Decoder::set_slant_estimator(SlantEstimator *estimator)
{
    m_impl->SetSlantEstimator(estimator);
}

bool Decoder::push(const int16_t *samples, size_t count)
{
    while (count > 0) {
        int32_t n = count > (size_t) INT32_MAX ? INT32_MAX : (int32_t) count;
        if (!m_impl->ProcessSamples(samples, n, 0))
            return false;
        samples += n;
        count -= n;
    }
    return true;
}

int32_t Decoder::lines() const
{
    return m_impl->Lines();
}

} // namespace fax
