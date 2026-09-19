#ifndef XTEXTURE_EDITOR_PREVIEW_H
#define XTEXTURE_EDITOR_PREVIEW_H
#pragma once

// Texture-editor preview mechanics taught by E10_TextureResourcePipeline.cpp, hosted inside
// the NEW editor-framework session (plugin document/commands/UI). E10 itself is not modified.
// Safe load: bitmap_inspector::Load + xgpu::tools::bitmap::Create (not getResource on library guids).
#include "source/Examples/E05_Textures/E05_BitmapInspector.h"
#include "source/tools/xgpu_imgui_breach.h"
#include "source/tools/xgpu_view.h"
#include "dependencies/xprim_geom/source/xprim_geom.h"
#include "source/tools/xgpu_xcore_bitmap_helpers.h"
#include "dependencies/xproperty/source/xcore/my_properties.h"
#include "dependencies/xproperty/source/examples/imgui/xPropertyImGuiInspector.h"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"
#include "dependencies/xresource_mgr/source/xresource_mgr.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include <filesystem>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <array>
#include <memory>

namespace e10
{
    struct vert_2d
    {
        float           m_X, m_Y;
        xmath::fvec3d   m_UV;           // UV able to deal with cube maps as well...
    };

    //------------------------------------------------------------------------------------------------

    struct vert_3d
    {
        xmath::fvec3d   m_Position;
        xmath::fvec3d   m_Binormal;
        xmath::fvec3d   m_Tangent;
        xmath::fvec3d   m_Normal;
        xmath::fvec2    m_TexCoord;
    };

    //------------------------------------------------------------------------------------------------

    struct push_contants
    {
        float           m_MipLevel       {0};
        float           m_ToGamma        {1};
        xmath::fvec2    m_Scale          {1};
        xmath::fvec2    m_Translation    {0};
        xmath::fvec2    m_UVScale        {1};
        xmath::fvec4    m_TintColor      {1};
        xmath::fvec4    m_ColorMask      {0};
        xmath::fvec4    m_Mode           {0};
        xmath::fvec4    m_NormalModes    {0};
        xmath::fmat4    m_L2C;
        xmath::fvec3    m_LocalSpaceLightPosition;
        xmath::fvec4    m_UVMode;
    };

    //------------------------------------------------------------------------------------------------

    static
    void DebugMessage(std::string_view View)
    {
        printf("%s\n", View.data());
    }

}

namespace xtexture_editor::preview
{
    inline xgpu::pipeline& PreviewEmptyPipeline() noexcept
    {
        static xgpu::pipeline s_Empty{};
        return s_Empty;
    }

    constexpr auto g_VertShader2DSPV = std::array
    {
        #include "e10_2d_vert.h"
    };
    constexpr auto g_FragShader2DSPV = std::array
    {
        #include "e10_2d_frag.h"
    };
    constexpr auto g_VertShader3DSPV = std::array
    {
        #include "e10_3d_vert.h"
    };
    constexpr auto g_FragShader3DSPV = std::array
    {
        #include "e10_3d_frag.h"
    };
    constexpr auto g_VertShader2DCubeSPV = std::array
    {
        #include "e10_2d_cube_vert.h"
    };
    constexpr auto g_FragShader2DCubeSPV = std::array
    {
        #include "e10_2d_cube_frag.h"
    };
    constexpr auto g_VertShader3DCubeSPV = std::array
    {
        #include "e10_3d_cube_vert.h"
    };
    constexpr auto g_FragShader3DCubeSPV = std::array
    {
        #include "e10_3d_cube_frag.h"
    };

struct draw_options
{
    enum class render_mode
    { RENDER_2D
    , RENDER_3D
    , RENDER_3D_WITH_LIGHTING
    };

    static constexpr auto render_modes_v = std::array
    { xproperty::settings::enum_item("2D",   render_mode::RENDER_2D,  "Renders the texture in a 2D plane. Good for close examination of a general texture.")
    , xproperty::settings::enum_item("3D",   render_mode::RENDER_3D,  "Renders using a 3d cube to see the textures in different angles. Good to check any mipmap issues")
    , xproperty::settings::enum_item("3D_LIGHTING",   render_mode::RENDER_3D_WITH_LIGHTING,  "Renders using a 3d cube assuming that the texture is a normal map. "
                                                                                              "It will render using a diffuse lighting. This method is good for checking "
                                                                                              "the quality of your normal maps.\nNOTE: You can freeze and unfreeze the light "
                                                                                              "direction by pressing the space bar.")
    };

    enum class channels_mode
    { COLOR_ALPHA
    , NO_ALPHA
    , A_ONLY
    , R_ONLY
    , G_ONLY
    , B_ONLY
    };

    static constexpr auto channels_mode_v = std::array
    { xproperty::settings::enum_item("COLOR + ALPHA",   channels_mode::COLOR_ALPHA,  "Render the image normally using all the channels available")
    , xproperty::settings::enum_item("NO_ALPHA",        channels_mode::NO_ALPHA,     "Render the image normally using all available channels except Alpha. Alpha will be set to opaque.")
    , xproperty::settings::enum_item("A_ONLY",          channels_mode::A_ONLY,       "Renders only the alpha channel as a single color without any alpha")
    , xproperty::settings::enum_item("R_ONLY",          channels_mode::R_ONLY,       "Renders only the red channel as a single color without any alpha")
    , xproperty::settings::enum_item("G_ONLY",          channels_mode::G_ONLY,       "Renders only the green channel as a single color without any alpha")
    , xproperty::settings::enum_item("B_ONLY",          channels_mode::B_ONLY,       "Renders only the blue channel as a single color without any alpha")
    };

    enum class display_gamma_mode
    { GAMMA
    , LINEAR
    , RAW_DATA_INFILE
    };

    static constexpr auto display_gamma_mode_v = std::array
    { xproperty::settings::enum_item("GAMMA",           display_gamma_mode::GAMMA,              "Normal rendering. This is how games and other applications render their images.")
    , xproperty::settings::enum_item("LINEAR",          display_gamma_mode::LINEAR,             "This is how the shader is receiving the image so that it can operate...")
    , xproperty::settings::enum_item("RAW_DATA_INFILE", display_gamma_mode::RAW_DATA_INFILE,    "This is the raw data in the file. Useful to see the kind of precision of the data.")
    };

