// The texture file (xtexture_file.h): written and read back byte for byte - tail only, with a part 1, a cube map; a part 1 from another
// compile is rejected; a version 1 file (a plain xbitmap) still loads. Built and run by xLION_Docs/tools/xtexture_file_test.py.
#define XTEXTURE_BASIS_IMPLEMENTATION       // (the Basis transcoder in this test: files stored as UASTC / ETC1S / UASTC HDR are transcoded when they load)
#include "plugins/xtexture.plugin/source/xtexture_file.h"
#include <cstdio>
#if defined(_WIN32)
#include <crtdbg.h>
// an assert or an abort ends the test with a message on stderr, never a box waiting for someone to press a button
static const bool s_bNoDialogs = []
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int Type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) { _CrtSetReportMode(Type, _CRTDBG_MODE_FILE); _CrtSetReportFile(Type, _CRTDBG_FILE_STDERR); }
    return true;
}();
#endif
#include <filesystem>
#include <vector>

static int s_Failures = 0;
#define CHECK(X) do { if (!(X)) { std::printf("FAILED %s:%d  %s\n", __FILE__, __LINE__, #X); ++s_Failures; } } while (0)

// An RGBA8 bitmap with a full mip chain and every byte set from its position (so a mip in the wrong place shows)
static std::vector<std::byte> MakeBitmap(xbitmap& B, std::uint32_t W, std::uint32_t H, int nFaces)
{
    int nMips = 1; while (std::max(W >> (nMips - 1), H >> (nMips - 1)) > 1) ++nMips;
    std::uint64_t Face = 0; for (int m = 0; m < nMips; ++m) Face += std::uint64_t(std::max(1u, W >> m)) * std::max(1u, H >> m) * 4;
    std::vector<std::byte> Data(nMips * sizeof(std::int32_t) + Face * nFaces);
    auto* pOffset = reinterpret_cast<std::int32_t*>(Data.data());
    for (int m = 0, o = 0; m < nMips; o += std::max(1u, W >> m) * std::max(1u, H >> m) * 4, ++m) pOffset[m] = o;
    for (std::size_t i = nMips * sizeof(std::int32_t); i < Data.size(); ++i) Data[i] = std::byte(i * 131 + (i >> 9));
    B.setup(W, H, xbitmap::format::XCOLOR, Face, Data, false, nMips, 1, nFaces == 6);
    B.setUWrapMode(xbitmap::wrap_mode::MIRROR);         // (not the default: the flags must carry over)
    return Data;
}

static bool Same(const xbitmap& A, const xbitmap& B)
{
    return A.getWidth() == B.getWidth() && A.getHeight() == B.getHeight() && A.getMipCount() == B.getMipCount() && A.getFaceCount() == B.getFaceCount()
        && A.getFormat() == B.getFormat() && A.getUWrapMode() == B.getUWrapMode() && A.isCubemap() == B.isCubemap()
        && A.getFrameSize() == B.getFrameSize() && 0 == std::memcmp(A.getMip<std::byte>(0).data(), B.getMip<std::byte>(0).data(), A.getFrameSize());
}

