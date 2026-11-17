# Dark images at 44.1/48 kHz: sample-rate-dependent low-pass filter

*2026-11-17*

## The fix

The bug is in the low-pass filter of the FM demodulator, in `src/FaxDecoder.cpp`. Its 17 coefficients were hard-coded for a 12 kHz sample rate, but the program used them unchanged at every sample rate. The filter's coefficients are now built for the file's actual sample rate when the decoder is configured (`firfilter::firfilter(Bandwidth, sample_rate)` in `src/FaxDecoder.cpp`, called from `Configure()`). `FaxDecoder.h` changed so the filter can hold a variable number of coefficients.

**Results with the test recording** (`20261113_052501-Master-3855000Hz-CW-audio.wav` at 48k, and its 16k downsampled copy):

| | 48k mean / median brightness | white paper level |
|---|---|---|
| Before, 48k | 44 / 42 | ~72 (grey) |
| After, 48k | 220 / 255 | 255 |
| 16k (reference) | 220 / 255 | 255 |

After the fix, the 48k image looks the same as the 16k one.

---

## Level 1: for someone with no coding or signal background

A radio fax sends a picture as sound. A low whistle means black, a higher whistle means white, and the program listens to the pitch to work out how dark each dot should be.

Before listening, the program runs the sound through a "sieve" that keeps the useful part and throws away a ghost tone the program creates itself during processing. The sieve was built for recordings that take 12,000 snapshots of the sound per second. The sieve's holes are sized in "snapshots", not in real pitches. With a 48,000-snapshot recording, each snapshot is four times shorter, so the holes became four times too wide in real terms. The ghost tone went straight through.

With the ghost mixed in, the program heard the wrong pitch nearly all the time, and that wrong pitch counts as "very black". So the whole picture went dark, and only a faint trace of the real image showed through.

The fix rebuilds the sieve for whatever recording rate the file uses, so it always lets through the same real pitches.

## Level 2: for a programmer with general signal-processing ideas (Fourier, DCT)

The demodulator (`DemodulateData`) works like this:

1. **Mix to baseband.** It multiplies the audio by cos and sin at the 1900 Hz carrier. Multiplying a real tone by a complex exponential produces two components:
   - the **difference** frequency: the wanted fax signal, within about ±400 Hz of zero;
   - the **sum** frequency: an unwanted image near 2 × 1900 = 3800 Hz.
2. **Low-pass filter** I and Q with a 17-tap FIR, which should remove the 3800 Hz image.
3. **FM discriminator.** It measures the phase change between samples, which gives instantaneous frequency, and maps it to grey levels.

The key point is that an FIR filter's frequency response depends only on the coefficients relative to the sample rate. The cutoff is a fraction of fs, so if you change fs and keep the coefficients, the cutoff in Hz scales with it. The response of the same 17 taps at different rates:

| fs | −3 dB cutoff | gain at 3800 Hz |
|---|---|---|
| 12000 | 1302 Hz | −42 dB ✅ |
| 16000 | 1736 Hz | −48 dB ✅ |
| 44100 | 4786 Hz | −0.7 dB ❌ |
| 48000 | 5209 Hz | −0.2 dB ❌ |

At 44.1k and 48k the image passes almost unchanged and corrupts the discriminator.

**The fix:** treat the 17 original taps as samples of a continuous impulse response taken at 12 kHz. Then resample that response at the real fs using band-limited (sinc) interpolation, which is the time-domain version of keeping the same spectrum. Then normalise to the same DC gain. The result:
- 48 kHz gets 65 taps, 44.1 kHz gets 61, and 16 kHz gets 23.
- All of them have the same response in Hz: −3 dB at 1300 Hz and about −42 dB at 3800 Hz.
- At 12 kHz and below, the original taps are used unchanged.

The cost is about 4× more multiply-adds per sample at 48k. The 40-minute test file decodes in 3.7 s.

## Level 3: for someone who knows this code

- **The image term.** `I + jQ = s·e^{+jωc·t}`. For s = cos((ωc+ωd)t), the result is ½e^{−jωd·t}, the wanted term (hence the negative `scale`), plus ½e^{j(2ωc+ωd)t}. That second term sits at 3.4–4.2 kHz across the black/white range.
- **What it does to the output.** With the ACfax MIDDLE taps at 48k, the image comes through at 0.972 against 1.003 for the baseband term. Two phasors of almost equal size give a resultant whose phase moves at roughly their average frequency, +ωc ≈ 1900 Hz, most of the time. Near each beat null it makes a short, steep slip back by nearly 2π.
  - The **plateau** gives Δφ ≈ 2π·1900/48000 = 0.249 rad. With `scale = −1.3·fs/dev/8 = −19.5`, that makes x ≈ −4.8, so `x/2+0.5` is negative and gets clamped to 0 (black).
  - The **slips** should balance that out, because the long-term mean is still the correct −ωd. But they are crushed by `sin(Δφ)` (which saturates past π/2) and by the 0–255 clamp.
  - After averaging about 13 samples per pixel, you get a nearly black floor (p5 = 16) with a faint image (p95 = 72). That matches the dark PGM.
- **Why `scale` wasn't the problem.** `scale` already contains `m_SamplesPerSec_nom`, so discriminator gain doesn't depend on fs. The filter was the only part that wasn't adapted to the sample rate.
- **What changed in the code:**
  - `firfilter` now holds `std::vector` coefficients and buffer.
  - It is built in `Configure()` once the sample rate is known. Before, it was built before the rate was set, and its 17-tap size was fixed.
  - `apply_firfilter` keeps the same circular-buffer convolution, just with a variable length.
  - The narrow and wide filters are scaled the same way, though `fax.cpp` only uses the middle one.
- **Side effect at 16 kHz.** 16 kHz files now get the 12 kHz response (cutoff 1.3 kHz instead of 1.74 kHz). This keeps behaviour the same across sample rates. No visible change was seen in the 16k test image, but it is a small difference from before.

Other code that depends on the sample rate is already correct: `ph_inc`, `m_SamplesPerLine`, the header-detection Fourier probe, and phasing.
