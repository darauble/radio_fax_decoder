// fax library: streaming decoder
//
// Push audio in any amount; finished image lines come out through the line sink.
// This is the building block for live decoding (e.g. audio from a KiwiSDR).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "fax/Log.h"
#include "fax/Options.h"

class FaxDecoder;   // implementation, src/FaxDecoder.h

namespace fax {

class SlantEstimator;

class Decoder {
public:
    // one image line: options.width pixels, 0 = black .. 255 = white
    using LineSink = std::function<void(const uint8_t *pixels, int width)>;

    // sample_rate: nominal rate of the audio (mono 16-bit). The clock correction is
    // options.clock_ppm. The log handler gets the configuration messages too.
    Decoder(const DecoderOptions &options, double sample_rate, LogHandler log = {});
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    void set_line_sink(LineSink sink);

    // Analysis mode: lines go to the estimator instead of the sink. Use with
    // phasing and autostop off, their jumps would disturb the measurement.
    void set_slant_estimator(SlantEstimator *estimator);

    // Returns false when decoding is finished (line limit reached); further samples are ignored.
    // For identical results, push the same block sizes (decode_file uses <= 1 s blocks).
    bool push(const int16_t *samples, size_t count);

    int32_t lines() const;          // lines produced so far
    const DecoderOptions &options() const { return m_options; }

private:
    friend class FileDecoderAccess;
    Decoder(const DecoderOptions &options, double sample_rate, double clock_factor, LogHandler log);

    DecoderOptions m_options;
    std::unique_ptr<::FaxDecoder> m_impl;
};

} // namespace fax
