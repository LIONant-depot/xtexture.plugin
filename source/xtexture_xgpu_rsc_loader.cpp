
#include "dependencies/xGPU/source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "xtexture_file.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <unordered_map>
#include <mutex>
#include <optional>

//
// We will register the loader, the properties, 
//
inline static auto s_TextureRegistrations = xresource::common_registrations<xrsc::texture_type_guid_v>{};

//------------------------------------------------------------------
// Decodes a texture file's blocks across the scheduler's workers; the caller works too. This module's xscheduler (its g_System and
// thread_local worker ids are per module: LIONCore.dll has its own) is started on first use.
static void TextureDecodeParallelFor(std::size_t n, const std::function<void(std::size_t)>& Job) noexcept
{
    auto& S = xscheduler::g_System;
    if (S.getWorkerCount() == 0) S.Init();
    const std::size_t nJobs = std::min<std::size_t>(n, std::max(S.getWorkerCount(), 1));
    std::atomic<std::size_t> Next{ 0 };
    xscheduler::task_group G({ "Texture decode" }, S);
    for (std::size_t j = 1; j < nJobs; ++j) G.Submit([&] { for (std::size_t i; (i = Next++) < n;) Job(i); });
    for (std::size_t i; (i = Next++) < n;) Job(i);
    G.join();
}

//------------------------------------------------------------------
// Residency (P3a/P3b): a texture's big mips (its part 1) load in the background, and the new texture is handed to the manager
// (QueueReplace), which swaps it in on its own thread at its next getResource - the references move over as after a recompile
// (MarkUpdated). A texture wants its mips from Wanted down (original mip numbers: 0 = full size; at most its tail's first), Resident is
// what its current data has.
// - The first load (Tomas, 2026-10-31: the one asking wants that quality - no fragmentation, no copy): Load makes the image of the wanted
//   size at once with only the tail's mips in it (xgpu setup::m_FirstMip, shown through those mips alone); the job uploads the big mips
//   into that same image (device::ExtendTexture) - the replacement is a new view of it, nothing allocated again.
// - Any later change (TextureResidency; a texture asked for less at first) is a reallocation: the new image is made whole with the
//   wanted mips and replaces the old one, which goes when its last reference does.
// - At most 2 jobs at a time (the others wait in a queue): they bound the memory in flight, and reading jobs never hold more than a
//   couple of the scheduler's workers.
// - The budget (setBudget / XTEXTURE_BUDGET_MB; 0: none) counts the big mips of every image from its creation until xGPU gets it back
//   (Destroy), so an old image and its replacement both count during a transition, plus the sizes of the transitions in flight; a
//   transition that does not fit waits (Update tries again). Tails are not counted (always resident while referenced).
// - A new demand acts once it has held for 0.25 s, and a texture moves at most every 0.5 s (a camera at a threshold does not ping-pong);
//   demand that changes during a transition is taken up when it ends.
// - The job reads part 1 whole at the disk's speed (xfile::ReadWholeFile: no file cache, 16 requests in flight - the disk gives 2.6 GB/s
//   to one request at a time, 6.6 with 4+) into a buffer per job slot, and xserializer decodes it in place (xfile's buffer device).
//   XTEXTURE_CACHED_READ=1: through the file cache as before.
// - XTEXTURE_SYNC=1: everything at once, as before. XTEXTURE_TOP_MIP=n: the first mip textures load to (a scripted cap).
// ponytail: the whole part 1 is read and decoded even for a smaller image (a prefix load would read only the mips kept); requests only
// from scripts (TextureResidency) and the initial load - quality hints and GPU feedback are P4
namespace
{
    using clock = std::chrono::steady_clock;

    struct texture_state
    {
        xresource::mgr*                         m_pMgr          = nullptr;
        xresource::full_guid                    m_Guid          = {};
        void*                                   m_pCurrent      = nullptr;
        std::uint32_t                           m_Gen           = 0;        // a new Load (recompiled) starts a new generation: older jobs' results are ignored
        int                                     m_nPart1        = 0;        // its tail's first mip
        bool                                    m_bWhole        = false;    // never a part of its mips (R8G8B8: xGPU expands it from a whole bitmap)
        std::array<std::uint64_t, 16>           m_MipBytes      = {};       // the part 1 mips' bytes, all faces
        int                                     m_Resident      = 0;
        int                                     m_Wanted        = 0;
        int                                     m_ImageTop      = -1;       // >= 0: its image was made from this mip with only the tail in (the first load): the big mips go into it
        bool                                    m_bMoving       = false;
        clock::time_point                       m_WantedAt      = {};
        clock::time_point                       m_MovedAt       = {};
        xgpu::device                            m_Device        = {};
        std::wstring                            m_Part0, m_Part1;

