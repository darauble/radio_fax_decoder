// fax library: decoding recordings (WAV files)

#include "fax/FileDecoder.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <new>

#include "avg.h"
#include "fax/Decoder.h"
#include "fax/PgmWriter.h"

namespace fax {

// Creates decoders with an exact clock factor (the public constructor takes ppm)
class FileDecoderAccess {
public:
    static std::unique_ptr<Decoder> make(const DecoderOptions &options, double sample_rate, double factor,
                                         const LogHandler &log)
    {
        return std::unique_ptr<Decoder>(new Decoder(options, sample_rate, factor, log));
    }
};

namespace {

void log(const Callbacks &cb, LogLevel level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void log(const Callbacks &cb, LogLevel level, const char *fmt, ...)
{
    if (!cb.log)
        return;
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    cb.log(level, msg);
}

double factor_of(double ppm)
{
    return ppm / 1000000 + 1;       // as the CLI always computed -s
}

struct AlignedDelete {
    void operator()(int16_t *p) const { operator delete(p, std::align_val_t(64)); }
};

// An opened recording with its read buffer
struct Input {
    Input(const std::string &path, const DecoderOptions &options, const Callbacks &cb) : wav(path)
    {
        const WavInfo &info = wav.info();
        if (info.channels != 1)
            throw Error("FAX Decoder only supports MONO (1 channel) WAV files.");

        // Read blocks of whole seconds, ~1 MB (10 MB without phasing). --remove_dc works
        // per block, so this sizing is kept as it always was, for identical results.
        int buf_size_b = 1048576;
        if (!options.phasing)
            buf_size_b *= 10;
        size = ((int) (((float) (buf_size_b / sizeof(int16_t)) / info.sample_rate))) * info.sample_rate;
        if (size == 0)
            size = info.sample_rate;
        log(cb, LogLevel::Debug, "read_buf_size: %zu", size);

        buffer.reset((int16_t *) operator new(sizeof(int16_t) * size, std::align_val_t(64)));
    }

    WavReader wav;
    size_t size;
    std::unique_ptr<int16_t, AlignedDelete> buffer;
};

// Feed the recording (after the dropped samples) to the decoder. Returns false if cancelled.
bool decode_pass(Decoder &decoder, Input &in, long drop, bool remove_dc, const Callbacks &cb)
{
    const uint32_t sample_rate = in.wav.info().sample_rate;
    int16_t *readbuf = in.buffer.get();
    size_t nread;

    in.wav.seek(drop > 0 ? drop : 0);

    while ((nread = in.wav.read(readbuf, in.size)) > 0) {
        if (remove_dc) {
            float avg = FLOAT_AVERAGE(readbuf, nread);
            SAMPLES_SUBTRACT(readbuf, nread, avg);
        }

        // whole seconds per call, as always (the resampler's rounding depends on it)
        for (size_t i = 0; i < nread; i += sample_rate) {
            size_t sample_length = (nread - i < sample_rate) ? nread - i : sample_rate;

            if (!decoder.push(&readbuf[i], sample_length))
                return true;    // finished (line limit)

            if (cb.progress && !cb.progress(Progress{decoder.lines(), in.wav.position()}))
                return false;
        }
    }
    return true;
}

DecodeResult decode(const std::string &wav_path, const std::string &pgm_path, const DecoderOptions &options,
                    double factor, const InputOptions &input, const Callbacks &cb)
{
    Input in(wav_path, options, cb);
    const uint32_t rate = in.wav.info().sample_rate;

    auto decoder = FileDecoderAccess::make(options, rate, factor, cb.log);

    PgmWriter pgm(pgm_path, options.width);
    log(cb, LogLevel::Debug, "FAX open %s", pgm_path.c_str());
    decoder->set_line_sink([&pgm](const uint8_t *pixels, int) { pgm.write_line(pixels); });

    DecodeResult result;
    result.cancelled = !decode_pass(*decoder, in, drop_samples(input, options, rate), input.remove_dc, cb);
    result.lines = pgm.lines();
    result.clock_ppm = (factor - 1) * 1e6;

    if (pgm.lines() > PgmWriter::MAX_LINES)
        log(cb, LogLevel::Warning, "height limited to %d!", PgmWriter::MAX_LINES);
    pgm.close();
    log(cb, LogLevel::Debug, "FAX wrote %d lines", result.lines);
    return result;
}

} // namespace

long drop_samples(const InputOptions &input, const DecoderOptions &options, uint32_t sample_rate)
{
    // the same types and order of operations as the CLI always had (float!), for identical results
    long drop = input.drop_samples;
    float line_ratio = 60.0 / options.lpm;

    if (input.drop_lines)
        drop += sample_rate * input.drop_lines * line_ratio;

    if (input.drop_pixels)
        drop += (long) ((float) input.drop_pixels / (uint32_t) options.width * sample_rate * line_ratio);

    return drop;
}

ClockResult find_clock_correction(const std::string &wav_path, const DecoderOptions &options,
                                  const InputOptions &input, const Callbacks &cb)
{
    Input in(wav_path, options, cb);
    const uint32_t rate = in.wav.info().sample_rate;
    const double factor = factor_of(options.clock_ppm);

    SlantEstimator estimator;
    estimator.SetLog(cb.log);

    // phasing and autostop jumps would break the slant measurement
    DecoderOptions analysis = options;
    analysis.phasing = false;
    analysis.autostop = false;

    ClockResult result;
    result.given_ppm = options.clock_ppm;
    result.clock_ppm = options.clock_ppm;
    result.clock_factor = factor;
    {
        auto analyser = FileDecoderAccess::make(analysis, rate, factor, cb.log);
        analyser->set_slant_estimator(&estimator);
        if (!decode_pass(*analyser, in, drop_samples(input, options, rate), input.remove_dc, cb)) {
            result.cancelled = true;
            return result;
        }
    }

    result.analysis = estimator.Estimate();
    result.valid = result.analysis.valid;
    if (result.valid) {
        result.residual_ppm = result.analysis.ppm;
        result.clock_factor = factor * (1 + result.residual_ppm * 1e-6);
        result.clock_ppm = (result.clock_factor - 1) * 1e6;
    }
    return result;
}

DecodeResult decode_file(const std::string &wav_path, const std::string &pgm_path, const DecoderOptions &options,
                         const InputOptions &input, const Callbacks &cb)
{
    return decode(wav_path, pgm_path, options, factor_of(options.clock_ppm), input, cb);
}

DecodeResult decode_file(const std::string &wav_path, const std::string &pgm_path, const DecoderOptions &options,
                         const ClockResult &clock, const InputOptions &input, const Callbacks &cb)
{
    return decode(wav_path, pgm_path, options, clock.valid ? clock.clock_factor : factor_of(options.clock_ppm),
                  input, cb);
}

} // namespace fax
