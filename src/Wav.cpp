// fax library: WAV input

#include "fax/Wav.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/types.h>

#include "fax/Log.h"

namespace fax {

static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }

WavReader::WavReader(const std::string &path)
{
    m_file = fopen(path.c_str(), "rb");
    if (!m_file)
        throw Error("open(" + path + ") failed: " + strerror(errno));

    try {
        uint8_t riff[12];
        if (fread(riff, sizeof riff, 1, m_file) != 1 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4))
            throw Error(path + ": not a WAV file");

        fseeko(m_file, 0, SEEK_END);
        const uint64_t file_size = ftello(m_file);

        // walk the chunks up to "data"
        uint16_t format = 0;
        bool have_fmt = false, have_data = false;
        uint64_t pos = 12;
        while (pos + 8 <= file_size) {
            uint8_t ch[8];
            fseeko(m_file, pos, SEEK_SET);
            if (fread(ch, sizeof ch, 1, m_file) != 1)
                break;
            const uint32_t len = le32(ch + 4);

            if (!memcmp(ch, "fmt ", 4)) {
                uint8_t fmt[40] = {};
                if (len < 16 || fread(fmt, std::min<uint32_t>(len, sizeof fmt), 1, m_file) != 1)
                    throw Error(path + ": broken format chunk");
                format = le16(fmt);
                m_info.channels = le16(fmt + 2);
                m_info.sample_rate = le32(fmt + 4);
                m_info.block_align = le16(fmt + 12);
                m_info.bits = le16(fmt + 14);
                if (format == 0xFFFE && len >= 40)      // WAVE_FORMAT_EXTENSIBLE: sub-format GUID
                    format = le16(fmt + 24);
                have_fmt = true;
            } else if (!memcmp(ch, "data", 4)) {
                m_info.data_offset = pos + 8;
                m_info.data_bytes = len;
                // recorders that were stopped early leave the size 0 or 0xFFFFFFFF: use the rest of the file
                if (len == 0 || len == 0xFFFFFFFF || m_info.data_offset + len > file_size)
                    m_info.data_bytes = file_size - m_info.data_offset;
                have_data = true;
                break;
            }
            pos += 8 + (uint64_t) len + (len & 1);      // chunks are padded to even sizes
        }

        if (!have_fmt || !have_data)
            throw Error(path + ": no " + (have_fmt ? "audio data" : "format chunk") + " found");
        if (format != 1)
            throw Error(path + ": only PCM WAV files are supported");
        if (m_info.bits != 16 || m_info.block_align != 2 * m_info.channels)
            throw Error(path + ": only 16-bit samples are supported");
        if (m_info.sample_rate == 0)
            throw Error(path + ": sample rate is 0");

        seek(0);
    } catch (...) {
        fclose(m_file);
        throw;
    }
}

WavReader::~WavReader()
{
    if (m_file)
        fclose(m_file);
}

void WavReader::seek(uint64_t sample)
{
    m_pos = std::min(sample, m_info.samples());
    fseeko(m_file, m_info.data_offset + m_pos * m_info.block_align, SEEK_SET);
}

size_t WavReader::read(int16_t *buffer, size_t count)
{
    const uint64_t left = (m_info.samples() - m_pos) * m_info.channels;
    const size_t n = fread(buffer, sizeof(int16_t), std::min<uint64_t>(count, left), m_file);
    m_pos += n / m_info.channels;
    return n;
}

double WavReader::position() const
{
    return m_info.samples() ? (double) m_pos / m_info.samples() : 1.0;
}

} // namespace fax
