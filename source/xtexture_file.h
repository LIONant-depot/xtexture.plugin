#ifndef XTEXTURE_FILE_H
#define XTEXTURE_FILE_H
#pragma once

// The compiled texture file, version 2: the resource file (part 0) and its ".1" (part 1), both xserializer streams.
//   part 0: the header, the mip sizes, and the tail - the mips of 128x128 and below - as an xbitmap of its own
//   part 1: the bigger mips in one pack, smallest first (so "down to mip k" is a prefix of it), a cube map's faces of a mip together
// Version 4: how the mips are stored - the GPU format's own blocks, or Basis UASTC blocks (super compression, xtexture_basis.h) that
// Load transcodes into the GPU format; version 3 files (no such field: native) still load.
// No part 1 when every mip is in the tail. Part 1 carries the generation of the compile that wrote it: a part 0 of another compile
// (a hot reload in the middle of a load) rejects it. Version 1 (a plain xbitmap in the resource file) still loads.
// The compiler writes with Save, the runtime loader reads with Load (xtexture_xgpu_rsc_loader.cpp).

#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "dependencies/xserializer/source/xserializer.h"
#include "xtexture_basis.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <random>
#include <span>
#include <thread>
#include <vector>

// The profiler (xprofile, Tracy): the file's steps as zones, the disk read vs the decode as plots - where xprofile is part of the build
// (the tools that read texture files are built without it: empty macros)
#if __has_include("xprofile.h")
    #include "xprofile.h"
#else
    #ifndef XPROFILE_SCOPE
        #define XPROFILE_SCOPE(NAME)
    #endif
    #ifndef XPROFILE_PLOT
        #define XPROFILE_PLOT(NAME, VALUE) do {} while (0)
    #endif
    #ifndef XPROFILE_PLOT_LIMIT
        #define XPROFILE_PLOT_LIMIT(NAME, MAX) do {} while (0)
    #endif
#endif

namespace xtexture_file
{
    inline constexpr std::uint32_t  tail_max_size_v = 128;          // a mip this size or smaller (both sides) is in the tail
    inline constexpr int            max_mips_v      = 16;

    // how the file's mips are stored
    enum class stored_encoding : std::uint8_t
    { NATIVE                                                                // the GPU format's own blocks
    , UASTC                                                                 // Basis UASTC blocks (16 bytes per 4x4, the tail's xbitmap says BC7): transcoded into m_GpuFormat
    };

    struct part0
    {
        static constexpr std::uint16_t      xserializer_version_v = 4;     // (version 3: no encoding; a plain xbitmap, version 1 files, is 2)

        xbitmap                             m_Tail;                         // mips m_nPart1Mips .. m_nMips-1 (all of them when there is no part 1)
        std::uint64_t                       m_Generation    {};             // part 1 must carry the same
        std::array<std::uint32_t, max_mips_v> m_Part1MipSize{};             // one face's bytes of each part 1 mip (by mip number)
        std::uint16_t                       m_Width         {};             // the whole texture
        std::uint16_t                       m_Height        {};
        std::uint8_t                        m_nMips         {};
        std::uint8_t                        m_nPart1Mips    {};             // mips 0 .. m_nPart1Mips-1 are in part 1
        stored_encoding                     m_Encoding      { stored_encoding::NATIVE };
        std::uint32_t                       m_GpuFormat     {};             // the xbitmap::format the GPU gets (the tail's own format when NATIVE)
    };

