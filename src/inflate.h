#pragma once

// Raw deflate (no zlib header), the way ZIP entries store it: stb_image's decoder, compiled once in
// language_import.cpp (where Imaginators' archives need it too).

#include <cstddef>
#include <vector>

namespace coreoflight {

// Decodes `size` bytes of raw deflate into `out`, which must come out exactly `out_size` bytes;
// false if the data is broken or of another size.
bool InflateRaw(const char* data, size_t size, size_t out_size, std::vector<char>& out);

}  // namespace coreoflight