        std::uint64_t Bytes(int Top) const noexcept { std::uint64_t B = 0; for (int m = Top; m < m_nPart1; ++m) B += m_MipBytes[m]; return B; }
    };

    using state_key = std::pair<const xresource::mgr*, std::uint64_t>;

    struct job
    {
        state_key                               m_Key;
        std::uint32_t                           m_Gen;
        xresource::mgr*                         m_pMgr;
        xresource::full_guid                    m_Guid;
        void*                                   m_pOld;
        int                                     m_Top;
        std::uint64_t                           m_Bytes;
        bool                                    m_bTailOnly;
        bool                                    m_bExtend;                  // the big mips into the current image (the first load), not a new one
        xgpu::device                            m_Device;
        std::wstring                            m_Part0, m_Part1;
    };

    struct residency
    {
        static constexpr int                    max_in_flight_v = 2;
        std::mutex                              m_Lock;
        std::map<state_key, texture_state>      m_Textures;
        std::unordered_map<const void*, std::uint64_t> m_DataBytes;         // each image's counted bytes, until Destroy
        std::uint64_t                           m_Bytes     = 0;            // the images' (old and new during a transition)
        std::uint64_t                           m_Reserved  = 0;            // the transitions in flight
        std::uint64_t                           m_Budget    = [] { const char* p = std::getenv("XTEXTURE_BUDGET_MB"); return p ? std::uint64_t(std::max(0, std::atoi(p))) << 20 : 0; }();
        int                                     m_DefaultTop = [] { const char* p = std::getenv("XTEXTURE_TOP_MIP"); return p ? std::max(0, std::atoi(p)) : 0; }();
        std::deque<job>                         m_Waiting;
        std::vector<std::unique_ptr<xfile::read_buffer>> m_ReadBuffers;     // part 1 read whole at the disk's speed into one of these (one per job in flight, kept: no page faults per file)
        int                                     m_nInFlight = 0;
        int                                     m_nInPlace  = 0;            // first loads whose big mips went into the image made at Load
        int                                     m_nDone     = 0;            // (since the queue was last empty: the log line when it empties)
        std::uint64_t                           m_LogBytes  = 0;
        clock::time_point                       m_Start     = {};
    };
    inline residency s_Residency;
    inline const bool s_bFastRead = std::getenv("XTEXTURE_CACHED_READ") == nullptr;     // XTEXTURE_CACHED_READ=1: part 1 through the file cache as before (A/B)

    // the profiler's plots of the residency (s_Residency.m_Lock held): whenever its numbers change
    void PlotResidency(void) noexcept
    {
        XPROFILE_PLOT("Texture big mips MB", s_Residency.m_Bytes / 1048576.0);
        XPROFILE_PLOT("Texture in flight MB", s_Residency.m_Reserved / 1048576.0);
        XPROFILE_PLOT("Texture jobs queued", static_cast<double>(s_Residency.m_Waiting.size()));
        XPROFILE_PLOT("Texture budget MB", s_Residency.m_Budget / 1048576.0);
        if (s_Residency.m_Budget) XPROFILE_PLOT_LIMIT("Texture big mips MB", s_Residency.m_Budget / 1048576.0);
    }

    // a transition's end however it ends: made (Make ran), or not (the read failed, the texture was released or compiled again since)
    struct transition_end
    {
        state_key m_Key; std::uint32_t m_Gen; std::uint64_t m_Bytes; bool m_bMade = false;
        ~transition_end()
        {
            if (m_bMade) return;
            std::lock_guard Lk(s_Residency.m_Lock);
            s_Residency.m_Reserved -= m_Bytes;
            if (auto It = s_Residency.m_Textures.find(m_Key); It != s_Residency.m_Textures.end() && It->second.m_Gen == m_Gen) It->second.m_bMoving = false;
        }
    };