    render_mode         m_RenderMode            = render_mode::RENDER_2D;  
    float               m_UVScale               = 1;
    float               m_BackgroundIntensity   = 0.16f;
    channels_mode       m_ChannelsMode          = channels_mode::COLOR_ALPHA;
    bool                m_bBilinearMode         = false;
    int                 m_ChooseMipLevel        = -1;
    int                 m_MaxMipLevels          = 0;
    display_gamma_mode  m_DisplayInGammaMode    = display_gamma_mode::GAMMA;
    float               m_DisplayGamma          = 2.2f;

    XPROPERTY_DEF
    ("Render", draw_options
    , obj_member
        < "Render Mode"
        , &draw_options::m_RenderMode
        , member_enum_span<render_modes_v>
        , member_help<"Changes between 2D and 3D views for your texture."
        >>

    , obj_member
        < "Bilinear"
        , &draw_options::m_bBilinearMode
        , member_help<"Render the texture with bilinear filtering or nearest"
        >>
    , obj_member
        < "UVScale"
        , &draw_options::m_UVScale
        , member_ui<float>::scroll_bar<1, 10>
        , member_help<"Scales the UV of the image"
        >>
    , obj_member
        < "Channels Mode"
        , &draw_options::m_ChannelsMode
        , member_enum_span<channels_mode_v>
        , member_help<"Renders the image following one of the rules selected"
        >>
    , obj_member
        < "Background Intensity"
        , &draw_options::m_BackgroundIntensity
        , member_ui<float>::scroll_bar<0, 3>
        , member_help<"Changes the intensity of the background"
        >>
    , obj_member
        < "ChooseMip"
        , +[](draw_options& O, bool bRead, int& Value)
        {
            if (bRead) Value = O.m_ChooseMipLevel;
            else       O.m_ChooseMipLevel = std::min( O.m_MaxMipLevels, Value);
        }
        , member_ui<int>::scroll_bar<-1, 20>
        , member_help<"Selects a particular mip to render the texture. If you set the value to -1 "
                      "then it will go back to using the texture with all the mips (trilinear when bilinear is enable)"
        >>
    , obj_member
        < "Display Mode"
        , &draw_options::m_DisplayInGammaMode
        , member_enum_span<display_gamma_mode_v>
        , member_help<"This mode shows the image in gamma (This is the normal mode, also how all the displays works), "
                      "or it can show the image in LINEAR which is how the shader receives the image."
        >>
    , obj_member
        < "Display Gamma"
        , &draw_options::m_DisplayGamma
        , member_ui<float>::scroll_bar<1, 4>
        , member_dynamic_flags < +[](const draw_options& O)
        {
            xproperty::flags::type Flags{};
            Flags.m_bDontShow = O.m_DisplayInGammaMode != display_gamma_mode::GAMMA;
            return Flags;
        } >
        , member_help<"Changes the display gamma for the image"
        >>
    )
};
XPROPERTY_REG(draw_options)

//------------------------------------------------------------------------------------------------

struct draw_controls
{
    draw_options*       m_pOptions                  = nullptr;

    float               m_MainWindowWidth           = {};
    float               m_MainWindowHeight          = {};

    float               m_2DMouseScale              = 1;
    xmath::fvec2        m_2DMouseTranslate          = { 0.0f, 0.0f };

    xgpu::tools::view   m_3DView                    = {};
    xmath::fvec3        m_3DLightPosition           = {};
    xmath::radian3      m_3DAngles                  = {};
    float               m_3DDistance                = 2;
    bool                m_3DFollowCamera            = true;

    draw_controls() = default;
    draw_controls(draw_options& O) : m_pOptions(&O)
    {
        m_3DView.setFov(60_xdeg);
        m_3DView.setPosition({ 0,0,m_3DDistance });
    }

    constexpr static xproperty::flags::type filter_2d(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode != draw_options::render_mode::RENDER_2D;
        return Flags;
    }

    constexpr static xproperty::flags::type filter_3d(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D;
        return Flags;
    }

    constexpr static xproperty::flags::type filter_3d_wl(const draw_controls& O)
    {
        xproperty::flags::type Flags{};
        Flags.m_bDontShow = O.m_pOptions->m_RenderMode != draw_options::render_mode::RENDER_3D_WITH_LIGHTING;
        return Flags;
    }

    XPROPERTY_DEF
    ( "Controls"
    , draw_controls
    , obj_member
        < "Image Location"
        , &draw_controls::m_2DMouseTranslate
        , member_dynamic_flags<filter_2d>
        , member_help<"The location of the texture"
        >>
    , obj_member_ro
        < "Camera Location"
        , +[](draw_controls& O) -> xmath::fvec3& { static xmath::fvec3 pos; pos = O.m_3DView.getPosition(); return pos; }
        , member_dynamic_flags<filter_3d>
        , member_help<"Zooms in and out"
        >>
    , obj_member
        < "Light Location"
        , &draw_controls::m_3DLightPosition
        , member_dynamic_flags<filter_3d_wl>
        , member_help<"The action location of the light relative to the object"
        >>
    , obj_member
        < "Light Follow"
        , &draw_controls::m_3DFollowCamera
        , member_dynamic_flags<filter_3d_wl>
        , member_help<"Tells if the light should follow the camera position or hold in place.\nNOTE: You can press space-bar to achieve the same effect"
        >>
    , obj_member
        < "Recenter"
        , +[](draw_controls& O, bool bRead, std::string& Value)
        {
            if (bRead) Value = "Recenter";
            else
            {
                if ( O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D )
                {
                    O.m_2DMouseTranslate = { 0.0f, 0.0f };
                }
                else
                {
                    O.m_3DView.setPosition({ 0,0,O.m_3DDistance });
                    O.m_3DAngles = xmath::radian3{ 0_xdeg,0_xdeg,0_xdeg };
                }
            }
        }
        , member_ui<std::string>::button<>
        , member_help<"Reset the image at the center of the screen"
        >>
    , obj_member 
        < "Zoom"
        , +[](draw_controls& O, bool bRead, float& Value)
        {
            if (O.m_pOptions->m_RenderMode == draw_options::render_mode::RENDER_2D)
            {
                static constexpr auto max_v = 30;
                if (bRead)
                {
                    Value = O.m_2DMouseScale / max_v;
                }
                else
                {
                    float v = Value * max_v;
                    O.m_2DMouseTranslate.m_X += (O.m_2DMouseTranslate.m_X) * (v - O.m_2DMouseScale) / O.m_2DMouseScale;
                    O.m_2DMouseTranslate.m_Y += (O.m_2DMouseTranslate.m_Y) * (v - O.m_2DMouseScale) / O.m_2DMouseScale;
                    O.m_2DMouseScale = v;
                }
            }
            else
            {
                static constexpr auto max_v = 7;
                static constexpr auto min_v = 0.61f;
                static constexpr auto range_v = max_v - min_v;
                if (bRead) Value = 1 - (O.m_3DDistance - min_v) / range_v;
                else O.m_3DDistance = (1 - Value) * range_v + min_v;
            }
        }
        , member_ui<float>::scroll_bar<0.01, 1>
//        , member_dynamic_flags<filter_2d>
        , member_help<"Zooms in and out of the image"
        >>
    )
};
XPROPERTY_REG(draw_controls)


struct material_mgr
{
    union material
    {
        std::uint32_t m_Value {};

