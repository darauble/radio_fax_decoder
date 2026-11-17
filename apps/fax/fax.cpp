/*********************************************************************************
 *
 * Project:  FAX Decoder
 * Purpose:  command line utility to decode weather fax from wav file
 * Author:   Darau, Blė
 *
 **********************************************************************************
 *   Copyright (C) 2023 by Darau, Blė                                             *
 *                                                                                *
 * Permission is hereby granted, free of charge, to any person obtaining a copy   *
 * of this software and associated documentation files (the "Software"), to deal  *
 * in the Software without restriction, including without limitation the rights   *
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell      *
 * copies of the Software, and to permit persons to whom the Software is          *
 * furnished to do so, subject to the following conditions:                       *
 *                                                                                *
 * The above copyright notice and this permission notice shall be included in all *
 * copies or substantial portions of the Software.                                *
 *                                                                                *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR     *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,       *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE    *
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER         *
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,  *
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE  *
 * SOFTWARE.                                                                      *
 *                                                                                *
 **********************************************************************************
 */

// Command line frontend of the fax library: options, messages, results.
// All decoding is in the library (include/fax), so other frontends (a GUI,
// fax-station) can do the same.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <getopt.h>

#include "fax/fax.h"

static void print_log(fax::LogLevel level, const std::string &msg)
{
    FILE *out = (level == fax::LogLevel::Warning || level == fax::LogLevel::Error) ? stderr : stdout;
    fprintf(out, "%s\n", msg.c_str());
}

static void print_clock_result(const fax::ClockResult &clock, int lpm, bool auto_scorr)
{
    const fax::SlantEstimator::Result &res = clock.analysis;
    const float line_ratio = 60.0 / lpm;
    const double given_factor = clock.given_ppm / 1000000 + 1;

    fprintf(stdout, "\nSlant analysis: %d lines, %d with picture content%s, confidence %.1f (min %.1f)\n",
            res.lines, res.used_lines, res.stride > 1 ? " (sparse)" : "", res.confidence,
            fax::SlantEstimator::MIN_CONFIDENCE);

    if (clock.valid) {
        for (auto &seg : res.segments) {
            fprintf(stdout, "  lines %6d - %6d (%6.1f - %6.1f min): -s %.2f\n", seg.first_line, seg.last_line,
                    seg.first_line * line_ratio / 60, (seg.last_line + 1) * line_ratio / 60,
                    (given_factor * (1 + seg.ppm * 1e-6) - 1) * 1e6);
        }
        fprintf(stdout, "Clock correction: -s %.2f", clock.clock_ppm);
        if (clock.given_ppm != 0)
            fprintf(stdout, "  (given -s %.2f, measured remaining %+.2f ppm)", clock.given_ppm, clock.residual_ppm);
        fprintf(stdout, "\n");
    } else {
        fprintf(stdout, "Clock correction: not found, the image has too little vertical structure%s\n",
                auto_scorr ? "; decoding with the given -s" : "");
    }
}

int main(int argc, char *const *argv)
{
    fprintf(stdout, "Radio Fax decoder v" FAX_VERSION "\n");

    const char *file_name = nullptr;
    fax::DecoderOptions options;
    fax::InputOptions input;

    int no_header {0};
    int no_phasing {0};
    int auto_stop {0};
    int remove_dc {0};
    int decimate {0};
    int find_scorr {0};
    int auto_scorr {0};

    static struct option long_options[] =
    {
        {"no_header",   no_argument,  &no_header, 1},
        {"remove_dc",   no_argument,  &remove_dc, 1},
        {"decimate",    no_argument,  &decimate, 1},
        {"find-scorr",  no_argument,  &find_scorr, 1},
        {"auto-scorr",  no_argument,  &auto_scorr, 1},
        {"auto_stop",   no_argument,  &auto_stop, 1},

        {"wav_file",    required_argument, 0, 'w'},
        {"center_freq", required_argument, 0, 'f'},
        {"lpm",         required_argument, 0, 'l'},
        {"srcorr",      required_argument, 0, 's'},
        {"drop",        required_argument, 0, 'd'},
        {"drop_lines",  required_argument, 0, 'r'},
        {"pixels",      required_argument, 0, 'p'},
        {"drop_pixels", required_argument, 0, 'x'},
        {"no_phasing",  no_argument,       0, 'n'},
        {"line_limit",  required_argument, 0, 'L'},
        {0, 0, 0, 0}
    };

    int opt_idx = 0;
    int c;

    while ((c = getopt_long(argc, argv, "w:f:l:s:d:r:x:p:nL:", long_options, &opt_idx)) >= 0) {
        switch (c) {
            case 'w': file_name = optarg; break;
            case 'f': options.carrier = atof(optarg); break;
            case 'l': options.lpm = atoi(optarg); break;
            case 's': options.clock_ppm = atof(optarg); break;
            case 'd': input.drop_samples += atol(optarg); break;
            case 'r': input.drop_lines += atol(optarg); break;
            case 'p': options.width = atoi(optarg); break;
            case 'x': input.drop_pixels += atoi(optarg); break;
            case 'n': no_phasing = 1; break;
            case 'L': options.line_limit = atoi(optarg); break;
        }
    }

    if (file_name == nullptr) {
        fprintf(stdout, "File name is required: -w <file name>\n");
        return EXIT_FAILURE;
    }
    if (options.lpm <= 0 || options.width <= 0) {
        fprintf(stderr, "LPM and pixels must be positive\n");
        return EXIT_FAILURE;
    }

    options.include_headers = !no_header;
    options.phasing = !no_phasing;
    options.autostop = auto_stop;
    options.decimate = decimate;
    input.remove_dc = remove_dc;

    // the image goes into the current directory, named after the recording
    std::filesystem::path pgm_name = std::filesystem::path(file_name).filename().stem();
    pgm_name += ".pgm";

    fax::Callbacks callbacks;
    callbacks.log = print_log;

    try {
        {
            fax::WavReader wav(file_name);
            fprintf(stdout, "Sample rate: %u\n", wav.info().sample_rate);
            fprintf(stdout, "   Channels: %u\n", wav.info().channels);
            fprintf(stdout, "        BPS: %u\n", wav.info().block_align);
        }

        if (find_scorr || auto_scorr) {
            fprintf(stdout, "Analysing the image slant...\n");
            fax::ClockResult clock = fax::find_clock_correction(file_name, options, input, callbacks);
            print_clock_result(clock, options.lpm, auto_scorr);

            if (!auto_scorr)
                return clock.valid ? 0 : 2;

            fprintf(stdout, "\nDecoding with -s %.2f...\n", clock.clock_ppm);
            fax::decode_file(file_name, pgm_name, options, clock, input, callbacks);
        } else {
            fax::decode_file(file_name, pgm_name, options, input, callbacks);
        }
    } catch (const fax::Error &e) {
        fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
