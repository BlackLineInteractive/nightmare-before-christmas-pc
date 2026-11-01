#include "OogieEngine/Loader/CapcomLZ.h"
#include <fstream>
#include <cstring>
#include <iostream>

namespace OogieEngine {
namespace Loader {

class BitReader {
public:
    BitReader(const uint8_t* buffer, size_t size, size_t startPos = 5)
        : data(buffer), dataSize(size), pos(startPos), bitPos(0)
    {
        if (pos + 4 <= dataSize) {
            std::memcpy(&curWord, data + pos, 4);
            pos += 4;
        } else {
            curWord = 0;
        }
    }

    uint32_t ReadBit() {
        uint32_t bit = (curWord >> bitPos) & 1;
        bitPos = (bitPos + 1) & 31;
        if (bitPos == 0) {
            if (pos + 4 <= dataSize) {
                std::memcpy(&curWord, data + pos, 4);
                pos += 4;
            } else if (pos < dataSize) {
                curWord = 0;
                std::memcpy(&curWord, data + pos, dataSize - pos);
                pos = dataSize;
            } else {
                curWord = 0;
            }
        }
        return bit;
    }

    uint32_t ReadBits(int n) {
        uint32_t val = 0;
        for (int i = 0; i < n; ++i) {
            val |= (ReadBit() << i);
        }
        return val;
    }

private:
    const uint8_t* data;
    size_t dataSize;
    size_t pos;
    int bitPos;
    uint32_t curWord = 0;
};

std::vector<uint8_t> CapcomLZ::Decompress(const uint8_t* compressedData, size_t compressedSize) {
    if (!compressedData || compressedSize < 5) return {};

    uint8_t compType = compressedData[0];
    uint32_t uncompSize = 0;
    std::memcpy(&uncompSize, compressedData + 1, 4);

    if (uncompSize == 0 || uncompSize > 256 * 1024 * 1024) { // Safety sanity cap at 256MB
        return {};
    }

    if (compType == 0) {
        if (compressedSize < 5 + uncompSize) return {};
        return std::vector<uint8_t>(compressedData + 5, compressedData + 5 + uncompSize);
    }

    BitReader reader(compressedData, compressedSize, 5);
    static const int table[4] = { 2, 4, 6, 10 };

    std::vector<uint8_t> out;
    out.reserve(uncompSize);

    while (out.size() < uncompSize) {
        uint32_t flag = reader.ReadBit();
        if (flag == 1) {
            // Literal byte (8 bits)
            out.push_back(static_cast<uint8_t>(reader.ReadBits(8)));
        } else {
            // Match (distance and length)
            uint32_t offset = 0;
            if (compType == 1) {
                offset = reader.ReadBits(10);
            } else {
                // Type 2: variable length offset
                int count0 = 0;
                while (reader.ReadBit() == 0) {
                    count0++;
                }
                if (count0 >= 4) break;
                offset = reader.ReadBits(table[count0]);
            }

            // Match length
            int count0Len = 0;
            while (reader.ReadBit() == 0) {
                count0Len++;
            }
            if (count0Len >= 4) break;
            uint32_t length = reader.ReadBits(table[count0Len]);

            uint32_t dist = offset + 1;
            if (dist > out.size()) {
                break; // Invalid backward offset
            }

            for (uint32_t i = 0; i < length && out.size() < uncompSize; ++i) {
                out.push_back(out[out.size() - dist]);
            }
        }
    }

    return out;
}

std::vector<uint8_t> CapcomLZ::Decompress(const std::vector<uint8_t>& compressedData) {
    return Decompress(compressedData.data(), compressedData.size());
}

std::vector<uint8_t> CapcomLZ::DecompressFile(const std::string& lzPath) {
    std::ifstream file(lzPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }

    return Decompress(buffer);
}

} // namespace Loader
} // namespace OogieEngine
