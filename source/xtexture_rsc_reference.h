
#include "../dependencies/xResourceMgr/src/xresource_mgr.h"

namespace xgpu
{
    struct texture;
}

template<>
struct xresource::type< xgpu::texture >
{
    constexpr static inline auto                  name_v = "Texture";
    constexpr static inline xresource::type_guid  guid_v = { name_v };

    static xgpu::texture*   Load    ( xresource::mgr& Mgr, xresource::guid GUID );
    static void             Destroy ( xgpu::texture& Texture, xresource::mgr& Mgr, xresource::guid GUID );
};
