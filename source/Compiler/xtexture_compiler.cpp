
#include "xtexture_compiler.h"
#include "../../dependencies/xbmp_tools/src/xbmp_tools.h"
#include "../../dependencies/crunch/inc/crnlib.h"
#include "../xtexture_rsc_descriptor.h"
#include "../../dependencies/xresource_pipelineV2/dependencies/xproperty/source/examples/xcore_sprop_serializer/xcore_sprop_serializer.h"
#include "Compressonator.h"
#include "etcpack.h"
#include <iostream>

namespace crnlib
{
    extern std::uint32_t g_number_of_processors;
}
namespace xtexture_compiler {

//---------------------------------------------------------------------------------------------

struct implementation final : instance
{
    xtexture_rsc::descriptor                        m_Descriptor;
    std::unordered_map<std::string, int>            m_BitmapHash;
    std::vector<xcore::bitmap>                      m_Bitmaps;
    xcore::bitmap                                   m_FinalBitmap;
    void*                                           m_pDDSData;
    bool                                            m_HasMixes;
    bool                                            m_bCubeMap;

    //---------------------------------------------------------------------------------------------

    xcore::guid::rcfull<> getResourcePipelineFullGuid() const noexcept override
    {
        return xtexture_rsc::full_guid_v;
    }

    //---------------------------------------------------------------------------------------------

    xcore::err onCompile(void) noexcept override
    {
        //
        // Read the descriptor file...
        //
        {
            xproperty::settings::context    Context{};
            auto                            DescriptorFileName = xcore::string::Fmt("%s/%s/Descriptor.txt", m_ProjectPaths.m_Project.data(), m_InputSrcDescriptorPath.data());

            if ( auto Err = m_Descriptor.Serialize(true, DescriptorFileName.data(), Context); Err)
                return Err;
        }

        //
        // Do a quick validation of the descriptor
        //
        {
            std::vector<std::string> Errors;
            m_Descriptor.Validate(Errors);
            if (Errors.size())
            {
                for (auto& E : Errors)
                {
                    XLOG_CHANNEL_ERROR(m_LogChannel, E.c_str());
                }
                    
                return xerr_failure_s("The descriptor has validation errors");
            }
        }

        //
        // Compile the textures
        //
        displayProgressBar("Processing Textures", 0.0f);
        DumpAllFileNamesIntoHash();
        displayProgressBar("Processing Textures", 20.0f);
        LoopThrowTheHashAndLoadImages();
        displayProgressBar("Processing Textures", 40.0f);
        if (m_HasMixes) CollapseMixes();
        displayProgressBar("Processing Textures", 60.0f);
        RunGenericFilters();
        displayProgressBar("Processing Textures", 100.0f);
        printf("\n");

        //
        // Now we are ready to compress and serialize our texture
        //
        if (false)
        {
            UseCrunch();

            //
            // Serialize textures
            //
            for (auto& T : m_Target)
            {
                if (T.m_bValid)
                {
                    Serialize(T.m_DataPath.data());
                }
            }
        }
        else
        {
            UseCompressonator();
        }

        return {};
    }

    //---------------------------------------------------------------------------------------------

    void RunGenericFilters()
    {
        //
        // Set all the wrapping properly
        //
        {
            xcore::bitmap::wrap_mode Mode;
            if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE)
            {
                Mode = xcore::bitmap::wrap_mode::UV_BOTH_CLAMP_TO_EDGE;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::WRAP)
            {
                Mode = xcore::bitmap::wrap_mode::UV_UCLAMP_VWRAP;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::WRAP && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE)
            {
                Mode = xcore::bitmap::wrap_mode::UV_UWRAP_VCLAMP;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::WRAP && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::WRAP)
            {
                Mode = xcore::bitmap::wrap_mode::UV_BOTH_WRAP;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::MIRROR && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE)
            {
                Mode = xcore::bitmap::wrap_mode::UV_UMIRROR_VCLAMP;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::CLAMP_TO_EDGE && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::MIRROR)
            {
                Mode = xcore::bitmap::wrap_mode::UV_UCLAMP_VMIRROR;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::MIRROR && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::MIRROR)
            {
                Mode = xcore::bitmap::wrap_mode::UV_BOTH_MIRROR;
            }
            else if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::MIRROR && m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::WRAP)
            {
                Mode = xcore::bitmap::wrap_mode::UV_UMIRROR_VWRAP;
            }
            else
            {
                // We do not support any other mode...
                assert(false);
            }

            for (auto& B : m_Bitmaps)
            {
                B.setWrapMode(Mode);
            }
        }

        //
        // If the user told us that he does not care about alpha let us make sure is set to 255
        //
        if (m_Descriptor.m_UsageType == xtexture_rsc::usage_type::COLOR)
        {
            for (auto& B : m_Bitmaps)
            {
                for (auto& E : B.getMip<xcore::icolor>(0))
                {
                    E.m_A = 255;
                }
            }
        }

