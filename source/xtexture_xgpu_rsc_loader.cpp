
#include "dependencies/xGPU/source/Tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xbitmap/source/xbitmap.h"
#include "dependencies/xbitmap/source/bridges/xserializer/xbitmap_to_xserializer.h"
#include "dependencies/xresource_guid/source/bridges/xresource_xproperty_bridge.h"

//
// We will register the loader, the properties, 
//
inline static auto s_TextureRegistrations = xresource::common_registrations<xrsc::texture_type_guid_v>{};

//------------------------------------------------------------------

xresource::loader< xrsc::texture_type_guid_v >::data_type* xresource::loader< xrsc::texture_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    auto&           UserData    = Mgr.getUserData<resource_mgr_user_data>();
    auto            Texture     = std::make_unique<xgpu::texture>();
    xbitmap*        pBitmap     = nullptr;
    std::wstring    Path        = Mgr.getResourcePath(GUID, type_name_v);

    // Load the xbitmap. A missing/not-yet-compiled resource is an expected, recoverable case (confirmed
    // live: the thumbnail system eagerly requests a Load for every texture merely SCROLLED into view, so a
    // fresh clone with nothing compiled yet hits this on the very first frame) - every caller already
    // handles getResource() returning null (this loader's own doc comment, the inspector picker, the
    // thumbnail renderer), so return null instead of asserting-then-dereferencing pBitmap anyway (a crash
    // in a debug build via ucrtbased's own assert, silent undefined behaviour in Release).
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pBitmap); Err)
    {
        return nullptr;
    }

    // Create the actual texture
    if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, *pBitmap); Err)
    {
        xserializer::default_memory_handler_v.Free( xserializer::mem_type{ .m_bUnique = true }, pBitmap);
        return nullptr;
    }

    // Free the bitmap
    xserializer::default_memory_handler_v.Free( xserializer::mem_type{ .m_bUnique = true }, pBitmap);

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

