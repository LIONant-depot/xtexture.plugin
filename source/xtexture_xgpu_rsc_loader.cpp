#include "xcore.h"
#include "../tools/xgpu_xcore_bitmap_helpers.h"

//
// We will register the loader
//
inline static auto s_TextureLoaderRegistration = xresource::loader_registration<xrsc::texture_type_guid_v>{};

//------------------------------------------------------------------

xresource::loader< xrsc::texture_type_guid_v >::data_type* xresource::loader< xrsc::texture_type_guid_v >::Load( xresource::mgr& Mgr, const full_guid& GUID )
{
    auto&           UserData    = Mgr.getUserData<resource_mgr_user_data>();
    auto            Texture     = std::make_unique<xgpu::texture>();
    xcore::bitmap*  pBitmap     = nullptr;
    std::wstring    Path        = Mgr.getResourcePath(GUID, type_name_v);

    // Load the xbitmap
    if (auto Err = xcore::bitmap::SerializeLoad(pBitmap, Path); Err)
    {
        assert(false);
    }

    // Create the actual texture
    if (auto Err = xgpu::tools::bitmap::Create(*Texture, UserData.m_Device, *pBitmap); Err)
    {
        assert(false);
    }

    // Free the bitmap
    xcore::memory::AlignedFree(pBitmap);

    // Return the texture
    return Texture.release();
}

//------------------------------------------------------------------

void xresource::loader< xrsc::texture_type_guid_v >::Destroy(xresource::mgr& Mgr, data_type& Data, const full_guid& GUID)
{
    auto& UserData = Mgr.getUserData<resource_mgr_user_data>();
    UserData.m_Device.Destroy( std::move(Data) );
    delete &Data;
}

