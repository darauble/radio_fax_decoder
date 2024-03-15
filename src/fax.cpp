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
#define VERSION "1.0.3"
#include <cstdio>
#include <cstring>
#include <filesystem>

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>

#include "FaxDecoder.h"

struct wav_header_t {
    char signature[4];            // "RIFF"
    uint32_t fileSize;            // data bytes + sizeof(WavHeader_t) - 8
    char file_type[4];            // "WAVE"
    char format_marker[4];        // "fmt "
    uint32_t format_header_length;// Always 16
    uint16_t sample_type;         // PCM (1)
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t bytes_per_second;
    uint16_t bytes_per_sample;
    uint16_t bit_depth;
    char data_marker[4];          // "data"
    uint32_t data_size;
};

int main(int argc, char *const * argv)
{
    fprintf(stdout, "Radio Fax decoder v" VERSION "\n");

    char *file_name = NULL;
    double center_freq = 1900;
    int lpm = 120;
    double srcorr = 1.0;
    long drop = 0;
    int pixels_width = 1809;

    int no_header = 0;
    int no_phasing = 0;
    int auto_stop = 0;
    int remove_dc = 0;

    static struct option long_options[] =
    {
        {"no_header",   no_argument,  &no_header, 1},
        {"no_phasing",  no_argument,  &no_phasing, 1},
        {"remove_dc",   no_argument,  &remove_dc, 1},
        {"auto_stop",   auto_stop,    &auto_stop, 1},

        {"wav_file",    required_argument, 0, 'w'},
        {"center_freq", required_argument, 0, 'f'},
        {"lpm",         required_argument, 0, 'l'},
        {"srcorr",      required_argument, 0, 's'},
        {"drop",        required_argument, 0, 'd'},
        {"pixels",      required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    int opt_idx = 0;
    int8_t c;

    while(1) {
        c = getopt_long(argc, argv, "w:f:l:s:d:", long_options, &opt_idx);

        if (c < 0) {
            break;
        }

        switch(c) {
            case 'w':
                file_name = optarg;
            break;

            case 'f':
                center_freq = atof(optarg);
            break;

            case 'l':
                lpm = atoi(optarg);
            break;

            case 's':
                srcorr = atof(optarg) / 1000000 + 1;
            break;

            case 'd':
                drop = atol(optarg);
            break;

            case 'p':
                pixels_width = atoi(optarg);
                printf("Pixels width set to %d\n", pixels_width);
            break;
        }
    }

    if (file_name == NULL) {
        fprintf(stdout, "File name is required: -w <file name>\n");
        exit(-1);
    }

    std::filesystem::path full_path = file_name;
    std::filesystem::path local_name = full_path.filename().stem();
    local_name +=  ".pgm";
    
    FILE *fd = fopen(file_name, "r");
    
    if (fd == NULL) {
        fprintf(stderr, "open(%s) failed: %s\n", file_name, strerror(errno));
        exit(EXIT_FAILURE);
    }

    wav_header_t hdr;
    
    auto nread = fread(&hdr, sizeof(wav_header_t), 1, fd);

    fprintf(stdout, "Sample rate: %d\n", hdr.sample_rate);
    fprintf(stdout, "   Channels: %d\n", hdr.channels);
    fprintf(stdout, "        BPS: %d\n", hdr.bytes_per_sample);


    int read_buf_size = hdr.sample_rate * hdr.channels;
    auto readbuf = new short[read_buf_size];
    short *inbuf;

    if (hdr.channels > 1) {
        inbuf = new short[hdr.sample_rate];
    }

    FaxDecoder faxdec;

    faxdec.Configure(
        lpm,
        pixels_width,
        8,
        center_freq,
        400,
        FaxDecoder::firfilter::MIDDLE,     // bandwidth
        15.0,       // double minus_saturation_threshold
        !no_header,       // bool bIncludeHeadersInImages
        !no_phasing, // Phasing
        auto_stop, // Autostop, not very useful, can cause dropouts in long faxes
        false, // Debug
        false, // reset
        hdr.sample_rate,
        srcorr
    );

    faxdec.FileOpen(local_name.c_str());

    if (drop) {
        fseek(fd, ftell(fd) + (drop * sizeof(short)), SEEK_SET);
    }

    while (true) {
        nread = fread(readbuf,  sizeof(short), hdr.sample_rate,fd);
        
        if (nread < 0) {
            fprintf(stderr, "read() failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (nread == 0)
            break;
        
        if (hdr.channels == 1) {
            inbuf = readbuf;
        } else {
            for (int i = 0; i < nread / hdr.channels; i++) {
                inbuf[i] = readbuf[i * hdr.channels];
            }
        }

        if (remove_dc) {
            int64_t sum = 0;

            for (int i = 0; i < nread / hdr.channels; i++) {
                sum += inbuf[i];
            }

            short avg = (short) (sum / (nread / hdr.channels));

            for (int i = 0; i < nread / hdr.channels; i++) {
                inbuf[i] -= avg;
            }
        }
        // fprintf(stdout, "Start processing...\n");
        faxdec.ProcessSamples(inbuf, int(nread / hdr.channels), 0);
        // fprintf(stdout, "process...\n");
    }

    faxdec.FileClose();

    fclose(fd);
    
    delete readbuf;

    if (hdr.channels > 1) {
        delete inbuf;
    }

    return 0;
}
