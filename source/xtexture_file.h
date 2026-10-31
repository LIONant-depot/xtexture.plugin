#ifndef XTEXTURE_FILE_H
#define XTEXTURE_FILE_H
#pragma once

// The compiled texture file, version 2: the resource file (part 0) and its ".1" (part 1), both xserializer streams.
//   part 0: the header, the mip sizes, and the tail - the mips of 128x128 and below - as an xbitmap of its own
//   part 1: the bigger mips in one pack, smallest first (so "down to mip k" is a prefix of it), a cube map's faces of a mip together
// No part 1 when every mip is in the tail. Part 1 carries the generation of the compile that wrote it: a part 0 of another compile
// (a hot reload in the middle of a load) rejects it. Version 1 (a plain xbitmap in the resource file) still loads.
// The compiler writes with Save, the runtime loader reads with Load (xtexture_xgpu_rsc_loader.cpp).

#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "dependencies/xserializer/source/xserializer.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>
#include <span>
#include <vector>

namespace xtexture_file
{
    inline constexpr std::uint32_t  tail_max_size_v = 128;          // a mip this size or smaller (both sides) is in the tail
    inline constexpr int            max_mips_v      = 16;

    struct part0
    {
        static constexpr std::uint16_t      xserializer_version_v = 3;     // (a plain xbitmap, version 1 files, is 2)

        xbitmap                             m_Tail;                         // mips m_nPart1Mips .. m_nMips-1 (all of them when there is no part 1)
        std::uint64_t                       m_Generation    {};             // part 1 must carry the same
        std::array<std::uint32_t, max_mips_v> m_Part1MipSize{};             // one face's bytes of each part 1 mip (by mip number)
        std::uint16_t                       m_Width         {};             // the whole texture
        std::uint16_t                       m_Height        {};
        std::uint8_t                        m_nMips         {};
        std::uint8_t                        m_nPart1Mips    {};             // mips 0 .. m_nPart1Mips-1 are in part 1
    };

    struct part1
    {
        static constexpr std::uint16_t      xserializer_version_v = 1;

        std::byte*                          m_pData         {};             // mips m_nPart1Mips-1 .. 0, each with its faces together
        std::uint64_t                       m_DataSize      {};
        std::uint64_t                       m_Generation    {};
    };

    //------------------------------------------------------------------------------------------------------------
    // Writes a texture (one frame; every mip of a face together, the xbitmap layout) as part 0 + part 1. Part 1 first, then part 0
    // through a temporary file and a rename, so a reader never sees a part 0 whose part 1 is not there yet. A part 1 left from an
    // older compile is deleted when this one needs none.
    inline xerr Save(const xbitmap& Bitmap, const std::wstring_view Part0Path, const std::wstring_view Part1Path, xserializer::compression_level Level) noexcept
    {
        const int nMips = Bitmap.getMipCount(), nFaces = Bitmap.getFaceCount();
        if (Bitmap.getFrameCount() != 1 || nMips > max_mips_v) return xerr::create_f<xserializer::state, "A texture file has one frame and at most 16 mips">();

        part0 P0;
        P0.m_Width  = static_cast<std::uint16_t>(Bitmap.getWidth());
        P0.m_Height = static_cast<std::uint16_t>(Bitmap.getHeight());
        P0.m_nMips  = static_cast<std::uint8_t>(nMips);
        while (P0.m_nPart1Mips < nMips - 1 && std::max(Bitmap.getWidth() >> P0.m_nPart1Mips, Bitmap.getHeight() >> P0.m_nPart1Mips) > tail_max_size_v) ++P0.m_nPart1Mips;
        P0.m_Generation = std::random_device{}() ^ (std::uint64_t(std::random_device{}()) << 32) ^ std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());

        // part 1: the big mips, smallest first
        std::uint64_t Part1Face = 0;
        for (int m = 0; m < P0.m_nPart1Mips; ++m) Part1Face += P0.m_Part1MipSize[m] = Bitmap.getMipSize(m);
        if (P0.m_nPart1Mips)
        {
            std::vector<std::byte> Data(Part1Face * nFaces);
            std::size_t            At = 0;
            for (int m = P0.m_nPart1Mips - 1; m >= 0; --m)
                for (int f = 0; f < nFaces; ++f)
                {
                    const auto Mip = Bitmap.getMip<std::byte>(m, f);
                    std::memcpy(&Data[At], Mip.data(), Mip.size()); At += Mip.size();
                }
            part1 P1{ Data.data(), Data.size(), P0.m_Generation };
            if (auto Err = xserializer::stream{}.Save(Part1Path, P1, Level); Err) return Err;
        }
        else
        {
            std::error_code Ec; std::filesystem::remove(std::filesystem::path(Part1Path), Ec);
        }

