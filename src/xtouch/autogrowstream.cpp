#include "autogrowstream.h"
#include <cstdlib>
#include <cstring>

static const size_t MAX_REPORT_SIZE = 32768;
static const size_t BUFFER_INCREMENT = 1024;

XtouchAutoGrowBufferStream::XtouchAutoGrowBufferStream()
    : _len(0), buffer_size(0), _buffer(nullptr), _failed(false)
{
}

bool XtouchAutoGrowBufferStream::includes(const char *target)
{
    return _buffer != nullptr && strstr(_buffer, target) != nullptr;
}

size_t XtouchAutoGrowBufferStream::write(uint8_t byte)
{
    if (_failed) return 0;
    if (_len >= MAX_REPORT_SIZE)
    {
        _failed = true;
        return 0;
    }
    // Reserve one byte for NUL, including at exact allocation boundaries.
    if (static_cast<size_t>(_len) + 1 >= buffer_size)
    {
        size_t capacity = buffer_size + BUFFER_INCREMENT;
        if (capacity > MAX_REPORT_SIZE + 1) capacity = MAX_REPORT_SIZE + 1;
        char *buffer = static_cast<char *>(realloc(_buffer, capacity));
        if (buffer == nullptr)
        {
            _failed = true;
            return 0;
        }
        _buffer = buffer;
        buffer_size = static_cast<uint16_t>(capacity);
    }
    _buffer[_len++] = byte;
    _buffer[_len] = '\0';
    return 1;
}

void XtouchAutoGrowBufferStream::flush()
{
    _len = 0;
    _failed = false;
    if (_buffer) _buffer[0] = '\0';
}

int XtouchAutoGrowBufferStream::read() { return -1; }
int XtouchAutoGrowBufferStream::peek() { return -1; }
int XtouchAutoGrowBufferStream::available() { return 0; }