        static constexpr auto num_bits_used = 7;
        struct
        {
            std::uint8_t    m_Bilinear : 1
            ,               m_CubeMap  : 1
            ,               m_3DRender : 1
            ,               m_UWrapMode: 2      // base on xbitmap::wrap_mode
            ,               m_VWrapMode: 2      // base on xbitmap::wrap_mode
            ;
        };

        // Fast CRC function
        static uint32_t crc32(const std::span<const std::byte> data) noexcept
        {
            uint32_t crc = 0xFFFFFFFF;
            size_t i = 0;

            // Process 4 bytes at a time
            while (i + 4 <= data.size())
            {
                uint32_t chunk = *reinterpret_cast<const uint32_t*>(&data[i]);
                crc = _mm_crc32_u32(crc, chunk);
                i += 4;
            }

            // Process remaining bytes
            while (i < data.size())
            {
                crc = _mm_crc32_u8(crc, static_cast<unsigned char>(data[i]));
                ++i;
            }

            return ~crc;
        }

        std::uint32_t getGuid() const noexcept
        {
            return crc32( std::span{reinterpret_cast<const std::byte*>(this), sizeof(*this)});
        }
    };

    struct material_instance
    {
        xrsc::texture_ref  m_TextureRef;

        std::uint32_t getGuid(xresource::mgr& RscMgr ) const noexcept
        {
            const auto Guid = RscMgr.getFullGuid(m_TextureRef).m_Instance;
            return static_cast<std::uint32_t>((Guid.m_Value>>0) ^ (Guid.m_Value>>32));
        }
    };

    //----------------------------------------------------------------------------------------

    material_mgr( xresource::mgr& RscMgr ) : m_RscManager(RscMgr)
    {}

    //----------------------------------------------------------------------------------------

    void SetMaterialInstance(xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, material_instance& MaterialInstance, bool b2D, bool bBilinear )
    {
        auto& Texture = *m_RscManager.getResource(MaterialInstance.m_TextureRef);

        material Material
        { .m_Bilinear  = bBilinear
        , .m_CubeMap   = Texture.isCubemap()
        , .m_3DRender  = !b2D
        , .m_UWrapMode = static_cast<std::uint8_t>(Texture.getAdressModes()[0])
        , .m_VWrapMode = static_cast<std::uint8_t>(Texture.getAdressModes()[1])
        };

        std::uint32_t PipelineGuid = Material.getGuid() ^ MaterialInstance.getGuid(m_RscManager);

        if (auto Entry = m_PipelineInstances.find(PipelineGuid); Entry != m_PipelineInstances.end())
        {
            CmdBuffer.setPipelineInstance(Entry->second);
        }
        else
        {
            auto& Pipeline = getMaterial(Device, Material);
            auto  Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            auto  Setup    = xgpu::pipeline_instance::setup
            { .m_PipeLine           = Pipeline
            , .m_SamplersBindings   = Bindings
            };

            xgpu::pipeline_instance TempPI;
            if (auto Err = Device.Create(TempPI, Setup); Err)
            {
                printf("xtexture_editor preview: %s\n", xgpu::getErrorMsg(Err));
                return;
            }

            CmdBuffer.setPipelineInstance(TempPI);
            m_PipelineInstances.insert({ PipelineGuid, std::move(TempPI) });
        }
    }

    //----------------------------------------------------------------------------------------

