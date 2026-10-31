
#include "dependencies/xGPU/source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "xtexture_file.h"
#include "dependencies/xscheduler/source/xscheduler.h"
#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

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

xresource::loader< xrsc::texture_type_guid_v >::data_type* xresource::loader< xrsc::texture_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    auto&           UserData    = Mgr.getUserData<resource_mgr_user_data>();
    auto            Texture     = std::make_unique<xgpu::texture>();
    xtexture_file::loaded Loaded;

    // Load the texture file (part 0, and part 1 when it has one: xtexture_file.h). A missing/not-yet-compiled resource is an expected, recoverable case (confirmed
    // live: the thumbnail system eagerly requests a Load for every texture merely SCROLLED into view, so a
    // fresh clone with nothing compiled yet hits this on the very first frame) - every caller already
    // handles getResource() returning null (this loader's own doc comment, the inspector picker, the
    // thumbnail renderer), so return null instead of asserting-then-dereferencing pBitmap anyway (a crash
    // in a debug build via ucrtbased's own assert, silent undefined behaviour in Release).
    if (auto Err = xtexture_file::Load(Mgr.getResourcePartPath(GUID, type_name_v, 0), Mgr.getResourcePartPath(GUID, type_name_v, 1), Loaded, TextureDecodeParallelFor, false); Err)
    {
        return nullptr;
    }

    // Create the actual texture from the file's pieces, copied once, into the upload ring (they are there when it returns: Loaded frees the file's memory - all of it; only
    // the bitmap header was freed before, the pixels leaked on every load)
    if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, Loaded.m_Bitmap, Loaded.m_MipFaceData); Err)
    {
        return nullptr;
    }

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

