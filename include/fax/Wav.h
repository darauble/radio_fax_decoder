// fax library: WAV input
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace fax {

struct WavInfo {
    uint32_t sample_rate = 0;       // nominal, as in the header
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint16_t block_align = 0;       // bytes per sample frame
    uint64_t data_offset = 0;       // file offset of the first sample
    uint64_t data_bytes = 0;        // audio bytes (to the end of the file if the header has no size)
    uint64_t samples() const { return block_align ? data_bytes / block_align : 0; }
};

// Reads 16-bit PCM WAV files. Walks the RIFF chunks, so extra chunks (LIST, ...)
// before or after the audio are skipped. Only mono is decoded.
class WavReader {
public:
    explicit WavReader(const std::string &path);        // throws fax::Error
    ~WavReader();
    WavReader(const WavReader &) = delete;
    WavReader &operator=(const WavReader &) = delete;

    const WavInfo &info() const { return m_info; }

    void seek(uint64_t sample);                         // sample frame from the start of the audio
    size_t read(int16_t *buffer, size_t count);         // returns 0 at the end of the audio
    double position() const;                            // 0..1, for progress

private:
    FILE *m_file = nullptr;
    WavInfo m_info;
    uint64_t m_pos = 0;                                 // sample frames read/skipped
};

} // namespace fax