    void CreateMaterialInstance(xgpu::device& Device, material_instance& MaterialInstance, const xbitmap& Bitmap)
    {
        assert(MaterialInstance.m_TextureRef.isValid() == false);

        // Generate a unique ID for it
        MaterialInstance.m_TextureRef.m_Instance = xresource::guid_generator::Instance64();

        // Create officially the material instance
        auto Texture = std::make_unique<xgpu::texture>();
        if (auto Err = xgpu::tools::bitmap::Create(*Texture, Device, Bitmap); Err)
        {
            printf("xtexture_editor preview: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        // Register it with the resource manager
        m_RscManager.RegisterResource(MaterialInstance.m_TextureRef, Texture.release() );
    }

    //----------------------------------------------------------------------------------------

    void CreateMaterialInstance(xgpu::device& Device, material_instance& MaterialInstance, xrsc::texture_ref Guid)
    {
        assert(MaterialInstance.m_TextureRef.isValid() == false);
        MaterialInstance.m_TextureRef = Guid;
    }

    //----------------------------------------------------------------------------------------

    void UpdateMaterialInstance( xgpu::device& Device, material_instance& MaterialInstance, xrsc::texture_ref Guid )
    {
        // First let us be sure that we are clear...
        ReleaseMaterialInstance(Device, MaterialInstance );

        // Now we can create a new material instance
        CreateMaterialInstance(Device, MaterialInstance, Guid);
    }

    // Safe preview path taught by E10's bitmap CreateMaterialInstance overload.
    void UpdateFromBitmap(xgpu::device& Device, material_instance& MaterialInstance, const xbitmap& Bitmap)
    {
        ReleaseMaterialInstance(Device, MaterialInstance);
        MaterialInstance.m_TextureRef = {};
        CreateMaterialInstance(Device, MaterialInstance, Bitmap);
    }


    //----------------------------------------------------------------------------------------

    void ReleaseMaterialInstance(xgpu::device& Device, material_instance& MaterialInstance)
    {
        if ( MaterialInstance.m_TextureRef.m_Instance.isValid() == false || false == MaterialInstance.m_TextureRef.m_Instance.isPointer() ) return;
        
        for (int i = 0; i < (1 << material::num_bits_used); ++i)
        {
            material Material;
            Material.m_Value = i;
            std::uint32_t PipelineGuid = Material.getGuid() ^ MaterialInstance.getGuid(m_RscManager);
            if (auto Entry = m_PipelineInstances.find(PipelineGuid); Entry != m_PipelineInstances.end())
            {
                // This is the right code we should do
                Device.Destroy( std::move(Entry->second) );
                m_PipelineInstances.erase(Entry);
            }
        }

        // Release the texture
        m_RscManager.ReleaseRef( MaterialInstance.m_TextureRef );

        // Since we have fully released it when can set it back to null...
        MaterialInstance.m_TextureRef.clear();
    }

    //----------------------------------------------------------------------------------------

    
    xgpu::pipeline& getMaterial(xgpu::device& Device, material Material)
    {
        if (auto Entry = m_Pipelines.find(Material.getGuid()); Entry != m_Pipelines.end())
            return Entry->second;

        xgpu::vertex_descriptor VertexDescriptor;
        if (Material.m_3DRender)
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_3d, m_Position), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_3d, m_Binormal), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_3d, m_Tangent),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_3d, m_Normal),   .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_3d, m_TexCoord), .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            };
            auto Setup = xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(e10::vert_3d), .m_Attributes = Attributes };
            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            {
                printf("xtexture_editor preview: 3D vertex descriptor failed: %s\n", xgpu::getErrorMsg(Err));
                return PreviewEmptyPipeline();
            }
        }
        else
        {
            auto Attributes = std::array
            {
                xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_2d, m_X),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            ,   xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(e10::vert_2d, m_UV), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            };
            auto Setup = xgpu::vertex_descriptor::setup{ .m_VertexSize = sizeof(e10::vert_2d), .m_Attributes = Attributes };
            if (auto Err = Device.Create(VertexDescriptor, Setup); Err)
            {
                printf("xtexture_editor preview: 2D vertex descriptor failed: %s\n", xgpu::getErrorMsg(Err));
                return PreviewEmptyPipeline();
            }
        }

        xgpu::shader FragmentShader;
        xgpu::shader VertexShader;
        auto FailShader = [&](const char* What, auto Err) -> xgpu::pipeline&
        {
            printf("xtexture_editor preview: %s failed: %s\n", What, xgpu::getErrorMsg(Err));
            return PreviewEmptyPipeline();
        };

        if (Material.m_3DRender)
        {
            if (Material.m_CubeMap)
            {
                if (auto Err = Device.Create(FragmentShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader3DCubeSPV } }); Err)
                    return FailShader("3D cube frag", Err);
                if (auto Err = Device.Create(VertexShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ g_VertShader3DCubeSPV } }); Err)
                    return FailShader("3D cube vert", Err);
            }
            else
            {
                if (auto Err = Device.Create(FragmentShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader3DSPV } }); Err)
                    return FailShader("3D frag", Err);
                if (auto Err = Device.Create(VertexShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ g_VertShader3DSPV } }); Err)
                    return FailShader("3D vert", Err);
            }
        }
        else
        {
            if (Material.m_CubeMap)
            {
                if (auto Err = Device.Create(FragmentShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader2DCubeSPV } }); Err)
                    return FailShader("2D cube frag", Err);
                if (auto Err = Device.Create(VertexShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ g_VertShader2DCubeSPV } }); Err)
                    return FailShader("2D cube vert", Err);
            }
            else
            {
                if (auto Err = Device.Create(FragmentShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ g_FragShader2DSPV } }); Err)
                    return FailShader("2D frag", Err);
                if (auto Err = Device.Create(VertexShader, xgpu::shader::setup{ .m_Type = xgpu::shader::type::bit::VERTEX, .m_Sharer = xgpu::shader::setup::raw_data{ g_VertShader2DSPV } }); Err)
                    return FailShader("2D vert", Err);
            }
        }

        auto Shaders  = std::array<const xgpu::shader*, 2>{ &FragmentShader, &VertexShader };
        auto Samplers = std::array{ xgpu::pipeline::sampler{} };
        Samplers[0].m_MipmapMin  = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_sampler::LINEAR : xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        Samplers[0].m_MipmapMag  = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_sampler::LINEAR : xgpu::pipeline::sampler::mipmap_sampler::NEAREST;
        Samplers[0].m_MipmapMode = Material.m_Bilinear ? xgpu::pipeline::sampler::mipmap_mode::LINEAR    : xgpu::pipeline::sampler::mipmap_mode::NEAREST;
        Samplers[0].m_AddressMode[0] = static_cast<xgpu::pipeline::sampler::address_mode>(Material.m_UWrapMode);
        Samplers[0].m_AddressMode[1] = static_cast<xgpu::pipeline::sampler::address_mode>(Material.m_VWrapMode);

        auto Setup = xgpu::pipeline::setup
        {
            .m_VertexDescriptor  = VertexDescriptor
        ,   .m_Shaders           = Shaders
        ,   .m_PushConstantsSize = sizeof(e10::push_contants)
        ,   .m_Samplers          = Samplers
        ,   .m_Primitive         = {.m_Cull = xgpu::pipeline::primitive::cull::NONE }
        ,   .m_DepthStencil      = {.m_bDepthTestEnable = (bool)Material.m_3DRender }
        ,   .m_Blend             = xgpu::pipeline::blend::getAlphaOriginal()
        };

        xgpu::pipeline Temp;
        if (auto Err = Device.Create(Temp, Setup); Err)
        {
            printf("xtexture_editor preview: pipeline create failed: %s\n", xgpu::getErrorMsg(Err));
            return PreviewEmptyPipeline();
        }

        m_Pipelines.insert({Material.getGuid(), std::move(Temp)});
        return m_Pipelines.find(Material.getGuid())->second;
    }
    std::unordered_map<std::uint32_t, xgpu::pipeline>           m_Pipelines;
    std::unordered_map<std::uint32_t, xgpu::pipeline_instance>  m_PipelineInstances;
    xresource::mgr&                                             m_RscManager;
};


