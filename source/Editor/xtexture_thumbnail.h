#ifndef XTEXTURE_THUMBNAIL_H
#define XTEXTURE_THUMBNAIL_H
#pragma once

// Texture's thumbnail renderer: the compiled texture itself, drawn as a simple tinted quad - reuses the
// exact draw_vert.h/draw_frag.h shader pair xeditor_mesh_preview.h already uses for its own checker
// background, rather than authoring a new one. A cubemap can't go through that path at all (confirmed
// live: VUID-vkCmdDrawIndexed-viewType-07752, a CUBE image view against a plain sampler2D) - it gets its
// own small cube mesh + the e10_3d_cube_vert/frag.glsl pair E10_TextureResourcePipeline already ships
// (a samplerCube debug-view shader, reused here instead of authoring a new one), viewed from a fixed angle.
#include "source/Tools/Editor/xeditor_thumbnail.h"
#include "source/Tools/Editor/xeditor_resource_editor.h"
#include "source/Tools/xgpu_view.h"
#include "source/xGPU.h"
#include "Plugins/xtexture.plugin/source/xtexture_xgpu_rsc_loader.h"
#include "dependencies/xprim_geom/source/xprim_geom.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>

namespace xtexture
{
    struct thumb_vert { float m_X, m_Y, m_Z; float m_U, m_V; std::uint32_t m_Color; };
    struct thumb_push_const { xmath::fmat4 m_L2C; };

    // Matches e10_3d_cube_vert.glsl's attribute layout exactly (position/binormal/tangent/normal/uv) -
    // binormal/tangent/uv are only there to satisfy the shader's inputs (unused by a plain unlit cubemap
    // sample: NormalModes below stays all-zero, so the frag shader's normal/lighting branches never engage).
    struct cube_vert { float m_X, m_Y, m_Z; float m_BX, m_BY, m_BZ; float m_TX, m_TY, m_TZ; float m_NX, m_NY, m_NZ; float m_U, m_V; };

    // Matches e10_3d_cube_vert/frag.glsl's uPushConstant exactly (field order and sizes must line up byte-
    // for-byte with the GLSL std140-ish push-constant block).
    struct cube_push_const
    {
        float           m_MipLevel      {0};
        float           m_ToGamma       {2.2f};
        xmath::fvec2    m_UScale        {1,1};
        xmath::fvec2    m_UTranslate    {0,0};
        xmath::fvec2    m_UVScale       {1,1};
        xmath::fvec4    m_TintColor     {1,1,1,1};
        xmath::fvec4    m_ColorMask     {1,0,0,0};
        xmath::fvec4    m_Mode          {1,0,0,1};      // .x: use Color as-is: .w: texture() not textureLod()
        xmath::fvec4    m_NormalModes   {0,0,0,0};      // all zero: no normal-map decode, no lighting
        xmath::fmat4    m_L2C;
        xmath::fvec3    m_LocalSpaceLightPos {0,0,0};
        xmath::fvec4    m_UVMode        {0,0,0,0};
    };

    class thumbnail_renderer final : public xeditor::thumbnail_renderer
    {
    public:
        bool Init(xgpu::device& Device) noexcept override
        {
            if (m_bReady) return true;
            m_pDevice = &Device;

            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(thumb_vert, m_X),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(thumb_vert, m_U),     .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(thumb_vert, m_Color), .m_Format = xgpu::vertex_descriptor::format::UINT8_4D_NORMALIZED }
            };
            if (!Ok(Device.Create(m_VD, { .m_VertexSize = sizeof(thumb_vert), .m_Attributes = Attributes }))) return false;

            static constexpr auto s_VertShader = std::array
            {
                #include "draw_vert.h"
            };
            static constexpr auto s_FragShader = std::array
            {
                #include "draw_frag.h"
            };

