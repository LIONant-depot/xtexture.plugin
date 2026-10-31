
#include "dependencies/xGPU/source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "xtexture_file.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"
#include <chrono>
#include <cstdio>
#include <deque>
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
// A texture's big mips (its part 1) load in the background: Load returns the tail (mips <= 128, part 0) at once, a job reads and decodes
// part 1 and hands the whole texture to the manager (QueueReplace), which swaps it in on its own thread at its next getResource - the
// references move over as after a recompile (MarkUpdated). At most 2 such loads at a time (the others wait in a queue): they bound the
// memory in flight, and a reading job never holds more than a couple of the scheduler's workers. XTEXTURE_SYNC=1: everything at once,
// as before (tests that need the full texture on the first frame).
// ponytail: whole part 1 at once, no priority, no cancel of queued loads (a released texture is dropped when its turn comes) - P3b/P4
namespace
{
    struct big_mips_request
    {
        xresource::mgr*         m_pMgr;
        xresource::full_guid    m_Guid;
        void*                   m_pTail;
        xgpu::device            m_Device;
        std::wstring            m_Part0, m_Part1;
    };

    struct big_mips_queue
    {
        static constexpr int            max_in_flight_v = 2;
        std::mutex                      m_Lock;
        std::deque<big_mips_request>    m_Waiting;
        int                             m_nInFlight = 0;
        int                             m_nDone     = 0;                                // (since the queue was last empty: the log line when it empties)
        std::uint64_t                   m_Bytes     = 0;
        std::chrono::steady_clock::time_point m_Start{};
    };
    inline big_mips_queue s_BigMips;

    void StartBigMips(big_mips_request&& R) noexcept;

    // the job: part 0 again (small) and part 1, decoded across the workers; the texture itself is made on the manager's thread
    std::uint64_t LoadBigMips(big_mips_request& R) noexcept
    {
        auto pLoaded = std::make_shared<xtexture_file::loaded>();
        if (xtexture_file::Load(R.m_Part0, R.m_Part1, *pLoaded, TextureDecodeParallelFor, false))
        {
            xerr::LogMessage<xerr::default_states::FAILURE>(std::format("Texture {:X}: its big mips could not be read (it stays at its tail)", R.m_Guid.m_Instance.m_Value));
            return 0;
        }
        std::uint64_t Bytes = 0; for (auto& P : pLoaded->m_MipFaceData) Bytes += P.size();
        R.m_pMgr->QueueReplace(R.m_Guid, R.m_pTail, [pLoaded, Device = R.m_Device]() mutable -> void*
        {
            auto Texture = std::make_unique<xgpu::texture>();
            if (xgpu::tools::bitmap::Create(*Texture, Device, pLoaded->m_Bitmap, pLoaded->m_MipFaceData)) return nullptr;
            return Texture.release();
        });
        return Bytes;
    }

    void StartBigMips(big_mips_request&& R) noexcept
    {
        {
            std::lock_guard Lk(s_BigMips.m_Lock);
            if (s_BigMips.m_nInFlight == 0 && s_BigMips.m_Waiting.empty()) { s_BigMips.m_Start = std::chrono::steady_clock::now(); s_BigMips.m_nDone = 0; s_BigMips.m_Bytes = 0; }
            if (s_BigMips.m_nInFlight >= big_mips_queue::max_in_flight_v) { s_BigMips.m_Waiting.push_back(std::move(R)); return; }
            ++s_BigMips.m_nInFlight;
        }
        auto& S = xscheduler::g_System;
        if (S.getWorkerCount() == 0) S.Init();
        S.SubmitLambda(xscheduler::str_v<"Texture big mips">, [R = std::move(R)]() mutable noexcept
        {
            for (std::optional<big_mips_request> Next{ std::move(R) }; Next;)
            {
                const auto Bytes = LoadBigMips(*Next);
                std::lock_guard Lk(s_BigMips.m_Lock);
                ++s_BigMips.m_nDone; s_BigMips.m_Bytes += Bytes;
                if (s_BigMips.m_Waiting.empty())
                {
                    if (--s_BigMips.m_nInFlight == 0)
                    {
                        std::printf("TEXTURE big mips: %d textures, %.1f MB in %.2f s (background)\n", s_BigMips.m_nDone, s_BigMips.m_Bytes / 1048576.0
                                   , std::chrono::duration<double>(std::chrono::steady_clock::now() - s_BigMips.m_Start).count());
                        std::fflush(stdout);
                    }
                    Next.reset();
                }
                else { Next.emplace(std::move(s_BigMips.m_Waiting.front())); s_BigMips.m_Waiting.pop_front(); }
            }
        }, xscheduler::complexity::NORMAL, xscheduler::priority::NORMAL, xscheduler::affinity::NOT_MAIN_THREAD);
    }
}

//------------------------------------------------------------------

xresource::loader< xrsc::texture_type_guid_v >::data_type* xresource::loader< xrsc::texture_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
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
    // the bitmap header was freed before, the pixels leaked on every load)
    if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, Loaded.m_Bitmap, Loaded.m_MipFaceData); Err)
    {
        return nullptr;
    }

    // The tail now; the whole texture when its big mips are in
    if (Loaded.m_bPart1Skipped) StartBigMips({ &Mgr, GUID, Texture.get(), UserData.m_Device, Part0, Part1 });

    // Return the texture
    return Texture.release();
}

//------------------------------------------------------------------

void xresource::loader< xrsc::texture_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type&& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();
    UserData.m_Device.Destroy( std::move(Data) );
    delete &Data;
}

