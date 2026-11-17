// fax library: PGM image output
//
// The height in the header is rewritten after every line, so the file is a valid
// image at any time, also if the program is killed while decoding.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace fax {

class PgmWriter {
public:
    PgmWriter() = default;
    PgmWriter(const std::string &path, int width) { open(path, width); }
    ~PgmWriter() { close(); }
    PgmWriter(const PgmWriter &) = delete;
    PgmWriter &operator=(const PgmWriter &) = delete;

    void open(const std::string &path, int width);     // throws fax::Error
    void write_line(const uint8_t *pixels);
    void close();                                       // also done by the destructor

    bool is_open() const { return m_file != nullptr; }
    int32_t lines() const { return m_lines; }

    static constexpr int32_t MAX_LINES = 999999;        // the header field has 6 digits

private:
    FILE *m_file = nullptr;
    int m_width = 0;
    long m_heightPos = 0;
    int32_t m_lines = 0;
};

} // namespace fax