        // part 0: the tail as a bitmap of its own (offsets relative to its first mip)
        const int                 nTail    = nMips - P0.m_nPart1Mips;
        const std::uint64_t       TailFace = Bitmap.getFaceSize() - Part1Face;
        std::vector<std::byte>    TailData(nTail * sizeof(std::int32_t) + TailFace * nFaces);
        auto*                     pOffset  = reinterpret_cast<std::int32_t*>(TailData.data());
        std::byte*                pPixels  = TailData.data() + nTail * sizeof(std::int32_t);
        for (int t = 0, Offset = 0; t < nTail; Offset += Bitmap.getMipSize(P0.m_nPart1Mips + t), ++t) pOffset[t] = Offset;
        for (int f = 0; f < nFaces; ++f)
            for (int t = 0; t < nTail; ++t)
            {
                const auto Mip = Bitmap.getMip<std::byte>(P0.m_nPart1Mips + t, f);
                std::memcpy(pPixels + f * TailFace + pOffset[t], Mip.data(), Mip.size());
            }
        P0.m_Tail.setup(std::max(1u, Bitmap.getWidth() >> P0.m_nPart1Mips), std::max(1u, Bitmap.getHeight() >> P0.m_nPart1Mips), Bitmap.getFormat()
                       , TailFace, TailData, false, nTail, 1, Bitmap.isCubemap());
        const bool bCube = Bitmap.isCubemap();
        P0.m_Tail.m_Flags      = Bitmap.m_Flags;
        P0.m_Tail.m_Flags.m_bOwnsMemory = false;
        P0.m_Tail.m_Flags.m_bCubeMap    = bCube;
        P0.m_Tail.m_ClampColor = Bitmap.m_ClampColor;