struct mesh_mgr
{
    struct mesh
    {
        xgpu::buffer m_VertexBuffer;
        xgpu::buffer m_IndexBuffer;
        int          m_IndexCount = 0;
    };

    enum class model
    { PLANE_2D
    , EXPLODED_CUBE_2D
    , CUBE_3D
    , SPHERE_3D
    , ENUM_COUNT
    };

    //----------------------------------------------------------------------------------

    void Initialize(xgpu::device& Device)
    {
        Create_2DPlane(Device);
        Create_2DExplodedCube(Device);
        Create_3DCube(Device);
        Create_3DSphere(Device);
    }

    //----------------------------------------------------------------------------------

    void Render( xgpu::cmd_buffer& CmdBuffer, model Model )
    {
        auto& Mesh = m_Meshes[static_cast<int>(Model)];
        if (Mesh.m_IndexCount <= 0) return;
        CmdBuffer.setBuffer(Mesh.m_VertexBuffer);
        CmdBuffer.setBuffer(Mesh.m_IndexBuffer);
        CmdBuffer.Draw(Mesh.m_IndexCount);
    }

    //----------------------------------------------------------------------------------

    void Create_2DPlane(xgpu::device& Device)
    {
        mesh& Mesh = m_Meshes[static_cast<int>(model::PLANE_2D)];
        Mesh.m_IndexCount = 6;

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_2d), .m_EntryCount = 4 }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_VertexBuffer.MemoryMap(0, 4, [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_2d*>(pData);
            pVertex[0] = { -100.0f, -100.0f,  { 0.0f, 0.0f, 0.0f } };
            pVertex[1] = {  100.0f, -100.0f,  { 1.0f, 0.0f, 0.0f } };
            pVertex[2] = {  100.0f,  100.0f,  { 1.0f, 1.0f, 0.0f } };
            pVertex[3] = { -100.0f,  100.0f,  { 0.0f, 1.0f, 0.0f } };
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
        {
            auto            pIndex = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);
            for (auto i : StaticIndex)
            {
                *pIndex = i;
                pIndex++;
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_2DExplodedCube(xgpu::device& Device)
    {
        mesh& Mesh = m_Meshes[static_cast<int>(model::EXPLODED_CUBE_2D)];
        Mesh.m_IndexCount = 6 * 6;

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_2d), .m_EntryCount = 4*6 }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_VertexBuffer.MemoryMap(0, 4*6, [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_2d*>(pData);

            int iVert=0;
            pVertex[iVert++] = { 0.0f, -100.0f,  xmath::fvec3(1.0f,  1.0f,  1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 200.0f, -100.0f,  xmath::fvec3(1.0f,  1.0f, -1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 200.0f,  100.0f,  xmath::fvec3(1.0f, -1.0f, -1.0f).NormalizeSafe() };
            pVertex[iVert++] = { 0.0f,  100.0f,  xmath::fvec3(1.0f, -1.0f,  1.0f).NormalizeSafe() };

            for ( int iFace=0; iFace<2; iFace++)
            {
                for (int i=0;i<4;++i)
                {
                    pVertex[iVert] = pVertex[iVert - 4];
                    pVertex[iVert].m_UV = xmath::fvec3(pVertex[iVert].m_UV).RotateY(-90_xdeg);
                    pVertex[iVert].m_X -= 200.0f;
                    iVert++;
                }
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[i];
                pVertex[iVert].m_UV = xmath::fvec3(pVertex[iVert].m_UV).RotateY(90_xdeg);
                pVertex[iVert].m_X += 200.0f;
                iVert++;
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[4 + i];
                pVertex[iVert].m_UV = xmath::fvec3(pVertex[iVert].m_UV).RotateX(-90_xdeg);
                pVertex[iVert].m_Y -= 200.0f;
                iVert++;
            }

            for (int i = 0; i < 4; ++i)
            {
                pVertex[iVert] = pVertex[4 + i];
                pVertex[iVert].m_UV = xmath::fvec3(pVertex[iVert].m_UV).RotateX(90_xdeg);
                pVertex[iVert].m_Y += 200.0f;
                iVert++;
            }

            assert(iVert <= (4 * 6) );
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = Mesh.m_IndexCount }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_IndexBuffer.MemoryMap(0, Mesh.m_IndexCount, [&](void* pData)
        {
            auto            pIndex = static_cast<std::uint32_t*>(pData);
            constexpr auto  StaticIndex = std::array
            {
                2u,  1u,  0u,      3u,  2u,  0u,    // front
            };
            static_assert(StaticIndex.size() == 6);

            for (int iFace = 0; iFace < 6; ++iFace)
            {
                for (auto i : StaticIndex)
                {
                    *pIndex = static_cast<std::uint32_t>(i + iFace* 4);
                    pIndex++;
                }
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_3DCube(xgpu::device& Device)
    {
        const auto  Primitive = xprim_geom::cube::Generate(4, 4, 4, 4, xprim_geom::float3{ 1,1,1 });
        mesh&       Mesh      = m_Meshes[static_cast<int>(model::CUBE_3D)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_3d), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_3d*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Vertices.size()); ++i )
            {
                auto&       V  = pVertex[i];
                const auto& v  = Primitive.m_Vertices[i];
                V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );
                V.m_Normal.setup( v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z );

                V.m_Tangent.setup(v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z);
                V.m_Binormal = (xmath::fvec3{ V.m_Normal }.Cross(xmath::fvec3{ V.m_Tangent } )).NormalizeSafe();

                V.m_TexCoord.setup(v.m_Texcoord.m_X, v.m_Texcoord.m_Y);
            }
        });
        
        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Indices.size()); ++i )
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    //----------------------------------------------------------------------------------

    void Create_3DSphere(xgpu::device& Device)
    {
        const auto  Primitive = xprim_geom::uvsphere::Generate( 70, 70, 1, 0.5f );
        mesh&       Mesh      = m_Meshes[static_cast<int>(model::SPHERE_3D)];

        Mesh.m_IndexCount = static_cast<int>(Primitive.m_Indices.size());

        if (auto Err = Device.Create(Mesh.m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(e10::vert_3d), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_VertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
        {
            auto pVertex = static_cast<e10::vert_3d*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Vertices.size()); ++i )
            {
                auto&       V  = pVertex[i];
                const auto& v  = Primitive.m_Vertices[i];
                V.m_Position.setup( v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z );
                V.m_Normal.setup( v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z );

                V.m_Tangent.setup(v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z);
                V.m_Binormal = (xmath::fvec3{ V.m_Normal }.Cross(xmath::fvec3{ V.m_Tangent } )).NormalizeSafe();

                V.m_TexCoord.setup(v.m_Texcoord.m_X, v.m_Texcoord.m_Y);
            }
        });

        if (auto Err = Device.Create(Mesh.m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = static_cast<int>(Primitive.m_Indices.size()) }); Err)
        {
            printf("xtexture_editor preview: mesh buffer failed: %s\n", xgpu::getErrorMsg(Err));
            return;
        }

        (void)Mesh.m_IndexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Indices.size()), [&](void* pData)
        {
            auto            pIndex      = static_cast<std::uint32_t*>(pData);
            for( int i=0; i< static_cast<int>(Primitive.m_Indices.size()); ++i )
            {
                pIndex[i] = Primitive.m_Indices[i];
            }
        });
    }

    std::array<mesh, static_cast<int>(mesh_mgr::model::ENUM_COUNT)> m_Meshes;
};






    inline std::wstring ResourcePathFromDescriptorPath(std::wstring DescriptorPath) noexcept
    {
        auto ReplaceCI = [](std::wstring& S, std::wstring_view From, std::wstring_view To) -> bool
        {
            if (From.size() > S.size()) return false;
            for (size_t i = 0; i + From.size() <= S.size(); ++i)
            {
                bool Match = true;
                for (size_t j = 0; j < From.size(); ++j)
                {
                    if (towlower(S[i + j]) != towlower(From[j])) { Match = false; break; }
                }
                if (Match)
                {
                    S.replace(i, From.size(), To);
                    return true;
                }
            }
            return false;
        };

        ReplaceCI(DescriptorPath, L"info.txt", L"Descriptor.txt");
        if (DescriptorPath.rfind(L'\\') == std::wstring::npos) return {};
        std::wstring ResourcePath = DescriptorPath.substr(0, DescriptorPath.rfind(L'\\') - sizeof("desc"));

        size_t pos = ResourcePath.rfind(L"Descriptors");
        if (pos == std::wstring::npos) return {};

        if (pos >= 7
            && DescriptorPath[pos - 1] == L'\\'
            && DescriptorPath[pos - 2] == L'e'
            && DescriptorPath[pos - 3] == L'h'
            && DescriptorPath[pos - 4] == L'c'
            && DescriptorPath[pos - 5] == L'a'
            && DescriptorPath[pos - 6] == L'C'
            && DescriptorPath[pos - 7] == L'\\')
        {
            ResourcePath.replace(pos, sizeof("Descriptors"), L"Resources\\Platforms\\WINDOWS\\");
        }
        else
        {
            ResourcePath.replace(pos, sizeof("Descriptors"), L"Cache\\Resources\\Platforms\\WINDOWS\\");
        }
        return ResourcePath;
    }

    struct runtime
    {
        xgpu::device*                  m_pDevice = nullptr;
        material_mgr                    m_Materials{xresource::g_Mgr};
        mesh_mgr                        m_Meshes{};
        draw_options                    m_DrawOptions{};
        draw_controls                   m_DrawControls{m_DrawOptions};
        e05::bitmap_inspector           m_BitmapInspector{};
        material_mgr::material_instance m_UserMaterial{};
        material_mgr::material_instance m_BackgroundMaterial{};
        std::wstring                    m_ResourcePath{};
        bool                            m_bGpuReady = false;
        bool                            m_bHasTexture = false;

        void Init(xgpu::device& Device) noexcept
        {
            if (m_bGpuReady) return;
            m_pDevice = &Device;
            m_Meshes.Initialize(Device);
            m_Materials.CreateMaterialInstance(Device, m_BackgroundMaterial, xbitmap::getDefaultBitmap());
            m_DrawOptions.m_RenderMode = draw_options::render_mode::RENDER_2D;
            m_bGpuReady = true;
        }

        void LoadFromDescriptorPath(const std::wstring& DescriptorPath) noexcept
        {
            m_bHasTexture = false;
            if (!m_pDevice) return;
            m_ResourcePath = ResourcePathFromDescriptorPath(DescriptorPath);
            if (m_ResourcePath.empty() || !std::filesystem::exists(m_ResourcePath))
            {
                m_BitmapInspector.clear();
                return;
            }

            m_BitmapInspector.Load(m_ResourcePath, true);
            if (!m_BitmapInspector.m_pBitmap || m_BitmapInspector.m_pBitmap->getFormat() == xbitmap::format::INVALID)
                return;

            m_Materials.UpdateFromBitmap(*m_pDevice, m_UserMaterial, *m_BitmapInspector.m_pBitmap);

            m_DrawOptions.m_MaxMipLevels = m_BitmapInspector.m_pBitmap->getMipCount() - 1;
            if (m_DrawOptions.m_ChooseMipLevel >= m_DrawOptions.m_MaxMipLevels)
                m_DrawOptions.m_ChooseMipLevel = std::min(m_DrawOptions.m_MaxMipLevels, std::max(0, m_DrawOptions.m_MaxMipLevels - 1));

            m_bHasTexture = m_UserMaterial.m_TextureRef.isValid();
        }

        // E10 2D input, verbatim math (MainWindow size -> ViewW/H; xgpu mouse -> ImGui on the preview canvas item).
        void Handle2DInput(float ViewW, float ViewH) noexcept
        {
            if (ViewW <= 1.f || ViewH <= 1.f) return;
            // Caller must have just submitted the preview canvas item (InvisibleButton).
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;

            const float OldScale = m_DrawControls.m_2DMouseScale;
            auto& io = ImGui::GetIO();

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                const float MouseDeltaX = io.MouseDelta.x;
                const float MouseDeltaY = io.MouseDelta.y;

                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    m_DrawControls.m_2DMouseScale -= 8000.0f * io.DeltaTime * (MouseDeltaY * (2.0f / ViewH));
                }
                else
                {
                    m_DrawControls.m_2DMouseTranslate.m_X += MouseDeltaX * (2.0f / ViewW);
                    m_DrawControls.m_2DMouseTranslate.m_Y += MouseDeltaY * (2.0f / ViewH);
                }
            }

            // Wheel scale (E10: Mouse.getValue(WHEEL_REL)[0])
            const double Wheel = io.MouseWheel;
            m_DrawControls.m_2DMouseScale += static_cast<float>(2000.9f * io.DeltaTime * (Wheel * Wheel * Wheel));
            m_DrawControls.m_2DMouseScale = std::max(m_DrawControls.m_2DMouseScale, 0.1f);

            // Always zoom from the perspective of the mouse (E10: POS_ABS / MainWindow size)
            const ImVec2 Origin = ImGui::GetItemRectMin();
            const ImVec2 Mouse  = ImGui::GetMousePos();
            const float mx = (((Mouse.x - Origin.x) / ViewW) - 0.5f) * 2.0f;
            const float my = (((Mouse.y - Origin.y) / ViewH) - 0.5f) * 2.0f;
            m_DrawControls.m_2DMouseTranslate.m_X += (m_DrawControls.m_2DMouseTranslate.m_X - mx) * (m_DrawControls.m_2DMouseScale - OldScale) / OldScale;
            m_DrawControls.m_2DMouseTranslate.m_Y += (m_DrawControls.m_2DMouseTranslate.m_Y - my) * (m_DrawControls.m_2DMouseScale - OldScale) / OldScale;
        }

        void Draw2D(xgpu::cmd_buffer& CmdBuffer, float ViewW, float ViewH) noexcept
        {
            if (!m_pDevice || !m_bGpuReady || ViewW <= 1.f || ViewH <= 1.f) return;

            {
                m_Materials.SetMaterialInstance(*m_pDevice, CmdBuffer, m_BackgroundMaterial, true, true);
                e10::push_contants PC{};
                PC.m_Scale = { (150 * 2.0f) / ViewW, (150 * 2.0f) / ViewH };
                PC.m_Translation.setup(0);
                PC.m_UVScale = { 120.0f, 120.0f };
                PC.m_ToGamma = 2.2f;
                PC.m_ColorMask = xmath::fvec4(1);
                PC.m_Mode = xmath::fvec4(0, 0, 1, 1);
                PC.m_TintColor = xmath::fvec4(m_DrawOptions.m_BackgroundIntensity
                    , m_DrawOptions.m_BackgroundIntensity
                    , m_DrawOptions.m_BackgroundIntensity
                    , 1);
                PC.m_MipLevel = 0;
                PC.m_NormalModes.setup(0);
                CmdBuffer.setPushConstants(PC);
                m_Meshes.Render(CmdBuffer, mesh_mgr::model::PLANE_2D);
            }

            if (!m_bHasTexture || !m_BitmapInspector.m_pBitmap) return;

            e10::push_contants PC{};
            PC.m_Scale.m_X = (m_DrawControls.m_2DMouseScale * 0.01f) / (ViewW / ViewH) * m_BitmapInspector.m_pBitmap->getAspectRatio();
            PC.m_Scale.m_Y = (m_DrawControls.m_2DMouseScale * 0.01f);
            PC.m_UVScale = m_DrawOptions.m_UVScale;
            PC.m_Translation = m_DrawControls.m_2DMouseTranslate;

            const float MipMode = m_DrawOptions.m_ChooseMipLevel == -1 ? 1.0f : 0.0f;
            switch (m_DrawOptions.m_ChannelsMode)
            {
            case draw_options::channels_mode::COLOR_ALPHA:
                PC.m_ColorMask = xmath::fvec4(1); PC.m_Mode = xmath::fvec4(1, 0, 0, MipMode); break;
            case draw_options::channels_mode::NO_ALPHA:
                PC.m_ColorMask = xmath::fvec4(1); PC.m_Mode = xmath::fvec4(0, 0, 1, MipMode); break;
            case draw_options::channels_mode::A_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 0, 0, 1); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::R_ONLY:
                PC.m_ColorMask = xmath::fvec4(1, 0, 0, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::G_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 1, 0, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::B_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 0, 1, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            }

            if (m_BitmapInspector.m_pBitmap->getFormat() == xbitmap::format::BC3_81Y0X_NORMAL
                && m_DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                PC.m_NormalModes = xmath::fvec4(1, 0, 0, 0);
            else if (m_BitmapInspector.m_pBitmap->getFormat() == xbitmap::format::BC5_8YX_NORMAL
                && m_DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                PC.m_NormalModes = xmath::fvec4(0, 1, 0, 0);
            else
                PC.m_NormalModes = xmath::fvec4(0, 0, 0, 0);

            PC.m_MipLevel = static_cast<float>(std::max(0, m_DrawOptions.m_ChooseMipLevel));
            PC.m_TintColor = xmath::fvec4(1);
            switch (m_DrawOptions.m_DisplayInGammaMode)
            {
            case draw_options::display_gamma_mode::GAMMA:   PC.m_ToGamma = m_DrawOptions.m_DisplayGamma; break;
            case draw_options::display_gamma_mode::LINEAR:  PC.m_ToGamma = 1; break;
            case draw_options::display_gamma_mode::RAW_DATA_INFILE:
                PC.m_ToGamma = (m_BitmapInspector.m_pBitmap->getColorSpace() == xbitmap::color_space::SRGB) ? 2.2f : 1.0f;
                break;
            }

            m_Materials.SetMaterialInstance(*m_pDevice, CmdBuffer, m_UserMaterial, true, m_DrawOptions.m_bBilinearMode);
            CmdBuffer.setPushConstants(PC);
            if (m_BitmapInspector.m_pBitmap->isCubemap())
                m_Meshes.Render(CmdBuffer, mesh_mgr::model::EXPLODED_CUBE_2D);
            else
                m_Meshes.Render(CmdBuffer, mesh_mgr::model::PLANE_2D);
        }

        // E10 3D input (right-drag orbit, wheel distance, space toggles light follow). LookAt runs in Draw3D.
        void Handle3DInput(float ViewW, float ViewH) noexcept
        {
            if (ViewW <= 1.f || ViewH <= 1.f) return;
            if (!ImGui::IsItemHovered() && !ImGui::IsItemActive()) return;

            auto& io = ImGui::GetIO();
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                const float MousePosX = io.MouseDelta.x;
                const float MousePosY = io.MouseDelta.y;
                m_DrawControls.m_3DAngles.m_Pitch.m_Value -= 0.01f * MousePosY;
                m_DrawControls.m_3DAngles.m_Yaw.m_Value   -= 0.01f * MousePosX;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
            {
                m_DrawControls.m_3DLightPosition = m_DrawControls.m_3DView.getPosition();
                m_DrawControls.m_3DFollowCamera = !m_DrawControls.m_3DFollowCamera;
            }

            m_DrawControls.m_3DDistance += m_DrawControls.m_3DDistance * -0.2f * io.MouseWheel;
            m_DrawControls.m_3DDistance = std::max(m_DrawControls.m_3DDistance, 0.2f);
        }

        void Draw3D(xgpu::cmd_buffer& CmdBuffer, float ViewW, float ViewH) noexcept
        {
            if (!m_pDevice || !m_bGpuReady || ViewW <= 1.f || ViewH <= 1.f) return;
            if (!m_bHasTexture || !m_BitmapInspector.m_pBitmap) return;
            m_DrawControls.m_3DView.LookAt(m_DrawControls.m_3DDistance, m_DrawControls.m_3DAngles, { 0,0,0 });

            e10::push_contants PC{};
            const float MipMode = m_DrawOptions.m_ChooseMipLevel == -1 ? 1.0f : 0.0f;
            switch (m_DrawOptions.m_ChannelsMode)
            {
            case draw_options::channels_mode::COLOR_ALPHA:
                PC.m_ColorMask = xmath::fvec4(1); PC.m_Mode = xmath::fvec4(1, 0, 0, MipMode); break;
            case draw_options::channels_mode::NO_ALPHA:
                PC.m_ColorMask = xmath::fvec4(1); PC.m_Mode = xmath::fvec4(0, 0, 1, MipMode); break;
            case draw_options::channels_mode::A_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 0, 0, 1); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::R_ONLY:
                PC.m_ColorMask = xmath::fvec4(1, 0, 0, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::G_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 1, 0, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            case draw_options::channels_mode::B_ONLY:
                PC.m_ColorMask = xmath::fvec4(0, 0, 1, 0); PC.m_Mode = xmath::fvec4(0, 1, 0, MipMode); break;
            }

            if (m_BitmapInspector.m_pBitmap->getFormat() == xbitmap::format::BC3_81Y0X_NORMAL
                && m_DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                PC.m_NormalModes = xmath::fvec4(1, 0, 0, 0);
            else if (m_BitmapInspector.m_pBitmap->getFormat() == xbitmap::format::BC5_8YX_NORMAL
                && m_DrawOptions.m_DisplayInGammaMode != draw_options::display_gamma_mode::RAW_DATA_INFILE)
                PC.m_NormalModes = xmath::fvec4(0, 1, 0, 0);
            else
                PC.m_NormalModes = xmath::fvec4(0, 0, 0, 0);

            PC.m_MipLevel = static_cast<float>(std::max(0, m_DrawOptions.m_ChooseMipLevel));
            PC.m_TintColor = xmath::fvec4(1);
            PC.m_UVScale = m_DrawOptions.m_UVScale;
            switch (m_DrawOptions.m_DisplayInGammaMode)
            {
            case draw_options::display_gamma_mode::GAMMA:   PC.m_ToGamma = m_DrawOptions.m_DisplayGamma; break;
            case draw_options::display_gamma_mode::LINEAR:  PC.m_ToGamma = 1; break;
            case draw_options::display_gamma_mode::RAW_DATA_INFILE:
                PC.m_ToGamma = (m_BitmapInspector.m_pBitmap->getColorSpace() == xbitmap::color_space::SRGB) ? 2.2f : 1.0f;
                break;
            }

            m_Materials.SetMaterialInstance(*m_pDevice, CmdBuffer, m_UserMaterial, false, m_DrawOptions.m_bBilinearMode);

            m_DrawControls.m_3DView.setViewport({ 0, 0, static_cast<int>(ViewW), static_cast<int>(ViewH) });
            const auto W2C = m_DrawControls.m_3DView.getW2C();
            xmath::fmat4 L2W;
            L2W.setupIdentity();
            if (m_BitmapInspector.m_pBitmap->isValid())
                L2W.setupScale(xmath::fvec3{ m_BitmapInspector.m_pBitmap->getAspectRatio(), 1, m_BitmapInspector.m_pBitmap->getAspectRatio() } * 2.0f);

            auto W2L = L2W;
            W2L = W2L.InverseSRT();
            if (m_DrawControls.m_3DFollowCamera)
                m_DrawControls.m_3DLightPosition = m_DrawControls.m_3DView.getPosition();

            PC.m_L2C = W2C * L2W;
            PC.m_LocalSpaceLightPosition = W2L * m_DrawControls.m_3DLightPosition;
            if (m_DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_3D_WITH_LIGHTING)
                PC.m_NormalModes.m_Z = 1;

            CmdBuffer.setPushConstants(PC);
            if (m_BitmapInspector.m_pBitmap->isCubemap())
                m_Meshes.Render(CmdBuffer, mesh_mgr::model::SPHERE_3D);
            else
                m_Meshes.Render(CmdBuffer, mesh_mgr::model::CUBE_3D);
        }

        void HandleInput(float ViewW, float ViewH) noexcept
        {
            if (m_DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_2D)
                Handle2DInput(ViewW, ViewH);
            else
                Handle3DInput(ViewW, ViewH);
        }

        void Draw(xgpu::cmd_buffer& CmdBuffer, float ViewW, float ViewH) noexcept
        {
            if (m_DrawOptions.m_RenderMode == draw_options::render_mode::RENDER_2D)
                Draw2D(CmdBuffer, ViewW, ViewH);
            else
                Draw3D(CmdBuffer, ViewW, ViewH);
        }
    };

}

#endif // XTEXTURE_EDITOR_PREVIEW_H