            xgpu::shader Vert, Frag;
            if (!Ok(Device.Create(Vert, { .m_Type = xgpu::shader::type::bit::VERTEX,   .m_Sharer = xgpu::shader::setup::raw_data{ s_VertShader } }))) return false;
            if (!Ok(Device.Create(Frag, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ s_FragShader } }))) return false;

            auto Shaders  = std::array<const xgpu::shader*, 2>{ &Frag, &Vert };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            // Cull defaults to BACK and depth-test defaults to enabled (xgpu_pipeline.h) - both wrong for a
            // single full-screen quad drawn into a colour-only render target with no depth attachment at
            // all: with depth-test on and no depth buffer bound, and with the wrong winding for this quad
            // culled away, every fragment failed and the "thumbnail" was just the render pass's own clear
            // colour (confirmed live: every generated PNG came out a flat, uniform grey - the clear colour
            // exactly, no texture content, no vertex-colour gradient - not a blank/zeroed readback and not a
            // texture-sampling bug, so the draw itself never contributed a single pixel).
            if (!Ok(Device.Create(m_Pipeline, { .m_VertexDescriptor = m_VD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(thumb_push_const)
                , .m_Samplers = Samplers
                , .m_Primitive = { .m_Cull = xgpu::pipeline::primitive::cull::NONE }
                , .m_DepthStencil = { .m_bDepthTestEnable = false }
                }))) return false;

            // A single full-NDC quad ([-1,1]x[-1,1]), UV top-left origin, white tint. Confirmed against the
            // source image directly (not just "looks right"): this mapping reproduces the source pixel-for-
            // pixel, so it stays as-is - do not "fix" this again without that same direct comparison.
            if (!Ok(Device.Create(m_VertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(thumb_vert), .m_EntryCount = 4 }))) return false;
            (void)m_VertexBuffer.MemoryMap(0, 4, [&](void* pData)
            {
                auto p = static_cast<thumb_vert*>(pData);
                p[0] = { -1,-1, 0,  0,1, 0xffffffff };
                p[1] = {  1,-1, 0,  1,1, 0xffffffff };
                p[2] = {  1, 1, 0,  1,0, 0xffffffff };
                p[3] = { -1, 1, 0,  0,0, 0xffffffff };
            });
            if (!Ok(Device.Create(m_IndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = 6 }))) return false;
            (void)m_IndexBuffer.MemoryMap(0, 6, [&](void* pData)
            {
                static constexpr std::uint32_t Idx[6] = { 0,1,2, 0,2,3 };
                std::memcpy(pData, Idx, sizeof(Idx));
            });

            if (!InitCube(Device)) return false;

            m_bReady = true;
            return true;
        }

        bool InitCube(xgpu::device& Device) noexcept
        {
            auto Attributes = std::array
            { xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(cube_vert, m_X),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(cube_vert, m_BX), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(cube_vert, m_TX), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(cube_vert, m_NX), .m_Format = xgpu::vertex_descriptor::format::FLOAT_3D }
            , xgpu::vertex_descriptor::attribute{ .m_Offset = offsetof(cube_vert, m_U),  .m_Format = xgpu::vertex_descriptor::format::FLOAT_2D }
            };
            if (!Ok(Device.Create(m_CubeVD, { .m_VertexSize = sizeof(cube_vert), .m_Attributes = Attributes }))) return false;

            static constexpr auto s_CubeVertShader = std::array
            {
                #include "e10_3d_cube_vert.h"
            };
            static constexpr auto s_CubeFragShader = std::array
            {
                #include "e10_3d_cube_frag.h"
            };

            xgpu::shader Vert, Frag;
            if (!Ok(Device.Create(Vert, { .m_Type = xgpu::shader::type::bit::VERTEX,   .m_Sharer = xgpu::shader::setup::raw_data{ s_CubeVertShader } }))) return false;
            if (!Ok(Device.Create(Frag, { .m_Type = xgpu::shader::type::bit::FRAGMENT, .m_Sharer = xgpu::shader::setup::raw_data{ s_CubeFragShader } }))) return false;

            auto Shaders  = std::array<const xgpu::shader*, 2>{ &Frag, &Vert };
            auto Samplers = std::array{ xgpu::pipeline::sampler{} };
            if (!Ok(Device.Create(m_CubePipeline, { .m_VertexDescriptor = m_CubeVD, .m_Shaders = Shaders, .m_PushConstantsSize = sizeof(cube_push_const)
                , .m_Samplers = Samplers
                , .m_Primitive = { .m_Cull = xgpu::pipeline::primitive::cull::BACK }
                , .m_DepthStencil = { .m_bDepthTestEnable = false }   // same colour-only, no-depth-attachment render target as the quad path
                }))) return false;

            const auto Primitive = xprim_geom::cube::Generate(1, 1, 1, 1, xprim_geom::float3{ 0.7f, 0.7f, 0.7f });

            if (!Ok(Device.Create(m_CubeVertexBuffer, { .m_Type = xgpu::buffer::type::VERTEX, .m_EntryByteSize = sizeof(cube_vert), .m_EntryCount = static_cast<int>(Primitive.m_Vertices.size()) }))) return false;
            (void)m_CubeVertexBuffer.MemoryMap(0, static_cast<int>(Primitive.m_Vertices.size()), [&](void* pData)
            {
                auto p = static_cast<cube_vert*>(pData);
                for (int i = 0; i < static_cast<int>(Primitive.m_Vertices.size()); ++i)
                {
                    const auto& v = Primitive.m_Vertices[i];
                    // Binormal isn't stored directly by xprim_geom - it's the tangent's own sign-carrying
                    // fourth component (m_D) combined with normal x tangent, same derivation E19's own
                    // draw_vert.m_TW field leaves for ITS consumer to do (there, the consumer never bothers
                    // since its shader ignores binormal entirely; here e10_3d_cube_vert wants it directly).
                    const xmath::fvec3 N{ v.m_Normal.m_X, v.m_Normal.m_Y, v.m_Normal.m_Z };
                    const xmath::fvec3 T{ v.m_Tangent.m_X, v.m_Tangent.m_Y, v.m_Tangent.m_Z };
                    const auto B = N.Cross(T) * v.m_Tangent.m_D;
                    p[i] = { v.m_Position.m_X, v.m_Position.m_Y, v.m_Position.m_Z
                           , B.m_X, B.m_Y, B.m_Z
                           , T.m_X, T.m_Y, T.m_Z
                           , N.m_X, N.m_Y, N.m_Z
                           , v.m_Texcoord.m_X, v.m_Texcoord.m_Y };
                }
            });

            m_CubeIndexCount = static_cast<int>(Primitive.m_Indices.size());
            if (!Ok(Device.Create(m_CubeIndexBuffer, { .m_Type = xgpu::buffer::type::INDEX, .m_EntryByteSize = sizeof(std::uint32_t), .m_EntryCount = m_CubeIndexCount }))) return false;
            (void)m_CubeIndexBuffer.MemoryMap(0, m_CubeIndexCount, [&](void* pData)
            {
                std::memcpy(pData, Primitive.m_Indices.data(), Primitive.m_Indices.size() * sizeof(std::uint32_t));
            });

            return true;
        }

        bool Draw(xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, xresource::full_guid Guid) noexcept override
        {
            if (!m_bReady) return false;
            auto* pTexture = Reference(Guid);
            if (!pTexture) return false;

            if (pTexture->isCubemap()) return DrawCube(Device, CmdBuffer, *pTexture);

            // The pipeline_instance binds THIS texture's sampler, so it's per-resource - built fresh each
            // call rather than cached (thumbnail generation is already rate-limited/rare), then handed to
            // the device's death-march queue (DestroyGpu's own rule: never just drop a live GPU object).
            xgpu::pipeline_instance Instance;
            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ *pTexture } };
            if (!Ok(Device.Create(Instance, { .m_PipeLine = m_Pipeline, .m_SamplersBindings = Bindings }))) return false;

            thumb_push_const PushConst{ .m_L2C = xmath::fmat4::fromIdentity() };
            CmdBuffer.setPipelineInstance(Instance);
            CmdBuffer.setPushConstants(PushConst);
            CmdBuffer.setBuffer(m_VertexBuffer);
            CmdBuffer.setBuffer(m_IndexBuffer);
            CmdBuffer.Draw(6);

            xeditor::DestroyGpu(&Device, Instance);
            return true;
        }

        bool DrawCube(xgpu::device& Device, xgpu::cmd_buffer& CmdBuffer, xgpu::texture& Texture) noexcept
        {
            xgpu::pipeline_instance Instance;
            auto Bindings = std::array{ xgpu::pipeline_instance::sampler_binding{ Texture } };
            if (!Ok(Device.Create(Instance, { .m_PipeLine = m_CubePipeline, .m_SamplersBindings = Bindings }))) return false;

            // Fixed 3/4 angle, non-interactive (a thumbnail, not a viewer). The bounding-SPHERE distance
            // (half-extent*sqrt(3) / sin(halfFOV)) is only a conservative upper bound - a cube's own
            // silhouette from a fixed corner-on angle is a hexagon strictly smaller than its circumscribing
            // sphere, so that bound alone still left visible margin (confirmed live, twice: "still not
            // maximizing the viewport"). Since the viewing angle here is FIXED (not user-orbitable), the
            // exact tightest-fit distance for THIS specific angle can be solved directly instead of settling
            // for the angle-agnostic conservative one: project the cube's 8 corners through a reference view
            // at an arbitrary distance, find how far the worst corner lands outside the [-1,1] NDC frame, and
            // scale distance by exactly that factor - perspective NDC extent scales as ~1/Distance once
            // Distance is much larger than the object (true here, checked: an 8x reference distance keeps
            // the linear approximation well under a pixel of error at 128x128), so one scale-and-done pass
            // is enough, no iteration needed.
            constexpr float HalfExtent = 0.35f;   // InitCube's Size{0.7,0.7,0.7} / 2 - Generate's Size is a FULL side length (its own StartPos = Size * -0.5f)
            xmath::radian3 Angles;
            Angles.m_Pitch = -30_xdeg;
            Angles.m_Yaw   =  45_xdeg;

            // No orthographic mode on this view class - a narrow FOV + far camera approximates one closely
            // enough to avoid the near-face-vs-far-face size distortion a wide FOV close-up would show on
            // a small icon-sized preview (the user's own suggested approximation). The exact-fit distance
            // solve below adapts to whatever FOV is set here automatically, no other change needed.
            xgpu::tools::view View;
            View.setFov(10_xdeg);
            View.setAspect(1.0f);
            View.setViewport({ 0, 0, 128, 128 });   // matches xeditor_thumbnail_cache::s_CellPixels - fixed by this whole feature's own design

            float Distance;
            {
                constexpr float RefDistance = 100.0f * HalfExtent;   // far enough that the 1/Distance approximation below is accurate to well under a pixel
                View.LookAt(RefDistance, Angles, { 0,0,0 });
                const auto& RefW2C = View.getW2C();
                float MaxNdc = 0.0f;
                for (float sx : { -1.0f, 1.0f }) for (float sy : { -1.0f, 1.0f }) for (float sz : { -1.0f, 1.0f })
                {
                    const auto Clip = RefW2C * xmath::fvec4{ sx * HalfExtent, sy * HalfExtent, sz * HalfExtent, 1.0f };
                    MaxNdc = std::max({ MaxNdc, std::fabs(Clip.m_X / Clip.m_W), std::fabs(Clip.m_Y / Clip.m_W) });
                }
                Distance = RefDistance * MaxNdc;   // the exact scale that brings the worst corner to the frame's edge
            }
            View.LookAt(Distance, Angles, { 0,0,0 });

            cube_push_const PushConst{};
            // The generic vertical flip in xeditor_thumbnail_cache.h's readback path (needed and confirmed
            // correct for the flat quad path) flips this 3D camera render the wrong way relative to it -
            // cancel that out here by flipping the already-projected clip-space Y before it ever reaches
            // the rasterizer, rather than touching the shared readback code every other type also uses.
            PushConst.m_L2C = xmath::fmat4::fromScale({ 1,-1,1 }) * View.getW2C();
            CmdBuffer.setPipelineInstance(Instance);
            CmdBuffer.setPushConstants(PushConst);
            CmdBuffer.setBuffer(m_CubeVertexBuffer);
            CmdBuffer.setBuffer(m_CubeIndexBuffer);
            CmdBuffer.Draw(m_CubeIndexCount);

            xeditor::DestroyGpu(&Device, Instance);
            return true;
        }

    private:
        static bool Ok(xgpu::device::error* pErr) noexcept
        {
            if (!pErr) return true;
            std::printf("Texture thumbnail: %s\n", std::string(xgpu::getErrorMsg(pErr)).c_str());
            return false;
        }

        // Keeps the texture loaded for a little while after its last thumbnail request - a render just
        // recorded here is only actually consumed by the GPU (and, further out, read back) several frames
        // later, so releasing the reference the moment Draw returns could free the texture out from under an
        // in-flight render. Mirrors xeditor_texture_thumbnails.h's own LRU-by-GUID exactly, for the same reason.
        xgpu::texture* Reference(const xresource::full_guid& Guid) noexcept
        {
            if (auto It = m_Refs.find(Guid); It != m_Refs.end())
            {
                m_Order.remove(Guid);
                m_Order.push_front(Guid);
                return xresource::g_Mgr.getResource(It->second);
            }

            xrsc::texture_ref Ref;
            Ref.m_Instance = Guid.m_Instance;
            auto& Held = m_Refs.emplace(Guid, Ref).first->second;
            m_Order.push_front(Guid);
            while (m_Refs.size() > m_Capacity)
            {
                const auto Oldest = m_Order.back();
                if (auto It = m_Refs.find(Oldest); It != m_Refs.end()) { xresource::g_Mgr.ReleaseRef(It->second); m_Refs.erase(It); }
                m_Order.pop_back();
            }
            return xresource::g_Mgr.getResource(Held);
        }

        xgpu::device*                                                m_pDevice   = nullptr;
        bool                                                         m_bReady    = false;
        xgpu::vertex_descriptor                                      m_VD;
        xgpu::pipeline                                                m_Pipeline;
        xgpu::buffer                                                  m_VertexBuffer, m_IndexBuffer;
        xgpu::vertex_descriptor                                      m_CubeVD;
        xgpu::pipeline                                                m_CubePipeline;
        xgpu::buffer                                                  m_CubeVertexBuffer, m_CubeIndexBuffer;
        int                                                            m_CubeIndexCount = 0;
        std::size_t                                                   m_Capacity  = 20;
        std::unordered_map<xresource::full_guid, xrsc::texture_ref>  m_Refs;
        std::list<xresource::full_guid>                               m_Order;
    };

    inline const xeditor::auto_register_thumbnail_renderer g_ThumbnailRegistration
    { xrsc::texture_type_guid_v
    , [] { return std::make_unique<thumbnail_renderer>(); }
    };
}

#endif // XTEXTURE_THUMBNAIL_H
