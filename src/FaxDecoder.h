/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  weather fax Plugin
 * Author:   Sean D'Epagnier
 *
 ***************************************************************************
 *   Copyright (C) 2015 by Sean D'Epagnier                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,  USA.         *
 ***************************************************************************
 */
 
// adapted from github.com/jks-prv/Beagle_SDR_GPS/
// which is
// adapted from github.com/seandepagnier/weatherfax_pi
#pragma once
//#include "types.h"
#include "datatypes.h"
#include <stdint.h>

#define FAX_MSG_CLEAR   255
#define FAX_MSG_DRAW    254
#define FAX_MSG_SCOPE   0       // channel 0, 1, 2, 3

class FaxDecoder
{
public:

    struct firfilter {
        enum Bandwidth {NARROW, MIDDLE, WIDE};
        firfilter() {}
        firfilter(enum Bandwidth b) : bandwidth(b), current(0)
            { for(int i=0; i<17; i++) buffer[i] = 0; }
        enum Bandwidth bandwidth;
        float buffer[17];
        int32_t current;
    };

    FaxDecoder():
        m_rx_chan {0},
        m_fn {NULL},
        //int m_file,
        m_file {NULL},
        m_bEndDecoding {false},
        m_SamplesPerSec_nom {0.0},
        m_SamplesPerSec_frac {0.0},
        m_SamplesPerSec_frac_prev {0.0},
        m_SampleRateRatio {0.0}, m_fi {0.0},
        m_lineIncrFrac {0.0},
        m_lineIncrAcc {0.0},
        m_lineBlend {0.0},
        m_SamplesPerLine {0},
        m_BytesPerLine {0},
        Iprev {0.0},
        Qprev {0.0},
        m_samples {NULL},
        m_samp_idx{0},
        m_demod_data {NULL},
        m_imgdata {NULL},
        m_outImage {NULL},
        m_skip {0},
        m_imageline {0},
        m_fax_line {0}
    { 
        
    }
        
    ~FaxDecoder() { FreeImage(); CleanUpBuffers(); }

    bool Configure(int lpm, int32_t imagewidth, int32_t BitsPerPixel, int32_t carrier,
                   int32_t deviation, enum firfilter::Bandwidth bandwidth,
                   double minus_saturation_threshold, bool bIncludeHeadersInImages,
                   bool use_phasing, bool autostop, int32_t debug, bool reset, double sample_rate, double srcorr);

    void ProcessSamples(int16_t *samps, int32_t nsamps, float shift);
    void FileOpen(const char *);
    void FileWrite(uint8_t *data, int32_t datalen);
    void FileClose();
    
    bool DecodeFaxFromFilename();
    bool DecodeFaxFromDSP();
    bool DecodeFaxFromPortAudio();

    void InitializeImage();
    void FreeImage();

    uint8_t *m_imgdata, *m_outImage;
    int32_t m_imageline;
    int32_t m_imagewidth;
    double m_minus_saturation_threshold;

private:
    bool DecodeFaxLine();
    void DemodulateData();

    void SetupBuffers();
    void CleanUpBuffers();

    int32_t m_rx_chan;
    char *m_fn;
    //int m_file;
    FILE *m_file;
    int32_t m_fax_line;
    bool m_bEndDecoding;        /* flag to end decoding thread */
    double m_SamplesPerSec_nom;
    double m_SamplesPerSec_frac, m_SamplesPerSec_frac_prev;
    double m_SampleRateRatio, m_fi;
    double m_lineIncrFrac, m_lineIncrAcc, m_lineBlend;
    int32_t m_SamplesPerLine, m_skip;
    int32_t m_BytesPerLine;

    float Iprev, Qprev;
    int16_t *m_samples;
    int32_t m_samp_idx;
    uint8_t *m_demod_data;

    enum Header {IMAGE, START, STOP};

    float FourierTransformSub(uint8_t* buffer, int32_t samps_per_line, int32_t buffer_len, int32_t freq);
    Header DetectLineType(uint8_t* buffer, int32_t samps_per_line, int32_t buffer_len);
    void DecodeImageLine(uint8_t* buffer, int32_t buffer_len, uint8_t *image);
    int32_t FaxPhasingLinePosition(uint8_t *image, int32_t samplesPerLine);
    void UpdateSampleRate();

    /* fax settings */
    int32_t m_BitsPerPixel;
    double m_carrier, m_deviation;
    struct firfilter firfilters[2];
    bool m_bSkipHeaderDetection;
    bool m_bIncludeHeadersInImages;
    bool m_use_phasing;
    bool m_autostop, m_autostopped;
    int32_t m_imagecolors;
    int32_t m_lpm;
    bool m_bFM;
    int32_t m_Start_IOC576_Frequency, m_Start_IOC288_Frequency, m_StopFrequency;
    int32_t m_StartStopLength;
    int32_t m_phasingLines;
    int32_t m_offset;
    int32_t m_imgsize;

    int32_t height, imgpos;

    Header lasttype;
    int32_t typecount;

    bool gotstart;

    int32_t *phasingPos;
    int32_t phasingLinesLeft, phasingSkipData;
    bool have_phasing;
    int32_t m_debug;
};

// extern FaxDecoder m_FaxDecoder[MAX_RX_CHANS];
