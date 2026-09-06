#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace renderlab
{
    inline constexpr char kRlHdrMagic[8] = {'R', 'L', 'H', 'D', 'R', '1', '\0', '\0'};
    inline constexpr uint32_t kRlHdrVersion = 1;
    inline constexpr uint32_t kRlHdrFormatRgba16Float = 1;
    inline constexpr uint32_t kRlHdrHeaderBytes = 32;
    inline constexpr uint16_t kHalfPositiveInf = 0x7c00;
    inline constexpr uint16_t kHalfNan = 0x7e00;

#pragma pack(push, 1)
    struct RlHdrHeader
    {
        char magic[8];
        uint32_t version;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t reserved0;
        uint32_t reserved1;
    };
#pragma pack(pop)

    static_assert(sizeof(RlHdrHeader) == kRlHdrHeaderBytes);

    struct RlHdrImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint16_t> rgba16;
    };

    inline uint16_t FloatToHalf(float value)
    {
        union
        {
            float f;
            uint32_t u;
        } in;
        in.f = value;
        const uint32_t sign = (in.u >> 16) & 0x8000u;
        uint32_t mantissa = in.u & 0x7fffffu;
        int32_t exponent = static_cast<int32_t>((in.u >> 23) & 0xffu) - 127 + 15;
        if ((in.u & 0x7fffffffu) > 0x7f800000u)
        {
            return static_cast<uint16_t>(sign | 0x7e00u);
        }
        if (exponent <= 0)
        {
            if (exponent < -10)
            {
                return static_cast<uint16_t>(sign);
            }
            mantissa |= 0x800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - exponent);
            const uint32_t half = mantissa >> shift;
            return static_cast<uint16_t>(sign | half);
        }
        if (exponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7c00u);
        }
        return static_cast<uint16_t>(
            sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
    }

    inline float HalfToFloat(uint16_t bits)
    {
        const uint32_t sign = (static_cast<uint32_t>(bits) & 0x8000u) << 16;
        int32_t exponent = static_cast<int32_t>((bits >> 10) & 0x1fu);
        uint32_t mantissa = bits & 0x3ffu;
        uint32_t out = 0;
        if (exponent == 0)
        {
            if (mantissa == 0)
            {
                out = sign;
            }
            else
            {
                exponent = 1;
                while ((mantissa & 0x400u) == 0)
                {
                    mantissa <<= 1;
                    --exponent;
                }
                mantissa &= 0x3ffu;
                out = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
            }
        }
        else if (exponent == 31)
        {
            out = sign | 0x7f800000u | (mantissa << 13);
        }
        else
        {
            out = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
        union
        {
            uint32_t u;
            float f;
        } conv;
        conv.u = out;
        return conv.f;
    }

    inline bool IsFiniteHalf(uint16_t bits)
    {
        return ((bits & 0x7c00u) != 0x7c00u);
    }

    inline uint64_t CountNonFiniteRgb(const uint16_t* rgba16, uint32_t width, uint32_t height)
    {
        uint64_t count = 0;
        const uint64_t pixels = static_cast<uint64_t>(width) * height;
        for (uint64_t i = 0; i < pixels; ++i)
        {
            const uint16_t r = rgba16[i * 4u + 0u];
            const uint16_t g = rgba16[i * 4u + 1u];
            const uint16_t b = rgba16[i * 4u + 2u];
            if (!IsFiniteHalf(r) || !IsFiniteHalf(g) || !IsFiniteHalf(b))
            {
                ++count;
            }
        }
        return count;
    }

    inline bool WriteRlHdrFile(
        const std::filesystem::path& path,
        uint32_t width,
        uint32_t height,
        const uint16_t* rgba16,
        std::string& error)
    {
        error.clear();
        if (width == 0 || height == 0 || rgba16 == nullptr)
        {
            error = "rlhdr write requires a non-empty image";
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "failed to create " + path.generic_string();
            return false;
        }
        RlHdrHeader header{};
        std::memcpy(header.magic, kRlHdrMagic, 8);
        header.version = kRlHdrVersion;
        header.width = width;
        header.height = height;
        header.format = kRlHdrFormatRgba16Float;
        header.reserved0 = 0;
        header.reserved1 = 0;
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        const std::streamsize bytes =
            static_cast<std::streamsize>(width) * height * 8;
        output.write(reinterpret_cast<const char*>(rgba16), bytes);
        if (!output)
        {
            error = "failed while writing " + path.generic_string();
            return false;
        }
        return true;
    }

    inline bool LoadRlHdrFile(const std::filesystem::path& path, RlHdrImage& image, std::string& error)
    {
        error.clear();
        image = {};
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = "failed to open " + path.generic_string();
            return false;
        }
        const auto size = static_cast<uint64_t>(input.tellg());
        input.seekg(0);
        RlHdrHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input)
        {
            error = "rlhdr header truncated";
            return false;
        }
        if (std::memcmp(header.magic, kRlHdrMagic, 8) != 0)
        {
            error = "rlhdr magic mismatch";
            return false;
        }
        if (header.version != kRlHdrVersion || header.format != kRlHdrFormatRgba16Float ||
            header.reserved0 != 0 || header.reserved1 != 0)
        {
            error = "rlhdr version/format/reserved mismatch";
            return false;
        }
        if (header.width == 0 || header.height == 0)
        {
            error = "rlhdr has zero size";
            return false;
        }
        const uint64_t expected =
            kRlHdrHeaderBytes + static_cast<uint64_t>(header.width) * header.height * 8ull;
        if (size != expected)
        {
            error = "rlhdr size mismatch";
            return false;
        }
        image.width = header.width;
        image.height = header.height;
        image.rgba16.resize(static_cast<size_t>(header.width) * header.height * 4u);
        input.read(
            reinterpret_cast<char*>(image.rgba16.data()),
            static_cast<std::streamsize>(image.rgba16.size() * sizeof(uint16_t)));
        if (!input)
        {
            error = "rlhdr pixels truncated";
            return false;
        }
        return true;
    }
}
