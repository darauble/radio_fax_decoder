// fax library: decoding options
#pragma once

#include <cstdint>

namespace fax {

enum class Bandwidth { Narrow, Middle, Wide };     // demodulator low pass filter (ACfax)

struct DecoderOptions {
    int lpm = 120;                  // lines per minute (120, 60, ...)
    int width = 1809;               // pixels per output line (IOC 576: 576 * pi)
    double carrier = 1900;          // Hz, centre of black (1500) .. white (2300); used as whole Hz
    double deviation = 400;         // Hz, black/white distance from the carrier
    Bandwidth bandwidth = Bandwidth::Middle;
    bool include_headers = true;    // keep START/phasing lines in the image
    bool phasing = true;            // align the line start from the phasing signal
    bool autostop = false;          // stop at a STOP signal (can drop real lines)
    bool decimate = false;          // reduce high sample rates to ~12 kHz first (faster)
    double clock_ppm = 0;           // sample clock correction (the CLI's -s), ppm
    int32_t line_limit = 0;         // stop after this many lines, 0 = no limit
};

// How a recording is read (file decoding only)
struct InputOptions {
    long drop_samples = 0;          // skip this many samples at the start...
    long drop_lines = 0;            // ...plus this many lines' worth
    long drop_pixels = 0;           // ...plus this many pixels' worth (aligns the image horizontally)
    bool remove_dc = false;         // subtract the DC offset of each read block
};

} // namespace fax
