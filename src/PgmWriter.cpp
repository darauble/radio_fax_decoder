// fax library: PGM image output

#include "fax/PgmWriter.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/types.h>

#include "fax/Log.h"

namespace fax {

void PgmWriter::open(const std::string &path, int width)
{
    close();

    m_file = fopen(path.c_str(), "w");
    if (!m_file)
        throw Error("cannot write " + path + ": " + strerror(errno));

    m_width = width;
    m_lines = 0;
    m_heightPos = fprintf(m_file, "P5 %d ", width);
    // reserve space for the height, rewritten after each line, using a fixed-length field
    fprintf(m_file, "%6d %d\n", 0, 255);
}

void PgmWriter::write_line(const uint8_t *pixels)
{
    if (!m_file)
        return;

    fwrite(pixels, m_width, 1, m_file);
    m_lines++;

    // Update the height with each line, so the file is readable if the program is
    // killed while decoding. Useful for a partial decode, to judge the slant by eye.
    off_t pos = ftello(m_file);
    fseeko(m_file, m_heightPos, SEEK_SET);
    fprintf(m_file, "%6d", std::min(m_lines, MAX_LINES));
    fseeko(m_file, pos, SEEK_SET);
}

void PgmWriter::close()
{
    if (!m_file)
        return;

    fflush(m_file);
    fseeko(m_file, m_heightPos, SEEK_SET);
    fprintf(m_file, "%6d", std::min(m_lines, MAX_LINES));
    fclose(m_file);
    m_file = nullptr;
}

} // namespace fax
