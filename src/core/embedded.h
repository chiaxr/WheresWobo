#pragma once

// An entry in a table of files compiled into the binary by
// cmake/EmbedFiles.cmake. Tables end with an entry whose path is null.

#include <cstddef>

namespace wobo {

struct EmbeddedFile {
    const char* path;           // relative to the embedded directory, '/'-separated
    const unsigned char* data;  // NUL-terminated for convenience
    std::size_t size;           // excluding the NUL
};

}  // namespace wobo