    // the job: the file (part 0, and part 1 unless only the tail is wanted), decoded across the workers; the texture itself is made on the
    // manager's thread: with the mips from m_Top down, or (m_bExtend) the current image's view of all its mips once the big ones are in
    std::uint64_t RunJob(job& J) noexcept
    {
        XPROFILE_SCOPE("Texture residency job");
        auto pEnd    = std::make_shared<transition_end>(J.m_Key, J.m_Gen, J.m_Bytes);
        auto pLoaded = std::make_shared<xtexture_file::loaded>();
        std::unique_ptr<xfile::read_buffer> pRead;
        {
            std::lock_guard Lk(s_Residency.m_Lock);
            if (s_Residency.m_ReadBuffers.empty()) pRead = std::make_unique<xfile::read_buffer>();
            else { pRead = std::move(s_Residency.m_ReadBuffers.back()); s_Residency.m_ReadBuffers.pop_back(); }
        }
        const auto Err = xtexture_file::Load(J.m_Part0, J.m_bTailOnly ? std::wstring_view{} : std::wstring_view{ J.m_Part1 }, *pLoaded, TextureDecodeParallelFor, false, s_bFastRead ? pRead.get() : nullptr);
        {
            std::lock_guard Lk(s_Residency.m_Lock);
            s_Residency.m_ReadBuffers.push_back(std::move(pRead));
        }
        if (Err)
        {
            xerr::LogMessage<xerr::default_states::FAILURE>(std::format("Texture {:X}: its big mips could not be read (it stays as it is)", J.m_Guid.m_Instance.m_Value));
            return 0;
        }
        std::uint64_t Read = 0; for (auto& P : pLoaded->m_MipFaceData) Read += P.size();
        J.m_pMgr->QueueReplace(J.m_Guid, J.m_pOld, [pLoaded, pEnd, Top = J.m_bTailOnly ? 0 : J.m_Top, Device = J.m_Device, Key = J.m_Key, Gen = J.m_Gen, ResidentTop = J.m_Top
                                                   , bExtend = J.m_bExtend, pOld = J.m_pOld]() mutable -> void*
        {
            XPROFILE_SCOPE("Texture make (xGPU)");
            xtexture_file::loaded& L = *pLoaded;
            const auto Done = [&](xgpu::texture* pNew, std::uint64_t Bytes)       // (s_Residency.m_Lock held)
            {
                pEnd->m_bMade = true;
                s_Residency.m_Reserved -= pEnd->m_Bytes;
                s_Residency.m_Bytes    += pEnd->m_Bytes;
                PlotResidency();
                s_Residency.m_DataBytes[pNew] = Bytes;
                if (auto It = s_Residency.m_Textures.find(Key); It != s_Residency.m_Textures.end() && It->second.m_Gen == Gen)
                {
                    It->second.m_pCurrent = pNew;
                    It->second.m_Resident = ResidentTop;
                    It->second.m_ImageTop = -1;
                    It->second.m_bMoving  = false;
                    It->second.m_MovedAt  = clock::now();
                }
            };

            // into the current image: the big mips (Top .. the tail's first, per face), then its new view (pOld is alive: Make runs only while it is the GUID's data)
            if (bExtend)
            {
                std::vector<std::span<const std::byte>> Pieces;
                const int nMips = L.m_Bitmap.getMipCount(), nFaces = L.m_Bitmap.getFaceCount();
                for (int f = 0; f < nFaces; ++f) for (int m = Top; m < L.m_nPart1Mips; ++m) Pieces.push_back(L.m_MipFaceData[f * nMips + m]);
                auto Texture = std::make_unique<xgpu::texture>();
                if (Device.ExtendTexture(*Texture, *static_cast<xgpu::texture*>(pOld), Pieces)) return nullptr;
                std::lock_guard Lk(s_Residency.m_Lock);
                auto& Counted = s_Residency.m_DataBytes[pOld];          // (the image was counted with the first texture: it moves to this one, the image's last holder)
                Done(Texture.get(), std::exchange(Counted, 0));
                ++s_Residency.m_nInPlace;
                return Texture.release();
            }

            // the mips from Top down: the description's size and mip count, and their pieces (face-major)
            xbitmap Desc;
            std::memcpy(static_cast<void*>(&Desc), &L.m_Bitmap, sizeof(xbitmap));
            std::vector<std::span<const std::byte>> Pieces;
            const int nMips = L.m_Bitmap.getMipCount(), nFaces = L.m_Bitmap.getFaceCount();
            std::uint64_t FaceSize = 0;
            for (int f = 0; f < nFaces; ++f) for (int m = Top; m < nMips; ++m) { Pieces.push_back(L.m_MipFaceData[f * nMips + m]); if (f == 0) FaceSize += L.m_MipFaceData[m].size(); }
            Desc.m_Width    = std::max(1u, L.m_Bitmap.getWidth() >> Top);
            Desc.m_Height   = std::max(1u, L.m_Bitmap.getHeight() >> Top);
            Desc.m_nMips    = static_cast<std::uint8_t>(nMips - Top);
            Desc.m_FaceSize = static_cast<std::uint32_t>(FaceSize);

            auto Texture = std::make_unique<xgpu::texture>();
            const auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Desc, Pieces);
            Desc.m_pData = nullptr; Desc.m_Flags.m_bOwnsMemory = false;     // (a view: it frees nothing)
            if (Err) return nullptr;

            std::lock_guard Lk(s_Residency.m_Lock);
            Done(Texture.get(), pEnd->m_Bytes);
            return Texture.release();
        });
        return Read;
    }

    void SubmitJob(job&& J) noexcept     // (s_Residency.m_Lock held)
    {
        auto& R = s_Residency;
        if (R.m_nInFlight == 0 && R.m_Waiting.empty()) { R.m_Start = clock::now(); R.m_nDone = 0; R.m_LogBytes = 0; }
        if (R.m_nInFlight >= residency::max_in_flight_v) { R.m_Waiting.push_back(std::move(J)); return; }
        ++R.m_nInFlight;
        auto& S = xscheduler::g_System;
        if (S.getWorkerCount() == 0) S.Init();
        S.SubmitLambda(xscheduler::str_v<"Texture residency">, [J = std::move(J)]() mutable noexcept
        {
            for (std::optional<job> Next{ std::move(J) }; Next;)
            {
                const auto Bytes = RunJob(*Next);
                std::lock_guard Lk(s_Residency.m_Lock);
                ++s_Residency.m_nDone; s_Residency.m_LogBytes += Bytes;
                if (s_Residency.m_Waiting.empty())
                {
                    if (--s_Residency.m_nInFlight == 0)
                    {
                        std::printf("TEXTURE big mips: %d textures, %.1f MB in %.2f s (background)\n", s_Residency.m_nDone, s_Residency.m_LogBytes / 1048576.0
                                   , std::chrono::duration<double>(clock::now() - s_Residency.m_Start).count());
                        std::fflush(stdout);
                    }
                    Next.reset();
                }
                else { Next.emplace(std::move(s_Residency.m_Waiting.front())); s_Residency.m_Waiting.pop_front(); }
            }
        }, xscheduler::complexity::NORMAL, xscheduler::priority::NORMAL, xscheduler::affinity::NOT_MAIN_THREAD);
    }

    // a transition of T toward its wanted mips, if it fits the budget (s_Residency.m_Lock held)
    bool TryStart(const state_key& Key, texture_state& T) noexcept
    {
        auto& R = s_Residency;
        if (T.m_bMoving || T.m_Wanted == T.m_Resident) return false;
        const bool          bExtend = T.m_ImageTop == T.m_Wanted;      // (the first load's image is that size already: its bytes are counted)
        const std::uint64_t Bytes   = bExtend ? 0 : T.Bytes(T.m_Wanted);
        if (R.m_Budget && Bytes && R.m_Bytes + R.m_Reserved + Bytes > R.m_Budget) return false;
        R.m_Reserved += Bytes;
        PlotResidency();
        T.m_bMoving   = true;
        SubmitJob({ Key, T.m_Gen, T.m_pMgr, T.m_Guid, T.m_pCurrent, T.m_Wanted, Bytes, T.m_Wanted >= T.m_nPart1, bExtend, T.m_Device, T.m_Part0, T.m_Part1 });
        return true;
    }
}