        //
        // If we are compressing base on BC1 force the alpha base on the threshold
        //
        if (m_Descriptor.m_Compression == xtexture_rsc::compression_format::RGBA_BC1_A1)
        {
            for (auto& B : m_Bitmaps)
            {
                xbmp::tools::filters::ForcePunchThroughAlpha(B, m_Descriptor.m_AlphaThreshold);
            }
        }

        //
        // If the user ask us to fill the average color to all the pixels that have alpha...
        // 
        if (m_Descriptor.m_bFillAveColorByAlpha)
        {
            for (auto& B : m_Bitmaps)
            {
                xbmp::tools::filters::FillAvrColorBaseOnAlpha(B, m_Descriptor.m_AlphaThreshold);
            }
        }
    }

    //---------------------------------------------------------------------------------------------

    void LoadTexture( xcore::bitmap& Bitmap, const xcore::cstring& FilePath )
    {
        if( xcore::string::FindStrI( FilePath, ".dds" ) == 0 )
        {
            if( auto Err = xbmp::tools::loader::LoadDSS( Bitmap, FilePath ); Err )
                throw( std::runtime_error( xbmp::tools::getErrorMsg(Err) ) );
        }
        else
        {
            if( auto Err = xbmp::tools::loader::LoadSTDImage( Bitmap, FilePath ); Err )
                throw(std::runtime_error(xbmp::tools::getErrorMsg(Err)));
        }
    }

    //---------------------------------------------------------------------------------------------

    void DumpAllFileNamesIntoHash()
    {
        auto AddTexture = [&]( std::string& Str )
        {
            //
            // Let first clean the path for the textures...
            //
            if ( int i = xcore::string::FindStrI(Str.c_str(), ".lion_project"); i != -1 )
            {
                Str = Str.substr(i + 14);
            }
            else if (int i = xcore::string::FindStrI(Str.c_str(), ".lion_library"); i != -1)
            {
                Str = Str.substr(i + 15);
            }
            
            //
            // Then we can add to the hash
            //
            if (auto P = m_BitmapHash.find(Str); P != m_BitmapHash.end())
            {
                XLOG_CHANNEL_WARNING(m_LogChannel, "You have duplicated file names (%s)", Str.c_str());
            }
            else
            {
                m_BitmapHash[Str] = -1;
            }
        };

        std::visit( [&]<typename T>( T& Input )
        {
            if constexpr( std::is_same_v<T, xtexture_rsc::single_input> )
            {
                m_bCubeMap = false;
                m_HasMixes = false;
                AddTexture(Input.m_FileName);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::single_input_array>)
            {
                m_bCubeMap = false;
                m_HasMixes = false;
                for( auto& E : Input.m_FileNameList )
                {
                    AddTexture(E);
                }
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::mix_source>)
            {
                m_bCubeMap = false;
                m_HasMixes = true;
                for ( auto& E : Input.m_Inputs )
                {
                    AddTexture(E.m_FileName);
                }
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::mix_source_array>)
            {
                m_bCubeMap = false;
                m_HasMixes = true;
                
                for (auto& L : Input.m_MixSourceList)
                    for (auto& E : L.m_Inputs )
                    {
                        AddTexture(E.m_FileName);
                    }
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input>)
            {
                m_bCubeMap = true;
                m_HasMixes = false;
                AddTexture(Input.m_FileNameRight);
                AddTexture(Input.m_FileNameLeft);
                AddTexture(Input.m_FileNameUp);
                AddTexture(Input.m_FileNameDown);
                AddTexture(Input.m_FileNameForward);
                AddTexture(Input.m_FileNameBack);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_array>)
            {
                m_bCubeMap = true;
                m_HasMixes = false;
                for (auto& E : Input.m_CubeInputArray )
                {
                    AddTexture(E.m_FileNameRight);
                    AddTexture(E.m_FileNameLeft);
                    AddTexture(E.m_FileNameUp);
                    AddTexture(E.m_FileNameDown);
                    AddTexture(E.m_FileNameForward);
                    AddTexture(E.m_FileNameBack);
                }
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_mix>)
            {
                m_bCubeMap = true;
                m_HasMixes = true;
                auto HandleMix = [&](xtexture_rsc::mix_source& MixSrc )
                {
                    for (auto& E : MixSrc.m_Inputs)
                        AddTexture(E.m_FileName);
                };

                HandleMix(Input.m_Right);
                HandleMix(Input.m_Left);
                HandleMix(Input.m_Up);
                HandleMix(Input.m_Down);
                HandleMix(Input.m_Forward);
                HandleMix(Input.m_Back);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_mix_array>)
            {
                m_bCubeMap = true;
                m_HasMixes = true;
                auto HandleMix = [&](xtexture_rsc::mix_source& MixSrc )
                {
                    for (auto& E : MixSrc.m_Inputs)
                        AddTexture(E.m_FileName);
                };

                for( auto& E : Input.m_CubeMixArray )
                {
                    HandleMix(E.m_Right);
                    HandleMix(E.m_Left);
                    HandleMix(E.m_Up);
                    HandleMix(E.m_Down);
                    HandleMix(E.m_Forward);
                    HandleMix(E.m_Back);
                }
            }
            else
            {
                assert(false);
            }

        }, m_Descriptor.m_InputVariant );
    }

    //---------------------------------------------------------------------------------------------
    // TODO: Must be able to handle HDR images at some point...
    // We normalize the bitmaps so we can treat them all the same way
    void NormalizeBitmap( xcore::bitmap& Bitmap )
    {
        if( Bitmap.getFormat() == xcore::bitmap::format::R8G8B8A8 ) return;

        if( Bitmap.getFormat() != xcore::bitmap::format::R8G8B8 && Bitmap.getFormat() != xcore::bitmap::format::R5G6B5 )
            throw(std::runtime_error("Source texture has a strange format"));

        auto        ColorFmt        = xcore::color::format{ static_cast<xcore::color::format::type>(Bitmap.getFormat()) };
        auto&       Descriptor      = ColorFmt.getDescriptor();
        const auto  BypePerPixel    = Descriptor.m_TB / 8;
        std::byte*  pBitmapData     = Bitmap.getMip<std::byte>(0).data();
        auto        Data            = std::make_unique<xcore::icolor[]>( 1 + Bitmap.getHeight() * Bitmap.getWidth() );
        auto        pData           = &Data[1];

        Data[0].m_Value = 0;

        for( int y=0, end_y = Bitmap.getHeight(); y < end_y; ++y )
        for( int x=0, end_x = Bitmap.getWidth();  x < end_x; ++x )
        {
            const std::uint32_t D = *reinterpret_cast<const std::uint32_t*>(pBitmapData);
            *pData = xcore::icolor{ D, ColorFmt };

            pData++;
            pBitmapData += BypePerPixel;
        }   

        //
        // Setup the bitmap again
        //
        Bitmap.setup
        ( Bitmap.getWidth()
        , Bitmap.getHeight()
        , xcore::bitmap::format::R8G8B8A8
        , sizeof(xcore::icolor) * (Bitmap.getHeight() * Bitmap.getWidth())
        , { reinterpret_cast<std::byte*>(Data.release()), sizeof(xcore::icolor) * (1 + Bitmap.getHeight() * Bitmap.getWidth()) }
        , true
        , 1
        , 1
        );
    }

    //---------------------------------------------------------------------------------------------

    void LoopThrowTheHashAndLoadImages()
    {
        m_Bitmaps.resize(m_BitmapHash.size());

        int Index  = 0;
        int Width  = 0;
        int Height = 0;
        for (auto& [FileName, BitmapIndex] : m_BitmapHash)
        {
            BitmapIndex = Index++;
            LoadTexture(m_Bitmaps[BitmapIndex], xcore::string::Fmt("%s/%s", m_ProjectPaths.m_Project.data(), FileName.data()));

            if (BitmapIndex == 0 )
            {
                Width  = m_Bitmaps[BitmapIndex].getWidth();
                Height = m_Bitmaps[BitmapIndex].getHeight();
            }
            else
            {
                if (Width != m_Bitmaps[BitmapIndex].getWidth() || Height != m_Bitmaps[BitmapIndex].getHeight())
                    throw std::runtime_error(std::format("Input Texture: [{}] All textures should be the same size", FileName));
            }

            //
            // Make sure to convert all textures to the same format... RGBA
            //
            NormalizeBitmap(m_Bitmaps[BitmapIndex]);
        }
    }

    //---------------------------------------------------------------------------------------------

    void CollapseMixes()
    {
        std::vector<xcore::bitmap>  MixedBitmaps;

        auto HandleMix = [&](xcore::bitmap& Dest, const xtexture_rsc::mix_source& MixSrc)
        {
            Dest.CreateBitmap( m_Bitmaps[0].getWidth(), m_Bitmaps[0].getHeight() );

            for (auto& E : MixSrc.m_Inputs)
            {
                xcore::bitmap& Src = m_Bitmaps[ m_BitmapHash[E.m_FileName] ];

                for (int y = 0, end_y = Src.getHeight(); y < end_y; ++y)
                    for (int x = 0, end_x = Src.getWidth(); x < end_x; ++x)
                    {
                        xcore::icolor D = Dest.getMip<xcore::icolor>(0)[y * Src.getWidth() + x];
                        xcore::icolor S = Src.getMip<xcore::icolor>(0) [y * Src.getWidth() + x];

                        if (E.m_CopyFrom == xtexture_rsc::compositing::A)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_A;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_A;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_A;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::A) D.m_A = S.m_A;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGB)  { D.m_R = S.m_A; D.m_G = S.m_A; D.m_B = S.m_A; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_A; D.m_G = S.m_A; D.m_B = S.m_A; D.m_A = S.m_A; }
                        }
                        else if (E.m_CopyFrom == xtexture_rsc::compositing::R)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::A) D.m_A = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGB) {  D.m_R = S.m_R; D.m_G = S.m_R; D.m_B = S.m_R; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_R; D.m_G = S.m_R; D.m_B = S.m_R; D.m_A = S.m_R; }
                        }
                        else if (E.m_CopyFrom == xtexture_rsc::compositing::G)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::A) D.m_A = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGB) { D.m_R = S.m_G; D.m_G = S.m_G; D.m_B = S.m_G; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_G; D.m_G = S.m_G; D.m_B = S.m_G; D.m_A = S.m_G; }
                        }
                        else if (E.m_CopyFrom == xtexture_rsc::compositing::B)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_B;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_B;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_B;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::A) D.m_A = S.m_B;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGB)  { D.m_R = S.m_B; D.m_G = S.m_B; D.m_B = S.m_B; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_B; D.m_G = S.m_B; D.m_B = S.m_B; D.m_A = S.m_B; }
                        }
                        else if (E.m_CopyFrom == xtexture_rsc::compositing::RGBA)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::RGB)  { D.m_R = S.m_R; D.m_G = S.m_G; D.m_B = S.m_B; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_R; D.m_G = S.m_G; D.m_B = S.m_B; D.m_A = S.m_A; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_B;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::A) D.m_A = S.m_A;
                            else assert(false);
                        }
                        else if (E.m_CopyFrom == xtexture_rsc::compositing::RGB)
                        {
                                 if (E.m_CopyTo == xtexture_rsc::compositing::RGBA) { D.m_R = S.m_R; D.m_G = S.m_G; D.m_B = S.m_B; D.m_A = 0xff; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::RGB)  { D.m_R = S.m_R; D.m_G = S.m_G; D.m_B = S.m_B; }
                            else if (E.m_CopyTo == xtexture_rsc::compositing::R) D.m_R = S.m_R;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::G) D.m_G = S.m_G;
                            else if (E.m_CopyTo == xtexture_rsc::compositing::B) D.m_B = S.m_B;
                            else throw(std::runtime_error("It does not have alpha information to copy from"));
                        }
                        else
                        {
                            assert(false);
                        }

                        //
                        // Set the destination pixel
                        //
                        Dest.getMip<xcore::icolor>(0)[y * Src.getWidth() + x] = D;
                    }
            }
        };

        std::visit([&]<typename T>(T & Input)
        {
            if constexpr (std::is_same_v<T, xtexture_rsc::single_input>)
            {
                // Nothing to do...
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::single_input_array>)
            {
                // Nothing to do...
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::mix_source>)
            {
                MixedBitmaps.resize(1);
                HandleMix(MixedBitmaps[0], Input);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::mix_source_array>)
            {
                MixedBitmaps.resize(Input.m_MixSourceList.size());
                int Index = 0;
                for (auto& A : Input.m_MixSourceList)
                    HandleMix(MixedBitmaps[Index++], A);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input>)
            {
                // Nothing to do...
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_array>)
            {
                // Nothing to do...
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_mix>)
            {
                MixedBitmaps.resize(6);
                HandleMix(MixedBitmaps[0], Input.m_Right);
                HandleMix(MixedBitmaps[1], Input.m_Left);
                HandleMix(MixedBitmaps[2], Input.m_Up);
                HandleMix(MixedBitmaps[3], Input.m_Down);
                HandleMix(MixedBitmaps[4], Input.m_Forward);
                HandleMix(MixedBitmaps[5], Input.m_Back);
            }
            else if constexpr (std::is_same_v<T, xtexture_rsc::cube_input_mix_array>)
            {
                MixedBitmaps.resize(6 * Input.m_CubeMixArray.size() );
                int Index = 0;
                for (auto& E : Input.m_CubeMixArray)
                {
                    HandleMix(MixedBitmaps[Index++], E.m_Right);
                    HandleMix(MixedBitmaps[Index++], E.m_Left);
                    HandleMix(MixedBitmaps[Index++], E.m_Up);
                    HandleMix(MixedBitmaps[Index++], E.m_Down);
                    HandleMix(MixedBitmaps[Index++], E.m_Forward);
                    HandleMix(MixedBitmaps[Index++], E.m_Back);
                }
            }
            else
            {
                assert(false);
            }

        }, m_Descriptor.m_InputVariant);

        // We are overriding all the bitmaps with the final bitmaps
        m_Bitmaps = std::move(MixedBitmaps);
    }

    //---------------------------------------------------------------------------------------------

    crn_format MatchForceCompressionFormat()
    {
        constexpr static auto Table = []() consteval ->auto
        {
            std::array< crn_format, static_cast<std::int32_t>(xtexture_rsc::compression_format::count_v) > Array = { crn_format::cCRNFmtInvalid };

            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_BC1)]     = crn_format::cCRNFmtDXT1;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC1_A1)] = crn_format::cCRNFmtDXT1;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC3_A8)] = crn_format::cCRNFmtDXT3;

            return Array;
        }();

        return Table[static_cast<std::int32_t>(m_Descriptor.m_Compression) ];
    }

    //---------------------------------------------------------------------------------------------

    void UseCrunch( void )
    {
        // Crunch the image data and return a pointer to the crunched result array
        crn_comp_params Params;
        Params.clear();

        Params.m_alpha_component = ( m_Descriptor.m_Compression == xtexture_rsc::compression_format::RGBA_BC1_A1 
                                  || m_Descriptor.m_Compression == xtexture_rsc::compression_format::RGBA_BC3_A8);
        Params.m_format          = MatchForceCompressionFormat();

        if ( Params.m_format == crn_format::cCRNFmtInvalid )
            throw(std::runtime_error("The compiler can not handle the specified compression format"));

        float Scalar;
             if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::O0) Scalar = 0.0f;
        else if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::O1) Scalar = m_Descriptor.m_Quality;
        else if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::Oz) Scalar = 1.0f;
        else Scalar = m_Descriptor.m_Quality;

             if (Scalar == 0.0f)  Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualitySuperFast;
        else if (Scalar <= 0.3f)  Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualityFast;
        else if (Scalar <= 0.7f)  Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualityNormal;
        else if (Scalar <= 0.8f)  Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualityBetter;
        else if (Scalar <= 0.9f)  Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualityUber;
        else Params.m_dxt_quality = crn_dxt_quality::cCRNDXTQualityTotal;


        Params.m_width                  = m_Bitmaps[0].getWidth();
        Params.m_height                 = m_Bitmaps[0].getHeight();
        Params.m_file_type              = crn_file_type::cCRNFileTypeDDS;
        Params.m_num_helper_threads     = std::min( static_cast<std::uint32_t>(cCRNMaxHelperThreads), Scalar >= 0.5f ? std::thread::hardware_concurrency() : std::thread::hardware_concurrency()/2 );
        crnlib::g_number_of_processors  = Params.m_num_helper_threads;


        Params.m_faces = static_cast<std::uint32_t>(m_Bitmaps.size());
        for (int i = 0; i < m_Bitmaps.size(); ++i)
        {
            Params.m_pImages[i][0] = m_Bitmaps[i].getMip<std::uint32_t>(0).data();
        }

        // If we are doing colors we can use perceptual compression otherwise we can not
        if (m_Descriptor.m_bSRGB && (m_Descriptor.m_UsageType == xtexture_rsc::usage_type::COLOR || m_Descriptor.m_UsageType == xtexture_rsc::usage_type::COLOR_AND_ALPHA) )
            Params.m_flags |= crn_comp_flags::cCRNCompFlagPerceptual;
        else
            Params.m_flags &= ~crn_comp_flags::cCRNCompFlagPerceptual;

        if( m_Descriptor.m_UsageType == xtexture_rsc::usage_type::INTENSITY ) 
            Params.m_flags |= crn_comp_flags::cCRNCompFlagGrayscaleSampling;
        else 
            Params.m_flags &= ~crn_comp_flags::cCRNCompFlagGrayscaleSampling;

             if (m_DebugType == debug_type::D0) Params.m_flags &= ~crn_comp_flags::cCRNCompFlagDebugging;
        else if (m_DebugType == debug_type::D1) Params.m_flags |= crn_comp_flags::cCRNCompFlagDebugging;
        else if (m_DebugType == debug_type::Dz) Params.m_flags |= crn_comp_flags::cCRNCompFlagDebugging;
        else Params.m_flags &= ~crn_comp_flags::cCRNCompFlagDebugging;

        // Check to make sure everything is OK
        if( Params.check() == false )
            throw(std::runtime_error("Parameters for the compressor (crunch) failed."));

        crn_uint32        CompressSize;
        crn_mipmap_params Mipmaps;
        Mipmaps.clear();
        {
            crn_uint32  Actual_quality_level;   // Print stats
            float       Actual_bitrate;

            m_pDDSData = crn_compress
            ( Params
            , Mipmaps
            , CompressSize
            , &Actual_quality_level
            , &Actual_bitrate
            );

            if(m_pDDSData == nullptr )
                throw(std::runtime_error("The compressor (crunch) failed."));
        }

        //
        // Convert from DDS format to xcore::bitmap
        //
        if (auto Err = xbmp::tools::loader::LoadDSS(m_FinalBitmap, { reinterpret_cast<std::byte*>(m_pDDSData), CompressSize }); Err)
            throw(std::runtime_error(xbmp::tools::getErrorMsg(Err)));

        m_FinalBitmap.setColorSpace( (Params.m_flags & crn_comp_flags::cCRNCompFlagPerceptual) ? xcore::bitmap::color_space::SRGB : xcore::bitmap::color_space::LINEAR );
        m_FinalBitmap.setCubemap(m_bCubeMap);
    }

    //---------------------------------------------------------------------------------------------

    void UseCompressonator(void)
    {
        constexpr static auto TextureConversionTable = []() consteval ->auto
        {
            std::array< CMP_FORMAT, static_cast<std::int32_t>(xtexture_rsc::compression_format::count_v) > Array = { CMP_FORMAT::CMP_FORMAT_Unknown };

            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_BC1)]             = CMP_FORMAT::CMP_FORMAT_BC1;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC1_A1)]         = CMP_FORMAT::CMP_FORMAT_BC1;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC3_A8)]         = CMP_FORMAT::CMP_FORMAT_BC3;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::R_BC4)]               = CMP_FORMAT::CMP_FORMAT_BC4;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RG_BC5)]              = CMP_FORMAT::CMP_FORMAT_BC5;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_UHDR_BC6)]        = CMP_FORMAT::CMP_FORMAT_BC6H;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SHDR_BC6)]        = CMP_FORMAT::CMP_FORMAT_BC6H_SF;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC7)]            = CMP_FORMAT::CMP_FORMAT_BC7;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SUPER_COMPRESS)]  = CMP_FORMAT::CMP_FORMAT_BASIS;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_SUPER_COMPRESS)] = CMP_FORMAT::CMP_FORMAT_BASIS;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_UNCOMPRESSED)]   = CMP_FORMAT::CMP_FORMAT_RGBA_8888;

            return Array;
        }();

        constexpr static auto ChannelConversionTable = []() consteval ->auto
        {
            std::array< CMP_BYTE, static_cast<std::int32_t>(xtexture_rsc::compression_format::count_v) > Array = { 0 };

            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_BC1)]             = 3;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC1_A1)]         = 4;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC3_A8)]         = 4;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::R_BC4)]               = 1;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RG_BC5)]              = 2;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_UHDR_BC6)]        = 3;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SHDR_BC6)]        = 3;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC7)]            = 4;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SUPER_COMPRESS)]  = 3;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_SUPER_COMPRESS)] = 4;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_UNCOMPRESSED)]   = 4;

            return Array;
        }();

        constexpr static auto DataTypeConversionTable = []() consteval ->auto
        {
            std::array< CMP_TextureDataType, static_cast<std::int32_t>(xtexture_rsc::compression_format::count_v) > Array = { CMP_TextureDataType::TDT_ARGB };

            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_BC1)]             = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC1_A1)]         = CMP_TextureDataType::TDT_ARGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC3_A8)]         = CMP_TextureDataType::TDT_ARGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::R_BC4)]               = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RG_BC5)]              = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_UHDR_BC6)]        = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SHDR_BC6)]        = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC7)]            = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_SUPER_COMPRESS)]  = CMP_TextureDataType::TDT_XRGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_SUPER_COMPRESS)] = CMP_TextureDataType::TDT_ARGB;
            Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_UNCOMPRESSED)]   = CMP_TextureDataType::TDT_ARGB;

            return Array;
        }();

        //
        // Initialize the framework
        // 
        CMP_InitFramework();

        //
        // Setup the mip tables
        //
        constexpr auto max_mip_levels_v = 20;
        CMP_MipLevel** MipLevelTable = new CMP_MipLevel*[max_mip_levels_v] {};
        for (int i = 0; i < max_mip_levels_v; ++i) MipLevelTable[i] = new CMP_MipLevel();

        //
        // Set up our texture 
        //
        CMP_MipSet MipSet;
        memset(&MipSet, 0, sizeof(CMP_MipSet));

        MipSet.m_nWidth             = static_cast<CMP_INT>(m_Bitmaps[0].getWidth());
        MipSet.m_nHeight            = static_cast<CMP_INT>(m_Bitmaps[0].getHeight());
        MipSet.m_nDepth             = 1;
        MipSet.m_format             = CMP_FORMAT::CMP_FORMAT_ARGB_8888;
        MipSet.m_ChannelFormat      = CF_8bit;
        MipSet.m_TextureDataType    = m_Descriptor.m_UsageType == xtexture_rsc::usage_type::TANGENT_NORMAL ? TextureDataType::TDT_NORMAL_MAP : DataTypeConversionTable[static_cast<std::int32_t>(m_Descriptor.m_Compression)];
        MipSet.m_TextureType        = m_bCubeMap ? TT_CubeMap : TT_2D;
        MipSet.m_Flags              = 0;
        MipSet.m_CubeFaceMask       = m_bCubeMap ? 0x3f : 0; // MS_CF_All
        MipSet.m_dwFourCC           = 0;
        MipSet.m_dwFourCC2          = 0;
        MipSet.m_nMaxMipLevels      = static_cast<CMP_INT>(max_mip_levels_v);
        MipSet.m_nMipLevels         = 1;
        MipSet.m_transcodeFormat    = CMP_FORMAT::CMP_FORMAT_Unknown;
        MipSet.m_compressed         = false;
        MipSet.m_isDeCompressed     = CMP_FORMAT::CMP_FORMAT_Unknown;
        MipSet.m_swizzle            = false;
        MipSet.m_nBlockWidth        = 0;
        MipSet.m_nBlockHeight       = 0;
        MipSet.m_nBlockDepth        = 0;
        MipSet.m_nChannels          = ChannelConversionTable[static_cast<std::int32_t>(m_Descriptor.m_Compression)];
        MipSet.dwWidth              = MipSet.m_nWidth;
        MipSet.dwHeight             = MipSet.m_nHeight;
        MipSet.dwDataSize           = static_cast<CMP_DWORD>(m_Bitmaps[0].getFaceSize());
        MipSet.pData                = new CMP_BYTE[MipSet.dwDataSize];

        memcpy(MipSet.pData, reinterpret_cast<CMP_BYTE*>(m_Bitmaps[0].getMip<std::byte>(0).data()), MipSet.dwDataSize);

        MipLevelTable[0]->m_nWidth       = MipSet.dwWidth;
        MipLevelTable[0]->m_nHeight      = MipSet.dwHeight;
        MipLevelTable[0]->m_dwLinearSize = MipSet.dwDataSize;
        MipLevelTable[0]->m_pbData       = MipSet.pData;

        MipSet.m_pMipLevelTable    = MipLevelTable;

        //
        // Generate the mipmaps
        //
        if( m_Descriptor.m_bGenerateMips )
        {
            constexpr auto      inv_gamma_v = 1.0f / 2.2f;
            CMP_CFilterParams   CFilterParam  = {};

            switch (m_Descriptor.m_MipmapFilter)
            {
            case xtexture_rsc::mipmap_filter::NONE:      CFilterParam.nFilterType = CMP_D3DX_FILTER_NONE; break;
            case xtexture_rsc::mipmap_filter::POINT:     CFilterParam.nFilterType = CMP_D3DX_FILTER_POINT; break;
            case xtexture_rsc::mipmap_filter::LINEAR:    CFilterParam.nFilterType = CMP_D3DX_FILTER_LINEAR; break;
            case xtexture_rsc::mipmap_filter::TRIANGLE:  CFilterParam.nFilterType = CMP_D3DX_FILTER_TRIANGLE; break;
            case xtexture_rsc::mipmap_filter::BOX:       CFilterParam.nFilterType = CMP_D3DX_FILTER_BOX; break;
            }

            CFilterParam.dwMipFilterOptions = 0;
            if (m_Descriptor.m_UWrap == xtexture_rsc::wrap_type::MIRROR || m_Descriptor.m_VWrap == xtexture_rsc::wrap_type::MIRROR ) 
                CFilterParam.dwMipFilterOptions |= CMP_D3DX_FILTER_MIRROR;

            // This seems to destroy the color information for alpha textures with BC1_A1 compression
            if (m_Descriptor.m_bSRGB )
                CFilterParam.dwMipFilterOptions |= CMP_D3DX_FILTER_SRGB;

            CFilterParam.nMinSize           = CMP_CalcMaxMipLevel(MipSet.m_nHeight, MipSet.m_nWidth, false);
            CFilterParam.fGammaCorrection   = 1;//m_Descriptor.m_bSRGB ? inv_gamma_v : 1.0f;
            CFilterParam.useSRGB            = m_Descriptor.m_bSRGB;

            CMP_GenerateMIPLevelsEx(&MipSet, &CFilterParam);
        }

        //
        // Set the compression type
        //
        CMP_MipSet MipSetCompressed;
        {
            KernelOptions KernelOps;

            float Scalar;
                 if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::O0) Scalar = 0.0f;
            else if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::O1) Scalar = m_Descriptor.m_Quality;
            else if (m_OptimizationType == xresource_pipeline::compiler::base::optimization_type::Oz) Scalar = 1.0f;
            else Scalar = m_Descriptor.m_Quality;

            memset(&KernelOps, 0, sizeof(KernelOps));
            KernelOps.format        = TextureConversionTable[static_cast<std::int32_t>(m_Descriptor.m_Compression)];
            KernelOps.fquality      = std::clamp(Scalar, 0.05f, 1.0f);
            KernelOps.threads       = 0;
            KernelOps.getPerfStats  = true;

                 if (m_DebugType == debug_type::D0) KernelOps.getDeviceInfo = false;
            else if (m_DebugType == debug_type::D1) KernelOps.getDeviceInfo = true;
            else if (m_DebugType == debug_type::Dz) KernelOps.getDeviceInfo = true;

            //
            // handle gamma textures
            //
            if ( m_Descriptor.m_bSRGB )
            {

                if( m_Descriptor.m_Compression  == xtexture_rsc::compression_format::RGBA_BC1_A1 )
                {
                    KernelOps.bc15.useAlphaThreshold = true;
                    KernelOps.bc15.alphaThreshold    = m_Descriptor.m_AlphaThreshold;
                }

                // Set channel weights for better perceptual compression
                KernelOps.bc15.useChannelWeights = true;
                KernelOps.bc15.channelWeights[0] = 0.3086f; // Red
                KernelOps.bc15.channelWeights[1] = 0.6094f; // Green
                KernelOps.bc15.channelWeights[2] = 0.0820f; // Blue
            }


            KernelOps.useSRGBFrames = m_Descriptor.m_bSRGB;
            

            //
            // Compress the texture
            //
            if(m_Descriptor.m_Compression == xtexture_rsc::compression_format::RGBA_UNCOMPRESSED)
            {
                MipSetCompressed = MipSet;
                memset(&MipSet, 0, sizeof(CMP_MipSet));
            }
            else
            {
                static int s_ActualProgress;
                static int s_nMipMaps;
                static int s_Updates;
                s_nMipMaps       = MipSet.m_nMipLevels;
                s_ActualProgress = 0;
                s_Updates        = 0;
                memset(&MipSetCompressed, 0, sizeof(CMP_MipSet));
                CMP_ERROR cmp_status = CMP_ProcessTexture(&MipSet, &MipSetCompressed, KernelOps, [](CMP_FLOAT fProgress, CMP_DWORD_PTR, CMP_DWORD_PTR) ->bool
                {
                    if (fProgress >= 100) 
                    {
                        s_ActualProgress++;
                        s_Updates=0;
                    }
                    else
                    {
                        s_Updates++;
                    }
                    
                    if ((s_Updates%20)==0)
                    {
                        float per =  (s_ActualProgress / static_cast<float>(s_nMipMaps));
                        displayProgressBar( "Compression", per );
                    }

                    return CMP_OK;
                });

                // Make sure that the display finish at 100%
                displayProgressBar("Compression", 100);
                printf("\n");
                if (cmp_status != CMP_OK)
                    throw(std::runtime_error("Unable to compress the texture"));

                CMP_FreeMipSet(&MipSet);
            }
        }

        //
        // Serialize texture
        // 
        {
            assert( m_Target[static_cast<int>(xcore::target::platform::WINDOWS)].m_bValid );
            auto& Path = m_Target[static_cast<int>(xcore::target::platform::WINDOWS)].m_DataPath;

            // Force the DDS file to serialize wih the DX10 Header (Only for gamma textures)
            if (m_Descriptor.m_bSRGB) MipSetCompressed.m_dwFourCC = CMP_MAKEFOURCC('D', 'X', '1', '0');
            auto filename = xcore::string::Fmt("%s.dds", Path.data());

            CMP_ERROR cmp_status = CMP_SaveTexture(filename, &MipSetCompressed);
            if (cmp_status != CMP_OK)
                throw(std::runtime_error("Unable to export the texture"));

            //  
            // HACK: Hack to convert to sRGB since compressonator does not support it...
            //
            if (m_Descriptor.m_bSRGB)
            {
                constexpr static auto ToSRGB = []() consteval ->auto
                {
                    std::array< std::uint32_t, static_cast<std::int32_t>(xtexture_rsc::compression_format::count_v) > Array = { 0 };
                    Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGB_BC1)]             = 72;   // DDSFile::DXGIFormat::BC1_UNorm_SRGB
                    Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC1_A1)]         = 72;   // DDSFile::DXGIFormat::BC1_UNorm_SRGB
                    Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC3_A8)]         = 78;   // DDSFile::DXGIFormat::BC3_UNorm_SRGB
                    Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_BC7)]            = 99;   // DDSFile::DXGIFormat::BC7_UNorm_SRGB
                    Array[static_cast<std::int32_t>(xtexture_rsc::compression_format::RGBA_UNCOMPRESSED)]   = 29;   // DDSFile::DXGIFormat::R8G8B8A8_UNorm_SRGB
                    return Array;
                }();

                if(auto NewFormat = ToSRGB[static_cast<std::int32_t>(m_Descriptor.m_Compression)]; NewFormat)
                {
                    // IF it suposed to have gamma make sure to convert it to sRGB
                    FILE* fp = fopen(filename, "r+b" );
                    if( fp == nullptr )
                        throw(std::runtime_error("Unable to reload the dds..."));

                    // in the DDS file offset to dxgiFormat part of the DX10 header
                    if (-1 == fseek(fp, 128, SEEK_SET))        
                        throw(std::runtime_error("Unable to reload the dds..."));

                    if (-1 == fwrite(&NewFormat, 4, 1, fp))
                        throw(std::runtime_error("Unable to reload the dds..."));

                    fclose(fp);
                }
            }
        }

        CMP_FreeMipSet(&MipSetCompressed);
    }

    //---------------------------------------------------------------------------------------------

    virtual ~implementation()
    {
        if (m_pDDSData != nullptr)
            crn_free_block(m_pDDSData);
    }

    //---------------------------------------------------------------------------------------------

    void Serialize(const std::string_view FilePath)
    {
        if (auto Err = m_FinalBitmap.SerializeSave(xcore::string::To<wchar_t>(FilePath), false); Err)
            throw(std::runtime_error(Err.getCode().m_pString));

        //
        // Verify this can be loaded... this can be put into a debug mode later..
        //
        {
            xcore::bitmap* pTemp;
            if (auto Err = m_FinalBitmap.SerializeLoad( pTemp, xcore::string::To<wchar_t>(FilePath)); Err)
                throw(std::runtime_error(Err.getCode().m_pString));

            //
            // OK Time to let things go...
            //
            xcore::memory::AlignedFree(pTemp);
        }
    }
};

//---------------------------------------------------------------------------------------------

std::unique_ptr<instance> instance::Create( void )
{
    return std::make_unique<implementation>();
}

//---------------------------------------------------------------------------------------------
// end of namespace xtexture_compiler
//---------------------------------------------------------------------------------------------
} 