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

    // ETC1S: the texture's shared codebooks and Huffman tables (stored once - a cube map's faces share them too - with face 0's smallest
    // mip, in the tail: every load has them), decoded once, then any (face, mip) transcoded by itself - several at once (each call its
    // own transcoder state). The codebooks' bytes:
    //   [endpoints count][endpoints bytes][selectors count][selectors bytes][tables bytes] (u32 each) then the three blocks of data
    // A (face, mip)'s bytes: [colour slice bytes (u32)][alpha slice bytes (u32)][colour slice][alpha slice] (zeros after: a mip's faces are one size)
    class etc1s
    {
    public:
                    etc1s       ( void ) noexcept = default;
                    etc1s       ( const etc1s& ) = delete;
                   ~etc1s       ( void ) noexcept;
        bool        Init        ( std::span<const std::byte> Codebooks ) noexcept;
        bool        Transcode   ( std::span<const std::byte> Mip, std::span<std::byte> Dest, std::uint32_t Width, std::uint32_t Height, std::uint32_t Level, xbitmap::format F ) const noexcept;
    private:
        void*       m_p = nullptr;
    };
}
#endif

// (outside the header's guard: a module may include this before it defines XTEXTURE_BASIS_IMPLEMENTATION - the editor's preview does)
#if defined(XTEXTURE_BASIS_IMPLEMENTATION) && !defined(XTEXTURE_BASIS_IMPLEMENTED)
#define XTEXTURE_BASIS_IMPLEMENTED
    #ifndef BASISD_SUPPORT_KTX2_ZSTD
        #define BASISD_SUPPORT_KTX2_ZSTD 0          // (no KTX2 here: the file's own zstd is xserializer's)
    #endif
    // (a build that compiles Basis's transcoder as a source of its own - the texture compiler, beside Basis's encoder - defines
    // XTEXTURE_BASIS_TRANSCODER_LINKED: only its header here)
    #if defined(XTEXTURE_BASIS_TRANSCODER_LINKED)
        #include "dependencies/basis_universal/transcoder/basisu_transcoder.h"
    #else
        #include "dependencies/basis_universal/transcoder/basisu_transcoder.cpp"
    #endif
    #include <cstring>
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

    etc1s::~etc1s( void ) noexcept { delete static_cast<basist::basisu_lowlevel_etc1s_transcoder*>(m_p); }

    bool etc1s::Init( std::span<const std::byte> Codebooks ) noexcept
    {
        static std::once_flag s_Init;
        std::call_once(s_Init, [] { basist::basisu_transcoder_init(); });
        if (Codebooks.size() < 20) return false;
        std::uint32_t H[5]; std::memcpy(H, Codebooks.data(), sizeof(H));              // endpoints count, bytes, selectors count, bytes, tables bytes
        if (std::uint64_t(20) + H[1] + H[3] + H[4] > Codebooks.size()) return false;
        const auto* pData = reinterpret_cast<const std::uint8_t*>(Codebooks.data()) + 20;
        auto* pT = new basist::basisu_lowlevel_etc1s_transcoder();
        delete static_cast<basist::basisu_lowlevel_etc1s_transcoder*>(m_p);
        m_p = pT;
        return pT->decode_palettes(H[0], pData, H[1], H[2], pData + H[1], H[3]) && pT->decode_tables(pData + H[1] + H[3], H[4]);
    }

    bool etc1s::Transcode( std::span<const std::byte> Mip, std::span<std::byte> Dest, std::uint32_t Width, std::uint32_t Height, std::uint32_t Level, xbitmap::format F ) const noexcept
    {
        const auto* pT = static_cast<const basist::basisu_lowlevel_etc1s_transcoder*>(m_p);
        if (!pT || Mip.size() < 8) return false;
        std::uint32_t Sizes[2]; std::memcpy(Sizes, Mip.data(), 8);
        const std::uint32_t RGB = Sizes[0], Alpha = Sizes[1];
        if (std::uint64_t(8) + RGB + Alpha > Mip.size()) return false;
        basist::transcoder_texture_format T;
        switch (F)
        {
        case xbitmap::format::BC1_4RGB:     T = basist::transcoder_texture_format::cTFBC1_RGB;  break;
        case xbitmap::format::BC3_8RGBA:    T = basist::transcoder_texture_format::cTFBC3_RGBA; break;
        case xbitmap::format::BC4_4R:       T = basist::transcoder_texture_format::cTFBC4_R;    break;
        case xbitmap::format::BC5_8RG:      T = basist::transcoder_texture_format::cTFBC5_RG;   break;
        case xbitmap::format::BC7_8RGBA:    T = basist::transcoder_texture_format::cTFBC7_RGBA; break;
        default:                            return false;
        }
        const std::uint32_t BW = (Width + 3) / 4, BH = (Height + 3) / 4;
        if (Dest.size() < std::size_t(BW) * BH * TargetBlockBytes(F)) return false;
        basist::basisu_transcoder_state State;                      // (one per call: calls may run at once on the same codebooks)
        return const_cast<basist::basisu_lowlevel_etc1s_transcoder*>(pT)->transcode_image(T, Dest.data(), BW * BH
            , reinterpret_cast<const std::uint8_t*>(Mip.data()), static_cast<std::uint32_t>(Mip.size()), BW, BH, Width, Height, Level
            , 8, RGB, 8 + std::uint64_t(RGB), Alpha, 0, Alpha != 0, false, 0, &State);
    }
}
#endif