static void RoundTrip(const wchar_t* pName, std::uint32_t W, std::uint32_t H, int nFaces, bool bExpectPart1)
{
    const auto  Dir = std::filesystem::temp_directory_path() / L"xtexture_file_unittest";
    std::filesystem::create_directories(Dir);
    const auto  P0 = (Dir / pName).wstring(), P1 = P0 + L".1";

    xbitmap B; auto Keep = MakeBitmap(B, W, H, nFaces);
    for (auto Level : { xserializer::compression_level::FAST, xserializer::compression_level::MEDIUM })
    {
        CHECK(!xtexture_file::Save(B, P0, P1, Level));
        CHECK(std::filesystem::exists(P1) == bExpectPart1);
        xtexture_file::loaded L;
        CHECK(!xtexture_file::Load(P0, P1, L));
        CHECK(Same(B, L.m_Bitmap));

        // as the runtime loads it: not assembled, one piece per face and mip, each the bitmap's mip byte for byte
        xtexture_file::loaded R;
        CHECK(!xtexture_file::Load(P0, P1, R, {}, false));
        CHECK(R.m_MipFaceData.size() == std::size_t(B.getFaceCount() * B.getMipCount()));
        for (int f = 0; f < B.getFaceCount(); ++f)
            for (int m = 0; m < B.getMipCount(); ++m)
            {
                const auto& Piece = R.m_MipFaceData[f * B.getMipCount() + m];
                const auto  Mip   = B.getMip<std::byte>(m, f);
                CHECK(Piece.size() == Mip.size() && 0 == std::memcmp(Piece.data(), Mip.data(), Mip.size()));
            }
        CHECK(R.m_Bitmap.getWidth() == B.getWidth() && R.m_Bitmap.getMipCount() == B.getMipCount() && R.m_Bitmap.getFormat() == B.getFormat() && R.m_Bitmap.isCubemap() == B.isCubemap());

        // the blocks decoded through a parallel-for (xserializer's parallel path, packs of 8+ blocks), its jobs run last first: each
        // job writes only its own blocks, so the order must not matter
        int nJobs = 0;
        xtexture_file::loaded P;
        CHECK(!xtexture_file::Load(P0, P1, P, [&](std::size_t n, const std::function<void(std::size_t)>& Job) { nJobs += int(n); for (std::size_t i = n; i-- > 0;) Job(i); }));
        CHECK(Same(B, P.m_Bitmap));
        // (fixed blocks hold 64 KB of input each, so 8+ blocks; MEDIUM's dynamic blocks hold up to 4x that of this compressible data)
        if (bExpectPart1 && Level == xserializer::compression_level::FAST && B.getFrameSize() >= 8 * 64 * 1024) CHECK(nJobs > 0);

        // part 1 read whole first (xfile::ReadWholeFile, the file cache bypassed) and parsed from memory (xfile's buffer device: the
        // parallel path decodes the blocks where they are), one buffer reused - the same texture
        {
            static xfile::read_buffer s_Read;
            xtexture_file::loaded F;
            CHECK(!xtexture_file::Load(P0, P1, F, [&](std::size_t n, const std::function<void(std::size_t)>& Job) { for (std::size_t i = 0; i < n; ++i) Job(i); }, true, &s_Read));
            CHECK(Same(B, F.m_Bitmap));
        }

        // the tail only (the runtime loader's first step: no part 1 read): the last mips, the same bytes as the full texture's
        xtexture_file::loaded T;
        CHECK(!xtexture_file::Load(P0, {}, T, {}, false));
        CHECK(T.m_bPart1Skipped == bExpectPart1);
        const int nSkipped = B.getMipCount() - T.m_Bitmap.getMipCount();
        CHECK(T.m_Bitmap.getFaceCount() == B.getFaceCount() && T.m_Bitmap.isCubemap() == B.isCubemap() && T.m_Bitmap.getFormat() == B.getFormat());
        CHECK(bExpectPart1 ? nSkipped > 0 && T.m_Bitmap.getWidth() == std::max(1u, B.getWidth() >> nSkipped) : nSkipped == 0);
        CHECK(std::max(T.m_Bitmap.getWidth(), T.m_Bitmap.getHeight()) <= (bExpectPart1 ? xtexture_file::tail_max_size_v : std::max(B.getWidth(), B.getHeight())));
        for (int f = 0; f < T.m_Bitmap.getFaceCount(); ++f)
            for (int m = 0; m < T.m_Bitmap.getMipCount(); ++m)
            {
                const auto Mip = B.getMip<std::byte>(m + nSkipped, f), Tail = T.m_Bitmap.getMip<std::byte>(m, f);
                CHECK(Mip.size() == Tail.size() && 0 == std::memcmp(Mip.data(), Tail.data(), Mip.size()));
            }
    }
    std::printf("round trip %ls %ux%u faces %d: %s\n", pName, W, H, nFaces, bExpectPart1 ? "part 0 + part 1" : "part 0 only");
}