namespace xtexture_residency
{
    void Request(xresource::mgr& Mgr, const xresource::full_guid& Guid, int TopMip) noexcept
    {
        std::lock_guard Lk(s_Residency.m_Lock);
        auto It = s_Residency.m_Textures.find({ &Mgr, Guid.m_Instance.m_Value });
        if (It == s_Residency.m_Textures.end()) return;
        auto& T = It->second;
        const int Top = T.m_bWhole ? 0 : std::clamp(TopMip, 0, T.m_nPart1);
        if (Top != T.m_Wanted) { T.m_Wanted = Top; T.m_WantedAt = clock::now(); }
    }

    void RequestAll(xresource::mgr& Mgr, int TopMip) noexcept
    {
        std::lock_guard Lk(s_Residency.m_Lock);
        const auto Now = clock::now();
        for (auto& [Key, T] : s_Residency.m_Textures)
        {
            if (Key.first != &Mgr) continue;
            const int Top = T.m_bWhole ? 0 : std::clamp(TopMip, 0, T.m_nPart1);
            if (Top != T.m_Wanted) { T.m_Wanted = Top; T.m_WantedAt = Now; }
        }
    }

    void setBudget(std::uint64_t Bytes) noexcept { std::lock_guard Lk(s_Residency.m_Lock); s_Residency.m_Budget = Bytes; }