    // a version 3 part 0 (no encoding: native) - the same memory, read without the last two fields
    struct part0_v3 : part0
    {
        static constexpr std::uint16_t      xserializer_version_v = 3;
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
    //   Encoding UASTC: Bitmap holds UASTC blocks (its format BC7: the same 16 bytes per 4x4 block), GpuFormat what Load transcodes them into
    inline xerr Save(const xbitmap& Bitmap, const std::wstring_view Part0Path, const std::wstring_view Part1Path, xserializer::compression_level Level
                    , stored_encoding Encoding = stored_encoding::NATIVE, xbitmap::format GpuFormat = xbitmap::format::INVALID) noexcept
    {
        const int nMips = Bitmap.getMipCount(), nFaces = Bitmap.getFaceCount();
        if (Bitmap.getFrameCount() != 1 || nMips > max_mips_v) return xerr::create_f<xserializer::state, "A texture file has one frame and at most 16 mips">();

        part0 P0;
        P0.m_Width  = static_cast<std::uint16_t>(Bitmap.getWidth());
        P0.m_Height = static_cast<std::uint16_t>(Bitmap.getHeight());
        P0.m_nMips  = static_cast<std::uint8_t>(nMips);
        P0.m_Encoding  = Encoding;
        P0.m_GpuFormat = static_cast<std::uint32_t>(Encoding == stored_encoding::NATIVE || GpuFormat == xbitmap::format::INVALID ? Bitmap.getFormat() : GpuFormat);
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
        // the whole texture's shape, whatever was read (a v1 file: all its mips, none in a part 1)
        std::uint16_t                           m_Width = 0, m_Height = 0;
        std::uint8_t                            m_nMips = 0, m_nPart1Mips = 0, m_nFaces = 0;
        std::array<std::uint32_t, max_mips_v>   m_Part1MipSize{};           // one face's bytes of each part 1 mip

        ~loaded() noexcept { for (void* p : m_pFree) if (p) xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, p); }
    };