        const std::wstring Temp = std::format(L"{}.tmp", Part0Path);
        if (auto Err = xserializer::stream{}.Save(Temp, P0, Level); Err) return Err;
        std::error_code Ec; std::filesystem::rename(std::filesystem::path(Temp), std::filesystem::path(Part0Path), Ec);
        if (Ec) return xerr::create_f<xserializer::state, "Could not replace the texture file">();
        return {};
    }

    //------------------------------------------------------------------------------------------------------------
    // A loaded texture, all of it views of memory freed with this object:
    //   m_MipFaceData: the pixels, one piece per face and mip (face * mips + mip) - what the GPU upload takes (no copy in between)
    //   m_Bitmap:      size, format, faces, flags; its pixels too only when Load was asked to assemble them (else m_pData is null)
    struct loaded
    {
        xbitmap                                 m_Bitmap;
        std::vector<std::span<const std::byte>> m_MipFaceData;
        std::array<void*, 4>                    m_pFree {};
        bool                                    m_bPart1Skipped = false;   // the texture has a part 1, not read (Part1Path empty): m_Bitmap is its tail

        ~loaded() noexcept { for (void* p : m_pFree) if (p) xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, p); }
    };

    // Reads a texture file, version 2 (part 0 + part 1) or version 1 (a plain xbitmap).
    //   ParallelFor (optional): the big pack's blocks are decoded at once through it (see xserializer::stream::setParallelFor)
    //   bAssemble:              a v2 texture with a part 1 also gets its pixels in m_Bitmap in the xbitmap layout (one more copy; tools)
    //   Part1Path empty:        the tail only (m_bPart1Skipped tells whether there is more)
    inline xerr Load(const std::wstring_view Part0Path, const std::wstring_view Part1Path, loaded& Out, const xserializer::stream::parallel_for& ParallelFor = {}, bool bAssemble = true) noexcept
    {
        // a bitmap that holds its pixels: the pieces point into it
        const auto PiecesOf = [&](const xbitmap& B)
        {
            Out.m_MipFaceData.clear();
            for (int f = 0; f < B.getFaceCount(); ++f) for (int m = 0; m < B.getMipCount(); ++m) Out.m_MipFaceData.push_back(B.getMip<std::byte>(m, f));
        };
        const auto View = [&](const xbitmap& B) { std::memcpy(static_cast<void*>(&Out.m_Bitmap), &B, sizeof(xbitmap)); Out.m_Bitmap.m_Flags.m_bOwnsMemory = false; PiecesOf(Out.m_Bitmap); };

        part0* pP0 = nullptr;
        if (auto Err = xserializer::stream{}.Load(Part0Path, pP0); Err)
        {
            if (Err.getState<xserializer::state>() != xserializer::state::WRONG_VERSION) return Err;

            // version 1: the bitmap as it is
            xbitmap* pB = nullptr;
            xserializer::stream S1; S1.setParallelFor(ParallelFor);
            if (auto Err1 = S1.Load(Part0Path, pB); Err1) return Err1;
            Out.m_pFree = { pB, pB->m_pData };
            View(*pB);
            return {};
        }
        Out.m_pFree = { pP0, pP0->m_Tail.m_pData };
        const xbitmap& Tail = pP0->m_Tail;

        if (pP0->m_nPart1Mips == 0 || Part1Path.empty()) { Out.m_bPart1Skipped = pP0->m_nPart1Mips != 0; View(Tail); return {}; }

        part1* pP1 = nullptr;
        xserializer::stream S1; S1.setParallelFor(ParallelFor);
        if (auto Err = S1.Load(Part1Path, pP1); Err) return Err;
        Out.m_pFree[2] = pP1; Out.m_pFree[3] = pP1->m_pData;
        if (pP1->m_Generation != pP0->m_Generation) return xerr::create_f<xserializer::state, "The texture's part 1 is from another compile">();

        const int           nMips = pP0->m_nMips, nP1 = pP0->m_nPart1Mips, nFaces = Tail.getFaceCount();
        std::uint64_t       Part1Face = 0;
        for (int m = 0; m < nP1; ++m) Part1Face += pP0->m_Part1MipSize[m];
        const std::uint64_t FaceSize = Part1Face + Tail.getFaceSize();
        if (pP1->m_DataSize != Part1Face * nFaces) return xerr::create_f<xserializer::state, "The texture's part 1 has the wrong size">();

        // the pieces: the part 1 mips where part 1 has them (smallest first, a mip's faces together), then the tail's
        Out.m_MipFaceData.assign(std::size_t(nFaces) * nMips, {});
        {
            const std::byte* pSrc = pP1->m_pData;
            for (int m = nP1 - 1; m >= 0; --m)
                for (int f = 0; f < nFaces; ++f) { Out.m_MipFaceData[f * nMips + m] = { pSrc, pP0->m_Part1MipSize[m] }; pSrc += pP0->m_Part1MipSize[m]; }
            for (int f = 0; f < nFaces; ++f)
                for (int t = 0; t < nMips - nP1; ++t) Out.m_MipFaceData[f * nMips + nP1 + t] = Tail.getMip<std::byte>(t, f);
        }

        // the description (no pixels unless assembled)
        Out.m_Bitmap.m_pData      = nullptr;
        Out.m_Bitmap.m_DataSize   = 0;
        Out.m_Bitmap.m_FaceSize   = static_cast<std::uint32_t>(FaceSize);
        Out.m_Bitmap.m_Width      = pP0->m_Width;
        Out.m_Bitmap.m_Height     = pP0->m_Height;
        Out.m_Bitmap.m_nMips      = static_cast<std::uint8_t>(nMips);
        Out.m_Bitmap.m_Flags      = Tail.m_Flags;
        Out.m_Bitmap.m_Flags.m_bOwnsMemory = false;
        Out.m_Bitmap.m_ClampColor = Tail.m_ClampColor;
        if (!bAssemble && Tail.getFormat() != xbitmap::format::R8G8B8) return {};       // (xGPU expands R8G8B8 to RGBA from a whole bitmap)

        // assembled: per face, every mip in order, after the mip offset table (the xbitmap layout)
        const std::size_t   Size    = nMips * sizeof(std::int32_t) + FaceSize * nFaces;
        auto* const         pData   = static_cast<std::byte*>(xserializer::default_memory_handler_v.Allocate(xserializer::mem_type{ .m_bUnique = true }, Size, 16));
        auto* const         pOffset = reinterpret_cast<std::int32_t*>(pData);
        std::byte* const    pPixels = pData + nMips * sizeof(std::int32_t);
        for (int m = 0, Offset = 0; m < nMips; Offset += static_cast<int>(Out.m_MipFaceData[m].size()), ++m) pOffset[m] = Offset;
        for (int f = 0; f < nFaces; ++f)
            for (int m = 0; m < nMips; ++m) std::memcpy(pPixels + f * FaceSize + pOffset[m], Out.m_MipFaceData[f * nMips + m].data(), Out.m_MipFaceData[f * nMips + m].size());
        xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, pP1->m_pData);
        Out.m_pFree[3] = pData;

        const auto Flags = Out.m_Bitmap.m_Flags; const auto Clamp = Out.m_Bitmap.m_ClampColor;
        Out.m_Bitmap.m_pData = nullptr;
        Out.m_Bitmap.setup(pP0->m_Width, pP0->m_Height, Tail.getFormat(), FaceSize, { pData, Size }, false, nMips, 1, Tail.isCubemap());
        Out.m_Bitmap.m_Flags = Flags; Out.m_Bitmap.m_ClampColor = Clamp;
        PiecesOf(Out.m_Bitmap);
        return {};
    }
}

//-------------------------------------------------------------------------------
namespace xserializer::io_functions
{
    template<> inline
    xerr SerializeIO<xtexture_file::part0>(xserializer::stream& Stream, const xtexture_file::part0& P) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(P.m_Tail))
        || (Err = Stream.Serialize(P.m_Generation))
        || (Err = Stream.Serialize(P.m_Part1MipSize))
        || (Err = Stream.Serialize(P.m_Width))
        || (Err = Stream.Serialize(P.m_Height))
        || (Err = Stream.Serialize(P.m_nMips))
        || (Err = Stream.Serialize(P.m_nPart1Mips))
        ;
        return Err;
    }

    template<> inline
    xerr SerializeIO<xtexture_file::part1>(xserializer::stream& Stream, const xtexture_file::part1& P) noexcept
    {
        xerr Err;
        false
        || (Err = Stream.Serialize(P.m_pData, P.m_DataSize, mem_type{ .m_bUnique = true }))
        || (Err = Stream.Serialize(P.m_DataSize))
        || (Err = Stream.Serialize(P.m_Generation))
        ;
        return Err;
    }
}
#endif
