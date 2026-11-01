#ifndef XTEXTURE_BASIS_H
#define XTEXTURE_BASIS_H

// Basis Universal for the texture file (xtexture_file.h): UASTC blocks transcoded into the GPU's block format while a texture loads.
// UASTC is 4x4 blocks of 16 bytes, each one standing alone (no codebooks): any mip transcodes by itself - a texture's tail without its
// body, its body later - so streaming works as for a plain texture. Basis's own transcoder (its low-level per-block functions, no KTX2):
// one module defines XTEXTURE_BASIS_IMPLEMENTATION before including this to compile the transcoder into it (the texture loader's .cpp
// does it for each engine module; a tool or test that loads texture files does it once too).

#include "dependencies/xbitmap/source/xbitmap.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace xtexture_basis
{
    // the GPU format's bytes per 4x4 block when UASTC can be transcoded into it, else 0
    constexpr int TargetBlockBytes( xbitmap::format F ) noexcept
    {
        switch (F)
        {
        case xbitmap::format::BC1_4RGB:     return 8;
        case xbitmap::format::BC4_4R:       return 8;
        case xbitmap::format::BC3_8RGBA:    return 16;
        case xbitmap::format::BC5_8RG:      return 16;
        case xbitmap::format::BC7_8RGBA:    return 16;
        default:                            return 0;
        }
    }

    // One mip (Width x Height pixels) of UASTC blocks into Dest's blocks of the GPU format (TargetBlockBytes(F) each), rows of blocks
    // Row0 .. Row1 (the whole mip: 0 .. (Height + 3) / 4) - several calls on different rows can run at once. False: a bad block.
    bool TranscodeUASTC( std::span<const std::byte> Src, std::span<std::byte> Dest, std::uint32_t Width, std::uint32_t Height, xbitmap::format F, std::uint32_t Row0, std::uint32_t Row1 ) noexcept;
}
#endif

// (outside the header's guard: a module may include this before it defines XTEXTURE_BASIS_IMPLEMENTATION - the editor's preview does)
#if defined(XTEXTURE_BASIS_IMPLEMENTATION) && !defined(XTEXTURE_BASIS_IMPLEMENTED)
#define XTEXTURE_BASIS_IMPLEMENTED
    #ifndef BASISD_SUPPORT_KTX2_ZSTD
        #define BASISD_SUPPORT_KTX2_ZSTD 0          // (no KTX2 here: the file's own zstd is xserializer's)
    #endif
    #include "dependencies/basis_universal/transcoder/basisu_transcoder.cpp"
    #include <mutex>

namespace xtexture_basis
{
    bool TranscodeUASTC( std::span<const std::byte> Src, std::span<std::byte> Dest, std::uint32_t Width, std::uint32_t Height, xbitmap::format F, std::uint32_t Row0, std::uint32_t Row1 ) noexcept
    {
        static std::once_flag s_Init;
        std::call_once(s_Init, [] { basist::basisu_transcoder_init(); });

        const std::uint32_t BW = (Width + 3) / 4, BH = (Height + 3) / 4;
        const int           DB = TargetBlockBytes(F);
        if (DB == 0 || Src.size() < std::size_t(BW) * BH * 16 || Dest.size() < std::size_t(BW) * BH * DB) return false;
        const auto* pSrc = reinterpret_cast<const basist::uastc_block*>(Src.data());
        bool bOk = true;
        for (std::uint32_t y = Row0; y < std::min(Row1, BH); ++y)
            for (std::uint32_t x = 0; x < BW; ++x)
            {
                const auto& B     = pSrc[y * BW + x];
                void*       pDest = Dest.data() + (std::size_t(y) * BW + x) * DB;
                switch (F)
                {
                case xbitmap::format::BC1_4RGB:     bOk &= basist::transcode_uastc_to_bc1(B, pDest, true); break;
                case xbitmap::format::BC4_4R:       bOk &= basist::transcode_uastc_to_bc4(B, pDest, true, 0); break;
                case xbitmap::format::BC3_8RGBA:    bOk &= basist::transcode_uastc_to_bc3(B, pDest, true); break;
                case xbitmap::format::BC5_8RG:      bOk &= basist::transcode_uastc_to_bc5(B, pDest, true, 0, 1); break;
                default:                            bOk &= basist::transcode_uastc_to_bc7(B, pDest); break;
                }
            }
        return bOk;
    }
}
#endif