    void Update(xresource::mgr& Mgr) noexcept
    {
        std::lock_guard Lk(s_Residency.m_Lock);
        const auto Now = clock::now();
        for (auto& [Key, T] : s_Residency.m_Textures)
            if (Key.first == &Mgr && !T.m_bMoving && T.m_Wanted != T.m_Resident && Now - T.m_WantedAt >= std::chrono::milliseconds(250) && Now - T.m_MovedAt >= std::chrono::milliseconds(500))
                TryStart(Key, T);
    }

    stats getStats(const xresource::mgr& Mgr) noexcept
    {
        std::lock_guard Lk(s_Residency.m_Lock);
        stats S{ .m_Bytes = s_Residency.m_Bytes, .m_Reserved = s_Residency.m_Reserved, .m_Budget = s_Residency.m_Budget, .m_nWaitingJobs = int(s_Residency.m_Waiting.size()), .m_nInPlace = s_Residency.m_nInPlace };
        for (auto& [Key, T] : s_Residency.m_Textures)
        {
            if (Key.first != &Mgr) continue;
            ++S.m_nTextures;
            S.m_nMoving += T.m_bMoving;
            S.m_nPending += !T.m_bMoving && T.m_Wanted != T.m_Resident;
        }
        return S;
    }
}

//------------------------------------------------------------------

