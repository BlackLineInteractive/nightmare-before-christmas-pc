#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Loader/CapcomLZ.h  —  Native Capcom LZ Type 2 Decompressor
// Decodes bitstream sliding-window dictionary compressed archives on the fly.
// ─────────────────────────────────────────────────────────────────────────────

#include <vector>
#include <cstdint>
#include <cstddef>
#include <string>

namespace OogieEngine {
namespace Loader {

class CapcomLZ {
public:
    // Decompresses a Capcom LZ buffer in RAM
    static std::vector<uint8_t> Decompress(const uint8_t* compressedData, size_t compressedSize);
    static std::vector<uint8_t> Decompress(const std::vector<uint8_t>& compressedData);

    // Loads and decompresses a .lz file directly from disk
    static std::vector<uint8_t> DecompressFile(const std::string& lzPath);
};

} // namespace Loader
} // namespace OogieEngine
