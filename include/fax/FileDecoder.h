// fax library: decoding recordings (WAV files)
//
// The high level API for frontends (the `fax` command line tool, a future GUI,
// fax-station). Blocking calls; run them in a worker thread for a responsive UI
// and use the progress callback to show progress or cancel.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "fax/Log.h"
#include "fax/Options.h"
#include "fax/SlantEstimator.h"
#include "fax/Wav.h"

namespace fax {

struct Progress {
    int32_t lines = 0;              // lines decoded or analysed so far
    double fraction = 0;            // of the recording read, 0..1
};

struct Callbacks {
    LogHandler log;
    std::function<bool(const Progress &)> progress;     // called about once per second of audio;
                                                        // return false to cancel
};

struct ClockResult {
    bool valid = false;             // false: no usable vertical structure, clock_ppm = given_ppm
    bool cancelled = false;
    double given_ppm = 0;           // options.clock_ppm the analysis ran with
    double residual_ppm = 0;        // measured on top of it
    double clock_ppm = 0;           // total correction to use (the CLI's -s value)
    double clock_factor = 1;        // the same as an exact factor of the sample rate
    SlantEstimator::Result analysis;
};

struct DecodeResult {
    int32_t lines = 0;              // lines written
    bool cancelled = false;
    double clock_ppm = 0;           // correction used
};

// Measure the sample clock error from the image slant (one analysis pass, no image).
ClockResult find_clock_correction(const std::string &wav_path, const DecoderOptions &options,
                                  const InputOptions &input = {}, const Callbacks &callbacks = {});

// Decode to a PGM image with options.clock_ppm.
DecodeResult decode_file(const std::string &wav_path, const std::string &pgm_path,
                         const DecoderOptions &options, const InputOptions &input = {},
                         const Callbacks &callbacks = {});

// Decode with a measured correction (if clock.valid; otherwise with options.clock_ppm).
DecodeResult decode_file(const std::string &wav_path, const std::string &pgm_path,
                         const DecoderOptions &options, const ClockResult &clock,
                         const InputOptions &input = {}, const Callbacks &callbacks = {});

// Samples skipped at the start for the given input options (drop_samples + lines + pixels).
long drop_samples(const InputOptions &input, const DecoderOptions &options, uint32_t sample_rate);

} // namespace fax
