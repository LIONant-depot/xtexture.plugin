#ifndef XTEXTURE_XGPU_XRSC_GUID_LOADER_H
#define XTEXTURE_XGPU_XRSC_GUID_LOADER_H
#pragma once

#include "dependencies/xresource_mgr/source/xresource_mgr.h"

// Other plugins that reference a texture (e.g. xfont.plugin's runtime header) re-declare this same
// pair locally rather than including this whole loader header, so each plugin's own compiler stays
// self-contained. Guarded on a shared macro (not this file's own include guard) so an editor that
// legitimately includes both this header AND such a plugin's runtime header in one translation unit
// doesn't hit a duplicate-definition error - whichever is included first wins, both declare the
// identical thing.
#ifndef XRSC_TEXTURE_TYPE_GUID_V_DECLARED
#define XRSC_TEXTURE_TYPE_GUID_V_DECLARED
namespace xrsc
{
    // While this should be just a type... it also happens to be an instance... the instance of the texture_plugin
    // So while generating the type guid we must treat it as an instance.
    inline static constexpr auto    texture_type_guid_v = xresource::type_guid(xresource::guid_generator::Instance64FromString("texture"));
    using                           texture_ref         = xresource::def_guid<texture_type_guid_v>;
}
#endif

namespace xgpu
{
    struct texture;
}

template<>
struct xresource::loader< xrsc::texture_type_guid_v >                  // Now we specify the loader and we must fill in all the information
{
        //--- Expected static parameters ---
        constexpr static inline auto         type_name_v        = L"Texture";                       // This name is used to construct the path to the resource (if not provided)
        constexpr static inline auto         use_death_march_v  = false;                            // xGPU already has a death march implemented inside itself...
        using                                data_type          = xgpu::texture;
        static data_type*                    Load          ( xresource::mgr& Mgr,                    const full_guid& GUID );
        static void                          Destroy       ( xresource::mgr& Mgr, data_type&& Data,  const full_guid& GUID );
};

#endif