// A texture stored as UASTC HDR (stored_encoding::UASTC_HDR): every 4x4 block a solid-colour ASTC HDR block (a void extent: the colour of
// its face and mip - the encoder is not in this test), BC7's 16 bytes per block geometry in the file. Load must give a BC6H texture (the
// format of the GPU, not the stored BC7's) with each block transcoded as xtexture_basis::TranscodeUASTCHDR does, whole or tail only.
static void UastcHdrCube(std::uint32_t W, int nFaces)
{
    const auto  Dir = std::filesystem::temp_directory_path() / L"xtexture_file_unittest";
    const auto  P0 = (Dir / (nFaces == 6 ? L"uastc_hdr_cube" : L"uastc_hdr")).wstring(), P1 = P0 + L".1";
    int nMips = 1; while ((W >> (nMips - 1)) > 1) ++nMips;
    const auto Blocks = [&](int m) { const std::size_t n = std::max(1u, W >> m) + 3 >> 2; return n * n; };
    std::uint64_t Face = 0; for (int m = 0; m < nMips; ++m) Face += Blocks(m) * 16;
    std::vector<std::byte> Data(nMips * sizeof(std::int32_t) + Face * nFaces);
    auto* pOffset = reinterpret_cast<std::int32_t*>(Data.data());
    for (int m = 0, o = 0; m < nMips; o += int(Blocks(m) * 16), ++m) pOffset[m] = o;
    for (int f = 0; f < nFaces; ++f)
        for (int m = 0; m < nMips; ++m)
            for (std::size_t b = 0; b < Blocks(m); ++b)
            {
                std::uint8_t Blk[16] = { 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };      // (void extent, HDR: the whole block one colour)
                const std::uint16_t Half[4] = { std::uint16_t(0x3C00 + f * 64 + m), std::uint16_t(0x4000 + m * 8 + (b & 7)), std::uint16_t(0x3800 + f), 0x3C00 };   // (RGBA, fp16)
                std::memcpy(&Blk[8], Half, 8);
                std::memcpy(&Data[nMips * sizeof(std::int32_t) + f * Face + pOffset[m] + b * 16], Blk, 16);
            }
    xbitmap B; B.setup(W, W, xbitmap::format::BC7_8RGBA, Face, Data, false, nMips, 1, nFaces == 6);     // (BC7's geometry: 16 bytes per block)
    CHECK(!xtexture_file::Save(B, P0, P1, xserializer::compression_level::FAST, xtexture_file::stored_encoding::UASTC_HDR, xbitmap::format::BC6H_8RGB_UFLOAT));

    xtexture_file::loaded L;
    CHECK(!xtexture_file::Load(P0, P1, L, {}, false));
    CHECK(L.m_Bitmap.getFormat() == xbitmap::format::BC6H_8RGB_UFLOAT && L.m_Bitmap.getFaceCount() == nFaces && L.m_Bitmap.getMipCount() == nMips && L.m_Bitmap.isCubemap() == (nFaces == 6));
    CHECK(L.m_MipFaceData.size() == std::size_t(nFaces * nMips));
    xtexture_file::loaded T;
    CHECK(!xtexture_file::Load(P0, {}, T, {}, false));                 // (the tail alone: the texture's smaller mips, transcoded without the body)
    CHECK(T.m_bPart1Skipped == (W > xtexture_file::tail_max_size_v) && T.m_Bitmap.getFormat() == xbitmap::format::BC6H_8RGB_UFLOAT);
    const int nSkipped = nMips - T.m_Bitmap.getMipCount();
    for (int f = 0; f < nFaces && L.m_MipFaceData.size() == std::size_t(nFaces * nMips); ++f)
        for (int m = 0; m < nMips; ++m)
        {
            const auto Src = B.getMip<std::byte>(m, f);
            std::vector<std::byte> Want(Blocks(m) * 16);
            CHECK(xtexture_basis::TranscodeUASTCHDR(Src, Want, std::max(1u, W >> m), std::max(1u, W >> m), 0, std::uint32_t(Blocks(m))));
            const auto& Got = L.m_MipFaceData[f * nMips + m];
            CHECK(Got.size() == Want.size() && 0 == std::memcmp(Got.data(), Want.data(), Want.size()));
            if (m >= nSkipped)
            {
                const auto Tail = T.m_Bitmap.getMip<std::byte>(m - nSkipped, f);
                CHECK(Tail.size() == Want.size() && 0 == std::memcmp(Tail.data(), Want.data(), Want.size()));
            }
        }
    std::printf("UASTC HDR file %u faces %d: loads as BC6H, whole and tail only\n", W, nFaces);
}

int main()
{
    RoundTrip(L"tail_only", 128, 64, 1, false);
    RoundTrip(L"with_part1", 1024, 256, 1, true);
    RoundTrip(L"cube", 256, 256, 6, true);
    RoundTrip(L"odd", 300, 129, 1, true);

    const auto Dir = std::filesystem::temp_directory_path() / L"xtexture_file_unittest";

    UastcHdrCube(512, 1);
    UastcHdrCube(256, 6);
    UastcHdrCube(64, 6);

    // a part 1 from another compile is rejected
    {
        const auto A = (Dir / L"gen_a").wstring(), Bp = (Dir / L"gen_b").wstring();
        xbitmap B; auto Keep = MakeBitmap(B, 512, 512, 1);
        CHECK(!xtexture_file::Save(B, A, A + L".1", xserializer::compression_level::FAST));
        CHECK(!xtexture_file::Save(B, Bp, Bp + L".1", xserializer::compression_level::FAST));
        xtexture_file::loaded L;
        CHECK(xtexture_file::Load(A, Bp + L".1", L));
        std::printf("part 1 of another compile: rejected\n");
    }

    // a cut part 1 (a copy that did not finish) fails the load, it does not crash (xserializer used to report success on a failed read)
    {
        const auto A = (Dir / L"cut").wstring();
        xbitmap B; auto Keep = MakeBitmap(B, 512, 512, 1);
        CHECK(!xtexture_file::Save(B, A, A + L".1", xserializer::compression_level::FAST));
        std::filesystem::resize_file(A + L".1", std::filesystem::file_size(A + L".1") / 2);
        xtexture_file::loaded L;
        CHECK(xtexture_file::Load(A, A + L".1", L));
        std::printf("cut part 1: fails cleanly\n");
    }

    // version 1: a plain xbitmap in the resource file
    {
        const auto V1 = (Dir / L"v1").wstring();
        xbitmap B; auto Keep = MakeBitmap(B, 512, 256, 1);
        CHECK(!xserializer::stream{}.Save(V1, B));
        xtexture_file::loaded L;
        CHECK(!xtexture_file::Load(V1, V1 + L".1", L));
        CHECK(Same(B, L.m_Bitmap));
        std::printf("version 1 file: loads\n");
    }

    std::printf(s_Failures ? "%d FAILED\n" : "all passed\n", s_Failures);
    return s_Failures ? 1 : 0;
}