    // Reads a texture file, version 2 (part 0 + part 1) or version 1 (a plain xbitmap).
    //   ParallelFor (optional): the big pack's blocks are decoded at once through it (see xserializer::stream::setParallelFor)
    //   bAssemble:              a v2 texture with a part 1 also gets its pixels in m_Bitmap in the xbitmap layout (one more copy; tools)
    //   Part1Path empty:        the tail only (m_bPart1Skipped tells whether there is more)
    //   pPart1Buffer:           part 1 read whole at the disk's speed into it first (xfile::ReadWholeFile: no file cache, parallel
    //                           requests), then parsed from memory - the loader's job passes its thread's buffer; tools pass none
    inline xerr Load(const std::wstring_view Part0Path, const std::wstring_view Part1Path, loaded& Out, const xserializer::stream::parallel_for& ParallelFor = {}, bool bAssemble = true, xfile::read_buffer* pPart1Buffer = nullptr) noexcept
    {
        // a bitmap that holds its pixels: the pieces point into it
        const auto PiecesOf = [&](const xbitmap& B)
        {
            Out.m_MipFaceData.clear();
            for (int f = 0; f < B.getFaceCount(); ++f) for (int m = 0; m < B.getMipCount(); ++m) Out.m_MipFaceData.push_back(B.getMip<std::byte>(m, f));
        };
        const auto View = [&](const xbitmap& B) { std::memcpy(static_cast<void*>(&Out.m_Bitmap), &B, sizeof(xbitmap)); Out.m_Bitmap.m_Flags.m_bOwnsMemory = false; PiecesOf(Out.m_Bitmap); };

        part0* pP0 = nullptr;
        XPROFILE_SCOPE("Texture file");
        auto LoadP0 = [&]() -> xerr
        {
            XPROFILE_SCOPE("Texture part 0");
            auto Err = xserializer::stream{}.Load(Part0Path, pP0);
            if (!Err || Err.getState<xserializer::state>() != xserializer::state::WRONG_VERSION) return Err;
            part0_v3* pV3 = nullptr;                                            // version 3: native blocks
            if (auto Err3 = xserializer::stream{}.Load(Part0Path, pV3); Err3) return Err3;
            pP0 = pV3;
            pP0->m_Encoding  = stored_encoding::NATIVE;
            pP0->m_GpuFormat = static_cast<std::uint32_t>(pP0->m_Tail.getFormat());
            return {};
        };
        if (auto Err = LoadP0(); Err)
        {
            if (Err.getState<xserializer::state>() != xserializer::state::WRONG_VERSION) return Err;

            // version 1: the bitmap as it is
            xbitmap* pB = nullptr;
            xserializer::stream S1; S1.setParallelFor(ParallelFor);
            if (auto Err1 = S1.Load(Part0Path, pB); Err1) return Err1;
            Out.m_pFree = { pB, pB->m_pData };
            View(*pB);
            Out.m_Width = std::uint16_t(pB->getWidth()); Out.m_Height = std::uint16_t(pB->getHeight()); Out.m_nMips = std::uint8_t(pB->getMipCount()); Out.m_nFaces = std::uint8_t(pB->getFaceCount());
            return {};
        }
        Out.m_pFree = { pP0, pP0->m_Tail.m_pData };
        const xbitmap& Tail = pP0->m_Tail;
        Out.m_Width = pP0->m_Width; Out.m_Height = pP0->m_Height; Out.m_nMips = pP0->m_nMips; Out.m_nPart1Mips = pP0->m_nPart1Mips;
        Out.m_nFaces = std::uint8_t(Tail.getFaceCount()); Out.m_Part1MipSize = pP0->m_Part1MipSize;

        // UASTC: the pieces read (every mip, or the tail's) transcoded into the GPU format, in the xbitmap layout of one allocation
        // that replaces the UASTC one - m_Bitmap then describes the GPU format's texture (its pixels too)
        const auto Transcode = [&]() -> xerr
        {
            if (pP0->m_Encoding != stored_encoding::UASTC) return {};
            XPROFILE_SCOPE("Texture UASTC transcode");
            const auto  F      = static_cast<xbitmap::format>(pP0->m_GpuFormat);
            const int   DB     = xtexture_basis::TargetBlockBytes(F);
            if (DB == 0) return xerr::create_f<xserializer::state, "The texture's UASTC cannot be transcoded into its GPU format">();
            const int   nFaces = Out.m_nFaces, nLocal = static_cast<int>(Out.m_MipFaceData.size()) / std::max(1, nFaces);
            const int   Base   = Out.m_bPart1Skipped ? pP0->m_nPart1Mips : 0;     // the first mip read
            const auto  Dim    = [&](int m, bool bH) { return std::max(1u, std::uint32_t(bH ? pP0->m_Height : pP0->m_Width) >> (Base + m)); };
            const auto  Bytes  = [&](int m) { return std::size_t((Dim(m, false) + 3) / 4) * ((Dim(m, true) + 3) / 4) * DB; };
            std::size_t FaceSize = 0;
            for (int m = 0; m < nLocal; ++m) FaceSize += Bytes(m);
            const std::size_t Size  = nLocal * sizeof(std::int32_t) + FaceSize * nFaces;
            auto* const pData   = static_cast<std::byte*>(xserializer::default_memory_handler_v.Allocate(xserializer::mem_type{ .m_bUnique = true }, Size, 16));
            auto* const pOffset = reinterpret_cast<std::int32_t*>(pData);
            std::byte* const pPixels = pData + nLocal * sizeof(std::int32_t);
            for (int m = 0, Offset = 0; m < nLocal; Offset += static_cast<int>(Bytes(m)), ++m) pOffset[m] = Offset;
            // a job per (face, mip) piece; the big mips cut in bands of 64 block rows (several jobs on one mip)
            struct job { int f, m; std::uint32_t r0, r1; };
            std::vector<job> Jobs;
            for (int f = 0; f < nFaces; ++f) for (int m = 0; m < nLocal; ++m)
            {
                const std::uint32_t BH = (Dim(m, true) + 3) / 4;
                for (std::uint32_t r = 0; r < BH; r += 64) Jobs.push_back({ f, m, r, std::min(BH, r + 64) });
            }
            std::atomic<bool> bOk{ true };
            const auto Run = [&](std::size_t i)
            {
                const auto& J = Jobs[i];
                std::span<std::byte> Dest{ pPixels + J.f * FaceSize + pOffset[J.m], Bytes(J.m) };
                if (!xtexture_basis::TranscodeUASTC(Out.m_MipFaceData[J.f * nLocal + J.m], Dest, Dim(J.m, false), Dim(J.m, true), F, J.r0, J.r1)) bOk = false;
            };
            if (ParallelFor && Jobs.size() > 1) ParallelFor(Jobs.size(), Run);
            else for (std::size_t i = 0; i < Jobs.size(); ++i) Run(i);
            if (!bOk) { xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, pData); return xerr::create_f<xserializer::state, "The texture's UASTC blocks did not transcode">(); }

            const auto Flags = Out.m_Bitmap.m_Flags; const auto Clamp = Out.m_Bitmap.m_ClampColor;
            if (Out.m_pFree[3]) xserializer::default_memory_handler_v.Free(xserializer::mem_type{ .m_bUnique = true }, Out.m_pFree[3]);   // (part 1's UASTC: transcoded)
            Out.m_pFree[3] = pData;
            Out.m_Bitmap.m_pData = nullptr;
            Out.m_Bitmap.setup(Dim(0, false), Dim(0, true), F, FaceSize, { pData, Size }, false, nLocal, 1, Tail.isCubemap());
            Out.m_Bitmap.m_Flags = Flags; Out.m_Bitmap.m_Flags.m_bOwnsMemory = false; Out.m_Bitmap.m_ClampColor = Clamp;
            PiecesOf(Out.m_Bitmap);
            for (int m = 0; m < pP0->m_nPart1Mips; ++m)                    // (part 1's sizes as the GPU gets them: residency counts these)
                Out.m_Part1MipSize[m] = static_cast<std::uint32_t>(std::size_t((std::max(1u, std::uint32_t(pP0->m_Width) >> m) + 3) / 4) * ((std::max(1u, std::uint32_t(pP0->m_Height) >> m) + 3) / 4) * DB);
            return {};
        };