xresource::loader< xrsc::texture_type_guid_v >::data_type* xresource::loader< xrsc::texture_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    XPROFILE_SCOPE("Texture load (tail)");
    auto&           UserData    = Mgr.getUserData<resource_mgr_user_data>();
    auto            Texture     = std::make_unique<xgpu::texture>();
    xtexture_file::loaded Loaded;
    static const bool bSync = std::getenv("XTEXTURE_SYNC") != nullptr;

    // Load the texture file (part 0 - and part 1 later, in the background: see above). A missing/not-yet-compiled resource is an expected, recoverable case (confirmed
    // live: the thumbnail system eagerly requests a Load for every texture merely SCROLLED into view, so a
    // fresh clone with nothing compiled yet hits this on the very first frame) - every caller already
    // handles getResource() returning null (this loader's own doc comment, the inspector picker, the
    // thumbnail renderer), so return null instead of asserting-then-dereferencing pBitmap anyway (a crash
    // in a debug build via ucrtbased's own assert, silent undefined behaviour in Release).
    const auto Part0 = Mgr.getResourcePartPath(GUID, type_name_v, 0);
    const auto Part1 = Mgr.getResourcePartPath(GUID, type_name_v, 1);
    if (auto Err = xtexture_file::Load(Part0, bSync ? std::wstring_view{ Part1 } : std::wstring_view{}, Loaded, TextureDecodeParallelFor, false); Err)
    {
        return nullptr;
    }

    // Create the actual texture from the file's pieces, copied once, into the upload ring (they are there when it returns: Loaded frees the file's memory - all of it; only
    // the bitmap header was freed before, the pixels leaked on every load). With big mips still to come (only the tail was read): the image
    // of the wanted size at once, only the tail's mips in it and shown - the job puts the big ones into the same image (ExtendTexture).
    const bool bWhole   = Loaded.m_Bitmap.getFormat() == xbitmap::format::R8G8B8;         // (xGPU expands it from a whole bitmap: no part of its mips)
    const int  Top      = bWhole ? 0 : std::min(s_Residency.m_DefaultTop, int(Loaded.m_nPart1Mips));
    const bool bInPlace = Loaded.m_bPart1Skipped && !bWhole && Top < Loaded.m_nPart1Mips;
    if (bInPlace)
    {
        xbitmap Desc;
        std::memcpy(static_cast<void*>(&Desc), &Loaded.m_Bitmap, sizeof(xbitmap));
        std::uint64_t FaceSize = Loaded.m_Bitmap.getFaceSize();
        for (int m = Top; m < Loaded.m_nPart1Mips; ++m) FaceSize += Loaded.m_Part1MipSize[m];
        Desc.m_Width    = std::max(1u, std::uint32_t(Loaded.m_Width) >> Top);
        Desc.m_Height   = std::max(1u, std::uint32_t(Loaded.m_Height) >> Top);
        Desc.m_nMips    = static_cast<std::uint8_t>(Loaded.m_nMips - Top);
        Desc.m_FaceSize = static_cast<std::uint32_t>(FaceSize);
        const auto Err  = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, Desc, Loaded.m_MipFaceData, Loaded.m_nPart1Mips - Top);
        Desc.m_pData = nullptr; Desc.m_Flags.m_bOwnsMemory = false;     // (a view: it frees nothing)
        if (Err) return nullptr;
    }
    else if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, Loaded.m_Bitmap, Loaded.m_MipFaceData); Err)
    {
        return nullptr;
    }

    // The tail now (or all of it: no part 1, or XTEXTURE_SYNC); its residency from here on, toward the default (the whole texture)
    if (Loaded.m_nPart1Mips)
    {
        std::lock_guard Lk(s_Residency.m_Lock);
        const state_key Key{ &Mgr, GUID.m_Instance.m_Value };
        auto& T = s_Residency.m_Textures[Key];
        const auto Gen = T.m_Gen + 1;
        T = texture_state{ .m_pMgr = &Mgr, .m_Guid = GUID, .m_pCurrent = Texture.get(), .m_Gen = Gen, .m_nPart1 = Loaded.m_nPart1Mips
                         , .m_bWhole = bWhole, .m_Device = UserData.m_Device, .m_Part0 = Part0, .m_Part1 = Part1 };
        for (int m = 0; m < T.m_nPart1; ++m) T.m_MipBytes[m] = std::uint64_t(Loaded.m_Part1MipSize[m]) * Loaded.m_nFaces;
        T.m_Resident = Loaded.m_bPart1Skipped ? T.m_nPart1 : 0;
        T.m_Wanted   = Top;
        T.m_ImageTop = bInPlace ? Top : -1;
        const std::uint64_t Bytes = T.Bytes(bInPlace ? Top : T.m_Resident);      // (the image as allocated: the whole of it in place)
        s_Residency.m_Bytes += Bytes;
        PlotResidency();
        s_Residency.m_DataBytes[Texture.get()] = Bytes;
        TryStart(Key, T);           // (no wait for the first load; over the budget: Update starts it later)
    }

    // Return the texture
    return Texture.release();
}

//------------------------------------------------------------------

void xresource::loader< xrsc::texture_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    {
        // its bytes leave the budget now that xGPU has it (its own death march frees the image after the frames that could use it); the
        // texture itself is gone when this was its current data (released: not replaced)
        std::lock_guard Lk(s_Residency.m_Lock);
        if (auto It = s_Residency.m_DataBytes.find(&Data); It != s_Residency.m_DataBytes.end()) { s_Residency.m_Bytes -= It->second; s_Residency.m_DataBytes.erase(It); PlotResidency(); }
        if (auto It = s_Residency.m_Textures.find({ &Mgr, GUID.m_Instance.m_Value }); It != s_Residency.m_Textures.end() && It->second.m_pCurrent == &Data) s_Residency.m_Textures.erase(It);
    }
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();
    UserData.m_Device.Destroy( std::move(Data) );
    delete &Data;
}

