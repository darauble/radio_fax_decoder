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

//#include "types.h"
#include "FaxDecoder.h"
#include "mem.h"

#include <math.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FAX_PRINTF
#ifdef FAX_PRINTF
	#define faxprintf(fmt, ...) \
		fprintf(stdout, fmt, ## __VA_ARGS__)
#else
	#define faxprintf(fmt, ...)
#endif

/* Note: the decoding algorithms are adapted from yahfax (on sourceforge)
   which was an improved adaptation of hamfax. */

// Narrow, middle and wide FIR low pass filters from ACfax.
// These 17 taps are only meaningful at the sample rate they were made for:
// a digital filter's cutoff is a fraction of the sample rate, not a fixed
// frequency in Hz. They come to us via KiwiSDR, whose audio runs at 12000 Hz
// (MIDDLE: -3 dB at ~1300 Hz, -42 dB at 3800 Hz).
#define FIR_REF_TAP_COUNT 17
#define FIR_REF_SAMPLE_RATE 12000.0
static const float lpfcoeff[3][FIR_REF_TAP_COUNT] = {
      { -7, -18, -15,  11,  56, 116, 177, 223, 240, 223, 177, 116,  56,  11, -15, -18,  -7},
      {  0, -18, -38, -39,   0,  83, 191, 284, 320, 284, 191,  83,   0, -39, -38, -18,   0},
      {  6,  20,   7, -42, -74, -12, 159, 353, 440, 353, 159, -12, -74, -42,   7,  20,   6}
};

static double sinc(double x)
{
    return (x == 0.0) ? 1.0 : sin(M_PI * x) / (M_PI * x);
}

/*
   Build the filter for the actual sample rate. The reference taps are fine at
   12000 Hz and below, but used as is at 48000 Hz they reach ~4x higher (-3 dB at ~5200 Hz)
   and let through the 2*carrier (3800 Hz) mixing product, which ruins the
   FM discriminator and makes the image dark.

   So treat the reference taps as samples of a continuous impulse response
   taken at 12000 Hz, and resample it (band limited, sinc interpolation) at the
   real sample rate. The filter gets proportionally more taps, but keeps the same
   response in Hz. At or below the reference rate the original taps are used.
*/
FaxDecoder::firfilter::firfilter(enum Bandwidth b, double sample_rate) : bandwidth(b)
{
    const float *ref = lpfcoeff[b];

    if (sample_rate <= FIR_REF_SAMPLE_RATE) {
        coeffs.assign(ref, ref + FIR_REF_TAP_COUNT);
    } else {
        const int32_t ref_center = FIR_REF_TAP_COUNT / 2;
        const double step = FIR_REF_SAMPLE_RATE / sample_rate;   // in reference taps
        const int32_t half = (int32_t) ceil(ref_center / step);
        double ref_sum = 0, sum = 0;

        for (int32_t k = 0; k < FIR_REF_TAP_COUNT; k++)
            ref_sum += ref[k];

        for (int32_t n = -half; n <= half; n++) {
            double t = ref_center + n * step;
            double h = 0;
            for (int32_t k = 0; k < FIR_REF_TAP_COUNT; k++)
                h += ref[k] * sinc(t - k);
            coeffs.push_back((float) h);
            sum += h;
        }

        // keep the same DC gain as the reference filter
        for (auto &c : coeffs)
            c = (float) (c * ref_sum / sum);
    }
}

int qsort_intcomp(const void *elem1, const void *elem2)
{
	const int32_t i1 = *(const int32_t *) elem1, i2 = *(const int32_t *) elem2;
	return i1 - i2;
}

int median_i(int *i, int32_t len, int32_t *pct_1, int32_t *pct_2)
{
     qsort(i, len, sizeof *i, qsort_intcomp);
     if (pct_1) *pct_1 = i[(int) (len * ((float) *pct_1 / 100.0))];
     if (pct_2) *pct_2 = i[(int) (len * ((float) *pct_2 / 100.0))];
     return i[len/2];
}


/*void FaxDecoder::UpdateSampleRate()
{
    m_SamplesPerSec_frac = ext_update_get_sample_rateHz(m_rx_chan);
    m_SamplesPerSec_nom = snd_rate;
    m_SampleRateRatio = m_SamplesPerSec_frac / m_SamplesPerSec_nom;
}*/

/* perform fourier transform at a specific frequency to look for start/stop */
float FaxDecoder::FourierTransformSub(uint8_t* buffer, int32_t samps_per_line, int32_t buffer_len, int32_t freq)
{
    int32_t i, n;
    float k = -2 * M_PI * freq * 60.0 / m_lpm / samps_per_line;
    float retr = 0, reti = 0;
    
    for (n=0, i=1; n < buffer_len; n++, i++) {
        retr += buffer[n]*MCOS(k*n);
        reti += buffer[n]*MSIN(k*n);
    }
    return MSQRT(retr*retr + reti*reti);
}

/* see if the fourier transform at the start and stop frequencies reveils header */
FaxDecoder::Header FaxDecoder::DetectLineType(uint8_t* buffer, int32_t samps_per_line, int32_t buffer_len)
{
     const int32_t threshold = 5; /* 5 is pretty arbitrary but works in practice even with lots of noise */
     float start_det = FourierTransformSub(buffer, samps_per_line, buffer_len, m_Start_IOC576_Frequency) / buffer_len;
     float stop_det = FourierTransformSub(buffer, samps_per_line, buffer_len, m_StopFrequency) / buffer_len;
    faxprintf("FAX start_det=%.2f stop_det=%.2f\n", start_det, stop_det);

     if (start_det > threshold)
         return START;
     if (stop_det > threshold)
         return STOP;
     return IMAGE;
}

/* detect start position from phasing line
   using 7% of the image (image should have 5% black 95% white)
   with a ^ shaped wedge, find positon it fits to the minimum.

   This isn't very fast, but only is used for phasing lines */
int FaxDecoder::FaxPhasingLinePosition(uint8_t *image, int32_t samplesPerLine)
{
    int32_t n = samplesPerLine * .07;
    int32_t i;
    int32_t mintotal = -1, min = 0;
    
    // reduce computational load by making loops below sparse 
    #define PIXEL_RESOLUTION 4
    int32_t sampsIncr = samplesPerLine/m_imagewidth * PIXEL_RESOLUTION;
    
    //u4_t start = timer_us();
    for (i = 0; i<samplesPerLine; i += sampsIncr) {
        int32_t total = 0, j;
        for (j = 0; j<n; j += PIXEL_RESOLUTION)
            total += (n/2 - abs(j-n/2)) * (255 - image[(i+j) % samplesPerLine]);
        if (total < mintotal || mintotal == -1) {
            mintotal = total;
            min = i;
        }
    }

    // faxprintf("FAX PhasingLinePosition iter=%d usec=%u\n", samplesPerLine/sampsIncr*n/PIXEL_RESOLUTION, timer_us()-start);
    faxprintf("FAX PhasingLinePosition iter=%d\n", samplesPerLine/sampsIncr*n/PIXEL_RESOLUTION);
    return (samplesPerLine? ((min+n/2) % samplesPerLine) : 0);
}

bool FaxDecoder::DecodeFaxLine()
{
    const int32_t phasingSkipLines = 2;
    
    DemodulateData();

    enum Header type;
    if (m_bSkipHeaderDetection) {
        type = IMAGE;
    } else {
        // processing all the line samples for low LPM is too expensive
        int32_t buffer_len = MIN(m_SamplesPerLine, 3000);
        type = DetectLineType(m_demod_data, m_SamplesPerLine, buffer_len);
    }

    /* accumulate how many start or stop lines we are getting */
    if (type == lasttype && type != IMAGE)
        typecount++;
    else {
        typecount--; /* can deal with noisy input if we had a miss rather than reset here */
        if (typecount < 0)
            typecount = 0;
    }
    lasttype = type;

    if (type != IMAGE) { /* if type is start or stop */
        /* require 4 less lines than there really are to handle
           noise and also misalignment on first and last lines */
        const int32_t leewaylines = 4;

        faxprintf("FAX L%d %s cnt=%d prepare=%d\n", m_imageline, (type == START)? "START":"STOP", typecount,
            typecount == m_StartStopLength*2 - leewaylines);
        if (typecount == m_StartStopLength*2 - leewaylines) {
            if (type == START /* && m_imageline < 100 */) {
                /* prepare for phasing */
                /* image start detected, reset image at 0 lines  */
                if (!m_bIncludeHeadersInImages) {
                    m_imageline = 0;
                    imgpos = 0;
                    m_lineIncrAcc = 0;
                }

                phasingLinesLeft = m_phasingLines;
                phasingSkipData = 0;
                have_phasing = false;
                if (m_autostopped) {
                    // ext_send_msg(m_rx_chan, false, "EXT fax_autostopped=0");
                    m_autostopped = false;
                    faxprintf("FAX L%d AUTOSTOPPED=0\n", m_imageline);
                }
            } else {
                // type == STOP
                if (m_autostop) {
                    // ext_send_msg(m_rx_chan, false, "EXT fax_autostopped=1");
                    m_autostopped = true;
                    faxprintf("FAX L%d AUTOSTOPPED=1\n", m_imageline);
                }
            }
        }
    }

    /* throw away first 2 lines of phasing because we are not sure
       if they are misaligned start lines */
    if (m_use_phasing && phasingLinesLeft > 0 && phasingLinesLeft <= m_phasingLines - phasingSkipLines) {
        phasingPos[phasingLinesLeft-1] = FaxPhasingLinePosition(m_demod_data, m_SamplesPerLine);
        faxprintf("FAX L%d phasingPos[%d]=%d\n", m_imageline, phasingLinesLeft-1, phasingPos[phasingLinesLeft-1]);
    }

    if (m_use_phasing && type == IMAGE && phasingLinesLeft >= -phasingSkipLines) {
        if (--phasingLinesLeft == 0) {  /* decrement each phasing line */
        
            // DetectLineType() sometimes returns START during IMAGEs with sparse black text on a white background.
            // If the extension was started in the middle of a fax transmission this can result in a false phasing align.
            // Filter that out by looking at the 10%/90% distribution width of the phasing data.
            int32_t ten_pct = 10, ninety_pct = 90;
            phasingSkipData = median_i(phasingPos, m_phasingLines - phasingSkipLines, &ten_pct, &ninety_pct);
            fprintf(stdout, "FAX L%d SET phasingSkipData=%d 10%%=%d 90%%=%d\n", m_imageline, phasingSkipData, ten_pct, ninety_pct);
            if ((ninety_pct - ten_pct) > m_SamplesPerLine/6) {
                faxprintf("FAX L%d BAD phasingSkipData\n", m_imageline);
                phasingSkipData = 0;
            }
        }
    }

    /* go past the phasing lines we are skipping to make sure we are in the image */
    if (m_bIncludeHeadersInImages || !m_use_phasing || (type == IMAGE && phasingLinesLeft < -phasingSkipLines)) {
        if (m_imageline >= height) {
            height *= 2;
            m_imgdata = (uint8_t*) kiwi_irealloc("DecodeFaxLine", m_imgdata, m_imagewidth*height*m_imagecolors);
            fprintf(stdout, "Kiwi realloc %d, %d, %d\n", m_imagewidth, height, m_imagecolors);
        }

        /*
            if (m_imageline == 10) {
                m_autostopped = true;
                ext_send_msg(m_rx_chan, false, "EXT fax_autostopped=1");
                faxprintf("FAX L%d TEST AUTOSTOPPED=1\n", m_imageline);
            }
            if (m_imageline == 20) {
                m_autostopped = false;
                ext_send_msg(m_rx_chan, false, "EXT fax_autostopped=0");
                faxprintf("FAX L%d TEST AUTOSTOPPED=0\n", m_imageline);
            }
        */

        if (!m_autostopped)
            DecodeImageLine(m_demod_data, m_SamplesPerLine, m_imgdata+imgpos);
        
        // fprintf(stdout, "Line decoded: %d\n", m_SamplesPerLine);

        phasingSkipData %= m_SamplesPerLine;

        if (phasingSkipData && m_use_phasing && !have_phasing) {
            m_skip = phasingSkipData;
            have_phasing = true;
            // ext_send_msg(m_rx_chan, false, "EXT fax_phased");
            // faxprintf("FAX L%d USE phasingSkipData=%d\n", m_imageline, phasingSkipData);
            faxprintf("FAX L%d USE phasingSkipData=%d\n", m_fax_line, phasingSkipData);
        }
        
        

        imgpos += m_imagewidth*m_imagecolors;
        m_imageline++;

        // fprintf(stdout, "Line decoded 3\n");
    }

    return true;
}

/*
    FM demodulation of one line (m_SamplesPerLine samples).

    Done in stages over whole arrays rather than sample by sample, so that
    the compiler can vectorise every loop (SSE/AVX/AVX-512 on x86, NEON on ARM)
    without hand-written intrinsics:
      1. carrier phase of every sample
      2. mix down to baseband (cos/sin are vectorised by glibc's libmvec with -Ofast)
      3. low pass filter I and Q
      4. FM discriminator, scaling and clamping to pixel values
*/
void FaxDecoder::DemodulateData()
{
    const int32_t n = m_SamplesPerLine;
    const int32_t taps = m_lpf.coeffs.size();
    const int32_t hist = taps - 1;

    float * __restrict__ cycle = m_carrierCycle.data();
    float * __restrict__ mixI = m_mixI.data();      // [0, hist) previous line, [hist, hist+n) this line
    float * __restrict__ mixQ = m_mixQ.data();
    float * __restrict__ filtI = m_filtI.data();    // [0] previous line, [1, n+1) this line
    float * __restrict__ filtQ = m_filtQ.data();
    const int16_t * __restrict__ samples = m_samples;
    uint8_t * __restrict__ demod = m_demod_data;

    // 1. Carrier phase in cycles [0, 1). Computed from the line start, in double,
    //    so it does not drift, and continued from the previous line, so there is
    //    no phase jump (and a spurious pixel) at the line start.
    const double ph_inc = m_carrier / m_SamplesPerSec_frac;
    const double ph0 = m_carrierPhase;
    for (int32_t i = 0; i < n; i++) {
        double ph = ph0 + i * ph_inc;
        cycle[i] = (float) (ph - floor(ph));
    }
    m_carrierPhase = ph0 + n * ph_inc;
    m_carrierPhase -= floor(m_carrierPhase);

    // 2. Mix to carrier so start/stop/black/white freqs will be relative to zero
    const float normalize_sample = 1.0f / 32768.0f;     // -1..0..1
    for (int32_t i = 0; i < n; i++) {
        float samp = samples[i] * normalize_sample;
        float angle = (float) K_2PI * cycle[i];
        mixI[hist + i] = samp * cosf(angle);
        mixQ[hist + i] = samp * sinf(angle);
    }

    // 3. Low pass filter: filt[i] = sum(c[k] * mix[i - k]).
    //    Loop over taps outside and over samples inside: the inner loop is a plain
    //    multiply-add over contiguous arrays (no reduction), the ideal shape for SIMD.
    //    Blocks keep the partial sums in L1 cache.
    const float * __restrict__ c = m_lpf.coeffs.data();
    const int32_t BLOCK = 512;
    for (int32_t b = 0; b < n; b += BLOCK) {
        const int32_t len = MIN(BLOCK, n - b);
        float * __restrict__ outI = filtI + 1 + b;
        float * __restrict__ outQ = filtQ + 1 + b;

        for (int32_t j = 0; j < len; j++) {
            outI[j] = 0;
            outQ[j] = 0;
        }

        for (int32_t k = 0; k < taps; k++) {
            const float ck = c[k];
            const float * __restrict__ inI = mixI + hist - k + b;
            const float * __restrict__ inQ = mixQ + hist - k + b;
            for (int32_t j = 0; j < len; j++) {
                outI[j] += ck * inI[j];
                outQ[j] += ck * inQ[j];
            }
        }
    }

    // keep the filter history for the next line
    memmove(mixI, mixI + n, hist * sizeof *mixI);
    memmove(mixQ, mixQ + n, hist * sizeof *mixQ);

    // 4. FM discriminator: sine of the phase step between consecutive samples,
    //    (I[i-1]*Q[i] - Q[i-1]*I[i]) / (|z[i]| * |z[i-1]|), scaled so the
    //    black..white deviation maps onto 0..255.
    const float scale = -1.3 * (m_SamplesPerSec_nom / m_deviation / 8);
    for (int32_t i = 0; i < n; i++) {
        float Icur = filtI[i + 1], Qcur = filtQ[i + 1];
        float Iprev = filtI[i], Qprev = filtQ[i];

        float cross = Qcur * Iprev - Icur * Qprev;
        float mag2 = (Icur * Icur + Qcur * Qcur) * (Iprev * Iprev + Qprev * Qprev);
        float x = (mag2 > 0) ? cross / sqrtf(mag2) : 0;    // silence: mid grey, not NaN

        x = (x * scale) * 0.5f + 0.5f;
        float pixel = x * 255.0f;
        pixel = fminf(fmaxf(pixel, 0.0f), 255.0f);   // clamp
        demod[i] = (uint8_t) pixel;
    }

    filtI[0] = filtI[n];
    filtQ[0] = filtQ[n];
}

/*
    Decode a single line of fax data from buffer placing it in image pointer.
    Buffer should contain m_SamplesPerSec_nom*60.0/m_lpm*colors bytes.
    Image will contain imagewidth*colors bytes.
*/
void FaxDecoder::DecodeImageLine(uint8_t* buffer, int32_t buffer_len, uint8_t *image)
{
    int32_t pixel;
    //int n = m_SamplesPerSec_nom*60.0/m_lpm;
    int32_t spl = m_SamplesPerLine;

    if (buffer_len != spl) {
        faxprintf("FAX m_SamplesPerSec_nom=%.1f buffer_len=%d spl=%d\n", m_SamplesPerSec_nom, buffer_len, spl);
        fprintf(stderr, "DecodeImageLine requires specific buffer length");
    }

    int32_t i, j;

    for (i = 0; i < m_imagewidth; i++) {
        
        int32_t firstsample = spl * i/m_imagewidth;
        int32_t lastsample = spl * (i+1)/m_imagewidth - 1;
        // fprintf(stdout, "  decode 2...\n");
        int32_t pixelSamples = 0, sample = firstsample;
        pixel = 0;
// printf("PIXEL: first sample [%d], last sample [%d], diff [%d], ", firstsample, lastsample, lastsample - firstsample + 1);
        do {
            pixel += buffer[sample];
            pixelSamples++;
        } while (sample++ < lastsample);
// printf("pixel [%d], ", pixel);
        pixel /= pixelSamples;
// printf("final pixel [%d]\n", pixel);
        image[i] = pixel;
    }
    
    bool emit = false;
    if (m_debug) {
        emit = true;
    } else {
        double m_lineNextBlend, m_linePrevBlend;
        if (m_lineIncrAcc >= 1.0) {
            m_lineIncrAcc -= 1.0;     // keep bounded
            if (m_imageline != 0 && m_lineIncrAcc != 0) {
                m_lineNextBlend = m_lineIncrAcc/m_lineBlend;
                m_linePrevBlend = 1.0 - m_lineNextBlend;
                j = -m_imagewidth;
                for (i = 0; i < m_imagewidth; i++, j++) {
                    pixel = roundf((float)image[i] * m_lineNextBlend + (float)image[j] * m_linePrevBlend);
                    pixel = MIN(255, pixel);
                    m_outImage[i] = pixel;
                }
                m_lineBlend = m_lineIncrFrac;
            }
            emit = true;
        } else {
            m_lineBlend += m_lineIncrFrac;
        }
        m_lineIncrAcc += m_lineIncrFrac;
    }

    if (emit) {
        FileWrite(m_outImage, m_imagewidth);
    }
}

bool FaxDecoder::ProcessSamples(int16_t *samps, int32_t nsamps, float shift)
{
    if ((m_lineLimit > 0) && (m_fax_line >= m_lineLimit)) {
        return false;
    }

    int32_t i = 0;
    
    if (m_bEndDecoding) return false;
    
    if (shift) m_skip = shift * m_SamplesPerLine;

    if (m_skip) {
        int32_t skip = MIN(nsamps, m_skip);
        nsamps -= skip;
        samps = &samps[skip];
        faxprintf("FAX m_skip %d skip %d\n", m_skip, skip);
        m_skip -= skip;
    }

    while (i < nsamps) {
        for (; i < nsamps && m_samp_idx < m_SamplesPerLine;) {
            m_samples[m_samp_idx] = samps[i];
            m_samp_idx++;
            m_fi += m_SampleRateRatio;
            i = trunc(m_fi);
        }
        
        if (m_samp_idx == m_SamplesPerLine) {
            DecodeFaxLine();
            m_samp_idx = 0;
        }
    }
    m_fi -= nsamps;     // keep bounded

    return true;
}

void FaxDecoder::InitializeImage()
{
    height = m_imgsize / 2 / m_SamplesPerSec_nom / 60.0 * m_lpm;
    imgpos = 0;

    if (height == 0) /* for unknown size, start out at 256 */
        height = 256;

    FreeImage();
    m_imgdata  = (uint8_t*) kiwi_imalloc("InitializeImage", m_imagewidth*height*m_imagecolors);
    m_outImage = (uint8_t*) kiwi_imalloc("InitializeImage", m_imagewidth*m_imagecolors);

    lasttype = IMAGE;
    typecount = 0;

    m_autostopped = false;
}

bool FaxDecoder::Configure(int lpm, int32_t imagewidth, int32_t BitsPerPixel, int32_t carrier,
                           int32_t deviation, enum firfilter::Bandwidth bandwidth,
                           double minus_saturation_threshold,
                           bool bIncludeHeadersInImages, bool use_phasing, bool autostop,
                           int32_t debug, bool reset, double sample_rate, double srcorr, int32_t lineLimit)
{
    // m_rx_chan = rx_chan;
    m_BitsPerPixel = BitsPerPixel;
    m_carrier = carrier;
    m_deviation = deviation;
    m_minus_saturation_threshold = minus_saturation_threshold;
    m_bIncludeHeadersInImages = bIncludeHeadersInImages;
    m_use_phasing = use_phasing;
    m_autostop = autostop;
    m_bSkipHeaderDetection = (m_use_phasing || m_autostop)? false : true;
    
    m_imagecolors = 1;

    m_lpm = lpm;
    m_bFM = true;
    m_Start_IOC576_Frequency = 300;
    m_Start_IOC288_Frequency = 675;
    m_StopFrequency = 450;
    m_StartStopLength = 5;
    m_phasingLines = 20;
    m_offset = 0;
    m_imgsize = 0;
    m_lineLimit = lineLimit;


    m_imagewidth = imagewidth;
    // /* must reset if image width changes */
    // if (m_imagewidth != imagewidth || reset) {
    //     m_imagewidth = imagewidth;
    //     InitializeImage();
    // }

    m_lineIncrFrac = m_imagewidth / (M_PI * 576);
    m_bEndDecoding = false;
    m_debug = debug;
    fprintf(stdout, "FAX Configure lpm=%d car=%.3f dev=%.3f debug=%d\n", m_lpm, m_carrier, m_deviation, m_debug);

    m_SamplesPerSec_frac = sample_rate * srcorr;// * 1.000092;
    m_SamplesPerSec_nom = sample_rate;
    m_SampleRateRatio = m_SamplesPerSec_frac / m_SamplesPerSec_nom;

    fprintf(stdout, "FAX Configure m_SamplesPerSec_frac=%0.3f m_SamplesPerSec_nom=%.3f m_SampleRateRatio=%.3f \n", m_SamplesPerSec_frac, m_SamplesPerSec_nom, m_SampleRateRatio);

    // the low pass filters depend on the sample rate, so build them only when it is known
    m_lpf = firfilter(bandwidth, m_SamplesPerSec_nom);
    fprintf(stdout, "FAX Configure low pass filter taps=%zu\n", m_lpf.coeffs.size());

    // if (reset) {
        // CleanUpBuffers();
        // SetupBuffers();
    // } else {
        InitializeImage();
        SetupBuffers();
    // }

    return true;
}

void FaxDecoder::SetupBuffers()
{
    // initial approx sps to set samplesPerMin/Line
    // UpdateSampleRate();
    
    double samplesPerMin = m_SamplesPerSec_nom * 60.0;
    m_SamplesPerLine = samplesPerMin / m_lpm;
    // m_BytesPerLine = m_SamplesPerLine * 2;
    
    faxprintf("FAX SamplesPerSec=%.3f/%.0f lpm=%d SamplesPerLine=%d\n",
        m_SamplesPerSec_frac, m_SamplesPerSec_nom, m_lpm, m_SamplesPerLine);
    
    m_samples = new int16_t[m_SamplesPerLine];
    m_samp_idx = 0;
    m_fi = 0;
    m_demod_data = new uint8_t[m_SamplesPerLine];

    const size_t hist = m_lpf.coeffs.size() - 1;
    m_carrierCycle.assign(m_SamplesPerLine, 0.0f);
    m_mixI.assign(hist + m_SamplesPerLine, 0.0f);
    m_mixQ.assign(hist + m_SamplesPerLine, 0.0f);
    m_filtI.assign(1 + m_SamplesPerLine, 0.0f);
    m_filtQ.assign(1 + m_SamplesPerLine, 0.0f);
    m_carrierPhase = 0;

    phasingPos = new int[m_phasingLines];
    phasingLinesLeft = phasingSkipData = 0;
    have_phasing = false;

    // we reset at start and stop, so if already offset phasing will follow
    if (m_offset)
        phasingLinesLeft = m_phasingLines;
}

void FaxDecoder::FreeImage()
{
    if (m_imgdata) {
        kiwi_ifree(m_imgdata, "FreeImage");
    }

    if (m_outImage) {
        kiwi_ifree(m_outImage, "FreeImage");
    }

    m_imageline = 0;
    m_lineIncrAcc = 0;
}

void FaxDecoder::CleanUpBuffers()
{
     delete [] m_samples;
     delete [] m_demod_data;
     delete [] phasingPos;
}

// SECURITY:
// Little bit of a security hole: Can look at previously saved fax images by downloading the fixed filename.
// Not a big deal really.

void FaxDecoder::FileOpen(const char *fn)
{
    // m_fn = fn;

    FileClose();
    // asprintf(&m_fn, DIR_DATA "/fax.ch%d.pgm", m_rx_chan);
    // m_file = pgm_file_open(m_fn, &m_offset, m_imagewidth, 0, 255);
    m_file = fopen(fn, "w");
    
    if (m_file != NULL) {
        m_offset = fprintf(m_file, "P5 %d ", m_imagewidth);
        // reserve space for height (written later with pgm_file_height()) using a fixed-length field
        fprintf(m_file, "%6d %d\n", 0, 255);
    }

    if (m_file == NULL) {
        faxprintf("FAX open FAILED %s\n", fn);
    } else {
        faxprintf("FAX open %s\n", fn);
    }
}

void FaxDecoder::FileWrite(uint8_t *data, int32_t datalen)
{
    if (m_file == NULL) return;
    
    fwrite(data, datalen, 1, m_file);
    m_fax_line++;

    // With each data write to the file update image header too,
    // so it would be readable if utility is killed midst processing.
    // Usefull for partial decode, when image tilt is evaluated by eye.
    int32_t pos = ftell(m_file);
    
    fseek(m_file, m_offset, SEEK_SET);
    fprintf(m_file, "%6d", m_fax_line);
    
    if (m_fax_line % 100 == 0) {
        fprintf(stdout, "Lines decoded: %d\n", m_fax_line);
    }

    fseek(m_file, pos, SEEK_SET);
}

void FaxDecoder::FileClose()
{
    if (m_file == NULL) return;
    
    fflush(m_file);
    
    fseek(m_file, m_offset, SEEK_SET);
    if (m_fax_line > 999999) {
        m_fax_line = 999999;
        fprintf(stdout, "height limited to 999999!\n");
    }
    fprintf(m_file, "%6d", m_fax_line);
    
    fclose(m_file);
    // faxprintf("FAX %s wrote %d lines\n", m_fn, m_fax_line);
    faxprintf("FAX wrote %d lines\n", m_fax_line);
    
    m_file = NULL;
    m_fax_line = 0;
}