        if (pP0->m_nPart1Mips == 0 || Part1Path.empty()) { Out.m_bPart1Skipped = pP0->m_nPart1Mips != 0; View(Tail); return Transcode(); }

        part1* pP1 = nullptr;
        xserializer::stream S1; S1.setParallelFor(ParallelFor);
        if (pPart1Buffer)
        {
            // (plots: the disk's MB/s of the file as stored, the decode's MB/s of what it gives - which one a texture waits on)
            std::span<const std::byte> Bytes;
            const auto t0 = std::chrono::steady_clock::now();
            if (auto Err = [&] { XPROFILE_SCOPE("Texture part 1: disk read"); return xfile::ReadWholeFile(Part1Path, *pPart1Buffer, Bytes); }(); Err) return Err;
            const auto t1 = std::chrono::steady_clock::now();
            if (auto Err = [&] { XPROFILE_SCOPE("Texture part 1: decode"); return S1.Load(xfile::BufferPath(Bytes), pP1); }(); Err) return Err;
            const auto t2 = std::chrono::steady_clock::now();
            const double ReadS = std::chrono::duration<double>(t1 - t0).count(), DecodeS = std::chrono::duration<double>(t2 - t1).count();
            if (ReadS > 0)   XPROFILE_PLOT("Texture disk read MB/s", Bytes.size() / 1048576.0 / ReadS);
            if (DecodeS > 0) XPROFILE_PLOT("Texture decode MB/s", pP1->m_DataSize / 1048576.0 / DecodeS);
            // their theoretical maxima (the Profiler tab's Max scale): the disk - the reference PC's NVMe (Samsung 980 PRO) is rated
            // 7000 MB/s (6.6 GB/s measured with 4+ requests); the decode - what loading may use while the game runs, not every core
            // (Tomas: it is never 100% decompressing): half the cores at ~1 GB/s each (zstd on BC data, one thread, measured 2026-10-31)
            // ponytail: the disk's number is the reference PC's; a per-machine measurement (xfile) when other machines matter
            static const double s_DecodeLimit = std::max(1u, std::thread::hardware_concurrency() / 2) * 1000.0;
            XPROFILE_PLOT_LIMIT("Texture disk read MB/s", 7000);
            XPROFILE_PLOT_LIMIT("Texture decode MB/s", s_DecodeLimit);
        }
        else if (auto Err = S1.Load(Part1Path, pP1); Err) return Err;
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
        if (pP0->m_Encoding == stored_encoding::UASTC) return Transcode();           // (the transcode assembles: one allocation in the xbitmap layout)
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
        || (Err = Stream.Serialize(reinterpret_cast<const std::uint8_t&>(P.m_Encoding)))
        || (Err = Stream.Serialize(P.m_GpuFormat))
        ;
        return Err;
    }

    template<> inline
    xerr SerializeIO<xtexture_file::part0_v3>(xserializer::stream& Stream, const xtexture_file::part0_v3& P) noexcept
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
