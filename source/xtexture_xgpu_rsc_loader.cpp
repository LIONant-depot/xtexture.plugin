
#include "../../tools/xgpu_xcore_bitmap_helpers.h"
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

    // Load the xbitmap
    xserializer::stream Stream;
    if (auto Err = Stream.Load(Path, pBitmap); Err)
    {
        assert(false);
    }

    // Create the actual texture
    if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, *pBitmap); Err)
    {
        assert(false);
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

