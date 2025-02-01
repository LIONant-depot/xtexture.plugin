#ifndef XTEXTURE_RSC_DESCRIPTOR_H
#define XTEXTURE_RSC_DESCRIPTOR_H
#pragma once

namespace xtexture_rsc
{
    static constexpr xcore::guid::rcfull<> full_guid_v
    { .m_Type = xcore::guid::rctype<>           { "resource.pipeline", "plugin" }
    , .m_Instance = xcore::guid::rcinstance<>   { "xtexture" }
    };

    enum class compression_format : std::uint8_t
    { RGBA_UNCOMPRESSED     // 32bpp
    , RGB_BC1               //  4bpp
    , RGBA_BC1_A1           //  4bpp
    , RGBA_BC3_A8           //  8bpp
    , R_BC4                 //  8bpp
    , RG_BC5                //  8bpp 
    , RGB_SHDR_BC6          //  8bpp
    , RGB_UHDR_BC6          //  8bpp
    , RGBA_BC7              //  8bpp 
    , RGB_SUPER_COMPRESS
    , RGBA_SUPER_COMPRESS
    , count_v
    };

    static constexpr char image_filter_v[] = "Images\0 *.png; *.tga; *.dds; *.jpg; *jpeg\0Any Thing\0 *.*\0";

    static constexpr auto compression_format_uncompressed_help_v =
    "32bits per-pixel, (8 bit per-element). Standard format with alpha support\n\n"
    "This format retains all the original data from a standard image and is used "
    "when no lost of information is required. Such is the case for debugging or for detailed gradiant";

    static constexpr auto compression_format_bc1_help_v =
    "4bits per-pixel, block compression format also known as DXT1. It has no alpha. \n\n"
    "It is the most used format for sRGB color images. This format compresses the image"
    "with a block of 4x4 pixels and achieves a (6:1 compression ratio).";

    static constexpr auto compression_format_bc1_a_help_v =
    "4bits per-pixel, block compression format also known as DXT1. It version supports 1bit alpha also known a punch-throw. \n\n"
    "It is the most used format for sRGB color images such sprites or fonts. This format compresses the image"
    "with a block of 4x4 pixels and achieves a (6:1 compression ratio).\n\n"
    "NOTE: That for the pixels that are going to be transparent the color will be set to black. "
    "This seems to be due to the BC1 compression format.";

    static constexpr auto compression_format_bc3_help_v =
    "8bits per-pixel, block compression format also known as DXT5. This format is typically used for alpha textures.\n\n"
    "It is the most used format for sRGB color images such particle effects and UI element. This format compresses the image"
    "with a block of 4x4 pixels and achieves a (4:1 compression ratio).";

    static constexpr auto compression_format_bc7_help_v =
    "8bits per-pixel or less, block compression format this is the newest version. This format is typically used for alpha textures.\n\n"
    "This format offers higher quality and greater flexibility than BC1 and BC3. It is used primarily for high-quality "
    "textures in modern graphic applications. (4:1 compression ratio or better).";

    static constexpr auto compression_format_v = std::array
    { xproperty::settings::enum_item("RGBA_UNCOMPRESSED",   compression_format::RGBA_UNCOMPRESSED, compression_format_uncompressed_help_v)
    , xproperty::settings::enum_item("RGB_BC1",             compression_format::RGB_BC1, compression_format_bc1_help_v)
    , xproperty::settings::enum_item("RGBA_BC1_A1",         compression_format::RGBA_BC1_A1, compression_format_bc1_a_help_v)
    , xproperty::settings::enum_item("RGBA_BC3_A8",         compression_format::RGBA_BC3_A8, compression_format_bc3_help_v)
    , xproperty::settings::enum_item("R_BC4",               compression_format::R_BC4)
    , xproperty::settings::enum_item("RG_BC5",              compression_format::RG_BC5)
    , xproperty::settings::enum_item("RGB_SHDR_BC6",        compression_format::RGB_SHDR_BC6)
    , xproperty::settings::enum_item("RGB_UHDR_BC6",        compression_format::RGB_UHDR_BC6)
    , xproperty::settings::enum_item("RGBA_BC7",            compression_format::RGBA_BC7, compression_format_bc7_help_v)
    , xproperty::settings::enum_item("RGB_SUPER_COMPRESS",  compression_format::RGB_SUPER_COMPRESS)
    , xproperty::settings::enum_item("RGBA_SUPER_COMPRESS", compression_format::RGBA_SUPER_COMPRESS)
    };

    enum class compression_format_hdr_color : std::uint8_t
    { RGBA_UNCOMPRESSED     = compression_format::RGBA_UNCOMPRESSED
    , RGB_SHDR_BC6          = compression_format::RGB_SHDR_BC6
    , RGB_UHDR_BC6          = compression_format::RGB_UHDR_BC6
    };

    static constexpr auto compression_format_hdr_color_v = std::array
    { xproperty::settings::enum_item("RGBA_UNCOMPRESSED",   compression_format_hdr_color::RGBA_UNCOMPRESSED, compression_format_uncompressed_help_v)
    , xproperty::settings::enum_item("RGB_SHDR_BC6",        compression_format_hdr_color::RGB_SHDR_BC6)
    , xproperty::settings::enum_item("RGB_UHDR_BC6",        compression_format_hdr_color::RGB_UHDR_BC6)
    };

    enum class compression_format_color : std::uint8_t
    { RGBA_UNCOMPRESSED     = compression_format::RGBA_UNCOMPRESSED
    , RGB_BC1               = compression_format::RGB_BC1
    , RGB_BC7               = compression_format::RGBA_BC7
    , RGB_SUPER_COMPRESS    = compression_format::RGB_SUPER_COMPRESS
    };

    static constexpr auto compression_format_color_v = std::array
    { xproperty::settings::enum_item("RGBA_UNCOMPRESSED",   compression_format_color::RGBA_UNCOMPRESSED, compression_format_uncompressed_help_v)
    , xproperty::settings::enum_item("RGB_BC1",             compression_format_color::RGB_BC1, compression_format_bc1_help_v)
    , xproperty::settings::enum_item("RGB_BC7",             compression_format_color::RGB_BC7, compression_format_bc7_help_v)
    , xproperty::settings::enum_item("RGB_SUPER_COMPRESS",  compression_format_color::RGB_SUPER_COMPRESS)
    };

    enum class compression_format_color_a : std::uint8_t
    { RGBA_UNCOMPRESSED     = compression_format::RGBA_UNCOMPRESSED
    , RGBA_BC1_A1           = compression_format::RGBA_BC1_A1
    , RGBA_BC3_A8           = compression_format::RGBA_BC3_A8
    , RGBA_BC7              = compression_format::RGBA_BC7
    , RGBA_SUPER_COMPRESS   = compression_format::RGBA_SUPER_COMPRESS
    };

    static constexpr auto compression_format_color_a_v = std::array
    { xproperty::settings::enum_item("RGBA_UNCOMPRESSED",   compression_format_color_a::RGBA_UNCOMPRESSED, compression_format_uncompressed_help_v)
    , xproperty::settings::enum_item("RGBA_BC1_A1",         compression_format_color_a::RGBA_BC1_A1, compression_format_bc1_a_help_v)
    , xproperty::settings::enum_item("RGBA_BC3_A8",         compression_format_color_a::RGBA_BC3_A8, compression_format_bc3_help_v)
    , xproperty::settings::enum_item("RGBA_BC7",            compression_format_color_a::RGBA_BC7, compression_format_bc7_help_v)
    , xproperty::settings::enum_item("RGBA_SUPER_COMPRESS", compression_format_color_a::RGBA_SUPER_COMPRESS)
    };

    enum class compression_format_tangent_normal : std::uint8_t
    { RGBA_UNCOMPRESSED     = compression_format::RGBA_UNCOMPRESSED
    , RG_BC5                = compression_format::RG_BC5
    , RGBA_BC3_A8           = compression_format::RGBA_BC3_A8
    , RGBA_BC7              = compression_format::RGBA_BC7
    , RGBA_SUPER_COMPRESS   = compression_format::RGBA_SUPER_COMPRESS
    };

    static constexpr auto compression_format_tangent_normal_v = std::array
    { xproperty::settings::enum_item("RGBA_UNCOMPRESSED",   compression_format_tangent_normal::RGBA_UNCOMPRESSED, compression_format_uncompressed_help_v)
    , xproperty::settings::enum_item("RG_BC5",              compression_format_tangent_normal::RG_BC5, "When compressing a normal map only 2 components of the normal will in fact be saved."
                                                                                                       "To decompress from this format you will need to do the following in your shader:\n"
                                                                                                       "vec3 Normal;\n"
                                                                                                       "Normal.rg = texture(uSamplerNormal, In.UV).gr;\n"
                                                                                                       "Normal.xy = Normal.rg * 2.0 - 1.0;\n"
                                                                                                       "Normal.z  = sqrt(1.0 - min( 1, dot(Normal.xy, Normal.xy)));")
    , xproperty::settings::enum_item("RGBA_BC3",            compression_format_tangent_normal::RGBA_BC3_A8, "When compressing a normal map only 2 components of the normal will in fact be saved."
                                                                                                            "Even if BC3 in back has 4 channels, due to the type of compression is always preferable to do 2."
                                                                                                            "to decompress this format in your shader you will need to do the following:\n"
                                                                                                            "vec3 Normal;\n"
                                                                                                            "Normal.rg = texture(uSamplerNormal, In.UV).ag;\n"
                                                                                                            "Normal.xy = Normal.rg * 2.0 - 1.0;\n"
                                                                                                            "Normal.z  = sqrt(1.0 - min(1,dot(Normal.xy, Normal.xy)));")
    , xproperty::settings::enum_item("RGBA_BC7",            compression_format_tangent_normal::RGBA_BC7)
    , xproperty::settings::enum_item("RGBA_SUPER_COMPRESS", compression_format_tangent_normal::RGBA_SUPER_COMPRESS)
    };

    enum class usage_type : std::uint8_t
    { COLOR                 // Color 
    , COLOR_AND_ALPHA       // Color
    , HDR_COLOR             // High Dynamic Range Color 
    , TANGENT_NORMAL        // BC5 compress (R,G,A), B set to black, should be linear
    , INTENSITY             // Things like MASKs (Data expected to be in R) Keep other channels black for better results. Should be linear.
    };

    static constexpr auto type_v = std::array
    { xproperty::settings::enum_item("COLOR",              usage_type::COLOR)
    , xproperty::settings::enum_item("COLOR_AND_ALPHA",    usage_type::COLOR_AND_ALPHA)
    , xproperty::settings::enum_item("HDR_COLOR",          usage_type::HDR_COLOR)
    , xproperty::settings::enum_item("TANGENT_NORMAL",     usage_type::TANGENT_NORMAL)
    , xproperty::settings::enum_item("INTENSITY",          usage_type::INTENSITY)
    };

    enum class compositing : std::uint8_t
    { RGBA
    , RGB
    , A
    , R
    , G
    , B
    };

    static constexpr auto compositing_v = std::array
    { xproperty::settings::enum_item("RGBA",    compositing::RGBA)
    , xproperty::settings::enum_item("RGB",     compositing::RGB)
    , xproperty::settings::enum_item("A",       compositing::A)
    , xproperty::settings::enum_item("R",       compositing::R)
    , xproperty::settings::enum_item("G",       compositing::G)
    , xproperty::settings::enum_item("B",       compositing::B)
    };

    struct mix
    {
        compositing     m_CopyFrom   { compositing::RGBA };
        compositing     m_CopyTo     { compositing::RGBA };
        std::string     m_FileName   {};

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            if (m_FileName.empty()) Errors.push_back("You forgot to enter a FileName");
        }

        XPROPERTY_DEF
        ("Mix", mix
        , obj_member< "Copy From", &mix::m_CopyFrom, member_enum_span<compositing_v> >
        , obj_member< "Copy To",   &mix::m_CopyTo, member_enum_span<compositing_v> >
        , obj_member< "FileName"
            , &mix::m_FileName
            , member_ui<std::string>::file_dialog<image_filter_v, true, 1> 
            >
        )
    };

    struct mix_source
    {
        std::vector<mix>  m_Inputs{ mix{}, mix{} };

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            auto C = Errors.size();
            int  Index = 0;
            std::array<bool, 4> Channels{};

            for( auto& E : m_Inputs) 
            {
                E.Validate(Errors);
                if( Errors.size() != C )
                {
                    for( auto i = C; i != C; ++ i)
                    {
                        Errors[i] = std::format("Mix[{}] {}", Index, Errors[i]);
                    }

                    C = Errors.size();
                }

                switch( E.m_CopyTo )
                {   case compositing::RGBA: Channels[0] = Channels[1] = Channels[2] = Channels[3] = true; break;
                    case compositing::RGB:  Channels[1] = Channels[2] = Channels[3] = true; break;
                    case compositing::A: Channels[0] = true; break;
                    case compositing::R: Channels[1] = true; break;
                    case compositing::G: Channels[2] = true; break;
                    case compositing::B: Channels[3] = true; break;
                }

                Index++;
            }

            if (Channels[1] == false) Errors.push_back("You forgot to fill the red channel");
            if (Channels[2] == false) Errors.push_back("You forgot to fill the gree channel");
            if (Channels[3] == false) Errors.push_back("You forgot to fill the blue channel");
        }

        XPROPERTY_DEF
        ( "MixSource", mix_source
        , obj_member< "Mixes"
            , &mix_source::m_Inputs
            , member_ui_list_size::drag_bar<1, 100>  
            >
        )
    };

    struct mix_source_array
    {
        std::vector<mix_source> m_MixSourceList{{},{}};

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            auto C     = Errors.size();
            int  Index = 0;

            for( auto& E : m_MixSourceList )
            {
                E.Validate(Errors);
                if (Errors.size() != C)
                {
                    for (auto i = C; i != C; ++i)
                    {
                        Errors[i] = std::format("MixSourceList[{}] {}", Index, Errors[i]);
                    }

                    C = Errors.size();
                }
            }
        }

        XPROPERTY_DEF
        ("mix_source_array", mix_source_array
        , obj_member<"MixSource"
            , &mix_source_array::m_MixSourceList
            , member_ui_open<true>
            , member_ui_list_size::drag_bar<1, 100>  
            >
        )
    };

    struct single_input
    {
        std::string m_FileName;

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            if (m_FileName.empty())      Errors.push_back("You forgot to enter a FileName");
        }

        XPROPERTY_DEF
        ("single_input", single_input
        , obj_member<"Filename"
            , &single_input::m_FileName
            , member_ui<std::string>::file_dialog<image_filter_v, true, 1> 
            >
        )
    };

    struct single_input_array
    {
        std::vector<std::string> m_FileNameList{ {},{}, };
         
        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            int Index = 0;
            for( auto& E : m_FileNameList )
            {
                if (E.empty()) Errors.push_back(std::format( "FileNameList[ {} ] You forgot to enter a FileName", Index ) );
                Index++;
            }
        }

        XPROPERTY_DEF
        ("single_input_array", single_input_array
        , obj_member<"Filenames"
            , &single_input_array::m_FileNameList
            , member_ui_open<true>
            , member_ui<std::string>::file_dialog<image_filter_v, true, 1>
            , member_ui_list_size::drag_bar<1, 100> 
            >
        )
    };

    struct cube_input
    {
        std::string m_FileNameLeft;
        std::string m_FileNameBack;
        std::string m_FileNameRight;
        std::string m_FileNameDown;
        std::string m_FileNameUp;
        std::string m_FileNameForward;

        void Validate( std::vector<std::string>& Errors ) const noexcept
        {
            if(m_FileNameLeft.empty())      Errors.push_back("You forgot to fill the FileName Left");
            if(m_FileNameBack.empty())      Errors.push_back("You forgot to fill the FileName Back");
            if(m_FileNameRight.empty())     Errors.push_back("You forgot to fill the FileName Right");
            if(m_FileNameDown.empty())      Errors.push_back("You forgot to fill the FileName Down");
            if(m_FileNameUp.empty())        Errors.push_back("You forgot to fill the FileName Up");
            if(m_FileNameForward.empty())   Errors.push_back("You forgot to fill the FileName Forward");
        }

        XPROPERTY_DEF
        ("cube_input", cube_input
            , obj_member<"Filename Left Face", &cube_input::m_FileNameLeft, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
            , obj_member<"Filename Back Face", &cube_input::m_FileNameBack, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
            , obj_member<"Filename Right Face", &cube_input::m_FileNameRight, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
            , obj_member<"Filename Down Face", &cube_input::m_FileNameDown, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
            , obj_member<"Filename Up Face", &cube_input::m_FileNameUp, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
            , obj_member<"Filename Forward Face", &cube_input::m_FileNameForward, member_ui<std::string>::file_dialog<image_filter_v, true, 1> >
        )
    };

    struct cube_input_array
    {
        std::vector<cube_input> m_CubeInputArray{ {},{}, };

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            auto C     = Errors.size();
            int  Index = 0;

            for (auto& E : m_CubeInputArray)
            {
                E.Validate(Errors);
                if (Errors.size() != C)
                {
                    for (auto i = C; i != C; ++i)
                    {
                        Errors[i] = std::format("CubeInputArray[{}] {}", Index, Errors[i]);
                    }

                    C = Errors.size();
                }
            }
        }

        XPROPERTY_DEF
        ("cube_input_array", cube_input_array
        , obj_member<"CubeInput"
            , &cube_input_array::m_CubeInputArray
            , member_ui_list_size::drag_bar<1, 100>
            , member_ui_open<true>
            >
        )
    };

    struct cube_input_mix
    {
        mix_source m_Left;
        mix_source m_Back;
        mix_source m_Right;
        mix_source m_Down;
        mix_source m_Up;
        mix_source m_Forward;

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            auto Function = [](std::vector<std::string>& Errors, const std::string_view Face, const mix_source& S )
            {
                auto C     = Errors.size();

                S.Validate(Errors);
                if (Errors.size() != C)
                {
                    for (auto i = C; i != C; ++i)
                    {
                        Errors[i] = std::format("_CubeInputMix Face[{}] {}", Face, Errors[i]);
                    }

                    C = Errors.size();
                }
            };

            Function(Errors, "Left", m_Left);
            Function(Errors, "Back", m_Back);
            Function(Errors, "Right", m_Right);
            Function(Errors, "Down", m_Down);
            Function(Errors, "Up", m_Up);
            Function(Errors, "Forward", m_Forward);
        }

        XPROPERTY_DEF
        ("cube_input_mix", cube_input_mix
        , obj_member<"Left Face",   &cube_input_mix::m_Left >
        , obj_member<"Back Face",   &cube_input_mix::m_Back >
        , obj_member<"Right Face",  &cube_input_mix::m_Right >
        , obj_member<"Down Face",   &cube_input_mix::m_Down >
        , obj_member<"Up Face",     &cube_input_mix::m_Up >
        , obj_member<"Forward Face", &cube_input_mix::m_Forward >
        )
    };

    struct cube_input_mix_array
    {
        std::vector<cube_input_mix> m_CubeMixArray{ {},{}, };

        void Validate(std::vector<std::string>& Errors) const noexcept
        {
            auto C = Errors.size();
            int  Index = 0;

            for (auto& E : m_CubeMixArray)
            {
                E.Validate(Errors);
                if (Errors.size() != C)
                {
                    for (auto i = C; i != C; ++i)
                    {
                        Errors[i] = std::format("CubeMixArray[{}] {}", Index, Errors[i]);
                    }

                    C = Errors.size();
                }
            }
        }

        XPROPERTY_DEF
        ( "cube_input_mix_array", cube_input_mix_array
        , obj_member<"CubeInputMix"
            , &cube_input_mix_array::m_CubeMixArray
            , member_ui_open<true>
            , member_ui_list_size::drag_bar<1, 100>
            >
        )
    };

    enum class variant_enum : std::uint8_t
    { SINGLE_INPUT
    , SINGLE_INPUT_ARRAY
    , MIX_SOURCE
    , MIX_SOURCE_ARRAY
    , CUBE_INPUT
    , CUBE_INPUT_ARRAY
    , CUBE_INPUT_MIX
    , CUBE_INPUT_MIX_ARRAY
    };

    static constexpr auto varient_enum_list_v = std::array
    { xproperty::settings::enum_item("SINGLE_INPUT",            variant_enum::SINGLE_INPUT)
    , xproperty::settings::enum_item("SINGLE_INPUT_ARRAY",      variant_enum::SINGLE_INPUT_ARRAY)
    , xproperty::settings::enum_item("MIX_SOURCE",              variant_enum::MIX_SOURCE)
    , xproperty::settings::enum_item("MIX_SOURCE_ARRAY",        variant_enum::MIX_SOURCE_ARRAY)
    , xproperty::settings::enum_item("CUBE_INPUT",              variant_enum::CUBE_INPUT)
    , xproperty::settings::enum_item("CUBE_INPUT_ARRAY",        variant_enum::CUBE_INPUT_ARRAY)
    , xproperty::settings::enum_item("CUBE_INPUT_MIX",          variant_enum::CUBE_INPUT_MIX)
    , xproperty::settings::enum_item("CUBE_INPUT_MIX_ARRAY",    variant_enum::CUBE_INPUT_MIX_ARRAY)
    };

    using input_variant = std::variant
    < single_input
    , single_input_array
    , mix_source
    , mix_source_array
    , cube_input
    , cube_input_array
    , cube_input_mix
    , cube_input_mix_array
    >;

    enum wrap_type : std::uint8_t
    { CLAMP_TO_EDGE
    , WRAP
    , MIRROR
    };

    static constexpr auto wrap_type_v = std::array
    { xproperty::settings::enum_item("CLAMP_TO_EDGE",           wrap_type::CLAMP_TO_EDGE)
    , xproperty::settings::enum_item("WRAP",                    wrap_type::WRAP)
    , xproperty::settings::enum_item("MIRROR",                  wrap_type::MIRROR)
    };

    enum mipmap_filter : std::uint8_t
    { NONE
    , POINT
    , LINEAR
    , TRIANGLE
    , BOX
    };

    static constexpr auto mipmap_filter_v = std::array
    { xproperty::settings::enum_item("NONE",            mipmap_filter::NONE)
    , xproperty::settings::enum_item("POINT",           mipmap_filter::POINT)
    , xproperty::settings::enum_item("LINEAR",          mipmap_filter::LINEAR)
    , xproperty::settings::enum_item("TRIANGLE",        mipmap_filter::TRIANGLE)
    , xproperty::settings::enum_item("BOX",             mipmap_filter::BOX)
    };



    struct descriptor final : xresource_pipeline::descriptor::base
    {
        input_variant               m_InputVariant  {};
        usage_type                  m_UsageType     { usage_type::COLOR };
        union
        {
            compression_format                  m_Compression           { compression_format::RGB_BC1 };
            compression_format_color            m_CompressionColor;     
            compression_format_color_a          m_CompressionColorA;    
            compression_format_tangent_normal   m_CompressionNormal;    
            compression_format_hdr_color        m_CompressionHDRColor;
        };
        
        float                       m_Quality                   { 0.5f };
        bool                        m_bSRGB                     { true };
        bool                        m_bGenerateMips             { true };
        bool                        m_bMinSizeIsOne             { true };
        bool                        m_bFillAveColorByAlpha      { false };
        std::uint8_t                m_AlphaThreshold            { 128 };
        mipmap_filter               m_MipmapFilter              { mipmap_filter::BOX };
        wrap_type                   m_UWrap                     { wrap_type::CLAMP_TO_EDGE };
        wrap_type                   m_VWrap                     { wrap_type::CLAMP_TO_EDGE };
        bool                        m_bTillableFilter           {false};
        float                       m_TilableWidthPercentage    { 0.1f };
        float                       m_TilableHeightPercentage   { 0.1f };
        bool                        m_bNormalMapFlipY           {false};
        bool                        m_bNormalizeNormals         {true};

        virtual void SetupFromSource(std::string_view FileName)
        {
            
        }

        void Validate( std::vector<std::string>& Errors ) const noexcept override
        {
            std::visit( [&](auto& E )
            {
                E.Validate(Errors);
            }, m_InputVariant);

            if( m_UsageType == usage_type::TANGENT_NORMAL )
            {
                if( m_Compression != compression_format::RG_BC5 
                 && m_Compression != compression_format::RGBA_UNCOMPRESSED
                 && m_Compression != compression_format::RGBA_BC7
                 && m_Compression != compression_format::RGBA_BC3_A8)
                {
                    Errors.push_back("You have selected usage Normal but the compression you selected does not support Normal compression");
                }
            }

            if (m_UWrap == xtexture_rsc::wrap_type::WRAP && m_VWrap == xtexture_rsc::wrap_type::MIRROR)
            {
                Errors.push_back("Sorry but (U set to wrap and V set to mirror) is the only combination that we do not support");
            }

            if(m_bTillableFilter)
            {
                if (m_UWrap != wrap_type::WRAP || m_VWrap != wrap_type::WRAP)
                {
                    Errors.push_back("You have enable to Tillable filter so you must setup the Both Wrap Modes to WRAP....");
                }
            }

            if (m_Compression == compression_format::RG_BC5)
            {
                if (m_bSRGB )
                {
                    Errors.push_back("BC5 does not support gamma You must set SRGB flag to false");
                }
            }
        }

        XPROPERTY_VDEF( "Texture", descriptor
        , obj_member<"Usage"
            , +[](descriptor& O, bool bRead, usage_type& UsageType) constexpr 
            {
                if (bRead)
                {
                    UsageType = O.m_UsageType;
                }
                else
                {
                    O.m_UsageType = UsageType;

                    // Setup useful defaults
                    switch (UsageType)
                    {
                    case usage_type::COLOR:             O.m_Compression = compression_format::RGB_BC1;      O.m_bSRGB = true;  O.m_bFillAveColorByAlpha = false; break;
                    case usage_type::COLOR_AND_ALPHA:   O.m_Compression = compression_format::RGBA_BC1_A1;  O.m_bSRGB = true;  O.m_bFillAveColorByAlpha = true;  break;
                    case usage_type::HDR_COLOR:         O.m_Compression = compression_format::RGB_UHDR_BC6; O.m_bSRGB = true;  O.m_bFillAveColorByAlpha = false; break;
                    case usage_type::INTENSITY:         O.m_Compression = compression_format::RGB_BC1;      O.m_bSRGB = false; O.m_bFillAveColorByAlpha = false; break;
                    case usage_type::TANGENT_NORMAL:    O.m_Compression = compression_format::RG_BC5;       O.m_bSRGB = false; O.m_bFillAveColorByAlpha = false; break;
                    default: assert(false);
                    }
                }
            }
            , member_enum_span<type_v>
            , member_help<"Specifies how the texture will be used. For example, "
                            "it can be used for regular color images, images with transparency, "
                            "high dynamic range (HDR) images, normal maps (used for adding detail "
                            "to 3D models), or intensity maps (like masks). It's like telling the "
                            "system what kind of picture you're working with."
            >>
        , obj_member< "Wrap Mode U"
            , &descriptor::m_UWrap
            , member_enum_span<wrap_type_v>
            , member_help<"Specifies the wrap mode for the U (horizontal) coordinate of texture."
                             " It can be one of several types such as CLAMP_TO_EDGE, WRAP, or MIRROR."
            >>
        , obj_member< "Wrap Mode V"
            , &descriptor::m_VWrap
            , member_enum_span<wrap_type_v>
            , member_help<"Specifies the wrap mode for the V (vertical) coordinate of texture."
                             " It can be one of several types such as CLAMP_TO_EDGE, WRAP, or MIRROR."
            >>
        , obj_member<"InputType"
            , +[](descriptor& O, bool bRead, variant_enum& Shape) constexpr
            {
                if (bRead)
                {
                    Shape = static_cast<variant_enum>(O.m_InputVariant.index());
                }
                else
                {
                    switch (Shape)
                    {
                    case variant_enum::SINGLE_INPUT:            O.m_InputVariant = single_input{};          break;
                    case variant_enum::SINGLE_INPUT_ARRAY:      O.m_InputVariant = single_input_array{};    break;
                    case variant_enum::MIX_SOURCE:              O.m_InputVariant = mix_source{};            break;
                    case variant_enum::MIX_SOURCE_ARRAY:        O.m_InputVariant = mix_source_array{};      break;
                    case variant_enum::CUBE_INPUT:              O.m_InputVariant = cube_input{};            break;
                    case variant_enum::CUBE_INPUT_ARRAY:        O.m_InputVariant = cube_input_array{};      break;
                    case variant_enum::CUBE_INPUT_MIX:          O.m_InputVariant = cube_input_mix{};        break;
                    case variant_enum::CUBE_INPUT_MIX_ARRAY:    O.m_InputVariant = cube_input_mix_array{};  break;
                    default: assert(false);
                    }
                }
            }
            , member_enum_span<varient_enum_list_v>
            , member_help<"Type of input data for the texture. It can be a single image, multiple images, "
                            "or a combination of images. Think of it as choosing whether you want to use one "
                            "picture, a series of pictures, or a mix of different pictures to create your texture."
            >>
        , obj_member < "Input"
            , +[](descriptor& O) constexpr -> std::pair<const xproperty::type::object*, void*>
            {
                return std::visit([&](auto& E) -> std::pair<const xproperty::type::object*, void*>
                {
                    using t = std::remove_reference_t<decltype(E)>;
                    return { getObjectByType<t>(), &E };
                }, O.m_InputVariant);
            }>
        , obj_member<"Compression"
            , &descriptor::m_Compression
            , member_enum_span<compression_format_v>
            , member_dynamic_flags<+[](const descriptor& O)
            { xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::INTENSITY;
                Flags.m_bDontSave = false;
                return Flags;
            }>
            , member_help<"This property specifies the compression format for the texture. "
                          "Compression reduces the file size of the texture, which can help with "
                          "performance. Different formats offer different levels of quality and "
                          "file size. It's like choosing between different ways to save your picture "
                          "to make it smaller without losing too much quality."
            >>
        , obj_member<"Color Compression"
            , &descriptor::m_CompressionColor
            , member_enum_span<compression_format_color_v>
            , member_dynamic_flags<+[](const descriptor& O)
            { xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::COLOR;
                Flags.m_bDontSave = true;
                return Flags;
            }>
            , member_help<"This property specifies the compression format for the texture. "
                          "Compression reduces the file size of the texture, which can help with "
                          "performance. Different formats offer different levels of quality and "
                          "file size. It's like choosing between different ways to save your picture "
                          "to make it smaller without losing too much quality."
            >>
        , obj_member<"Color+A Compression"
            , &descriptor::m_CompressionColorA
            , member_enum_span<compression_format_color_a_v>
            , member_dynamic_flags<+[](const descriptor& O)
            { xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::COLOR_AND_ALPHA;
                Flags.m_bDontSave = true;
                return Flags;
            }>
            , member_help<"This property specifies the compression format for the texture. "
                          "Compression reduces the file size of the texture, which can help with "
                          "performance. Different formats offer different levels of quality and "
                          "file size. It's like choosing between different ways to save your picture "
                          "to make it smaller without losing too much quality."
            >>
        , obj_member<"Tangent Normal Compression"
            , &descriptor::m_CompressionNormal
            , member_enum_span<compression_format_tangent_normal_v>
            , member_dynamic_flags<+[](const descriptor& O)
            { xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::TANGENT_NORMAL;
                Flags.m_bDontSave = true;
                return Flags;
            }>
            , member_help<"This property specifies the compression format for the texture. "
                          "Compression reduces the file size of the texture, which can help with "
                          "performance. Different formats offer different levels of quality and "
                          "file size. It's like choosing between different ways to save your picture "
                          "to make it smaller without losing too much quality."
            >>
        , obj_member<"HDR Color Compression"
            , &descriptor::m_CompressionHDRColor
            , member_enum_span<compression_format_hdr_color_v>
            , member_dynamic_flags<+[](const descriptor& O)
            { xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::HDR_COLOR;
                Flags.m_bDontSave = true;
                return Flags;
            }>
            , member_help<"Specifies the compression format for the texture. "
                          "Compression reduces the file size of the texture, which can help with "
                          "performance. Different formats offer different levels of quality and "
                          "file size. It's like choosing between different ways to save your picture "
                          "to make it smaller without losing too much quality."
            >>
        , obj_member<"Quality"
            , &descriptor::m_Quality
            , member_ui<float>::scroll_bar<0, 1>
            , member_help<"Quality affects the level of detail in the texture. "
                            "Higher quality means more detail, but will take longer to compute."
            >>
        , obj_member<"SRGB"
            , &descriptor::m_bSRGB
            , member_help<"Tell the system that the image is Gamma and that the mips and other functions "
                          "should convert to linear before doing anything to its data. However the RAW data "
                          "can still be store in the linear range. If you don't enable this the texture will be "
                          "consider to be fully in linear space. Linear space is reserved for textures such "
                          "Normal maps, Roughness, AO, etc... Basically mathematical textures not meant for the "
                          "Humans to be seems."
            >>
        , obj_scope<"Mipmaps"
            , obj_member<"GenerateMips"
                , &descriptor::m_bGenerateMips
                , member_help<"This property indicates whether the texture should be treated as sRGB, "
                                 "which is a standard color space for images. If set to true, the texture "
                                 "will be treated as sRGB, ensuring colors look correct on different screens. "
                                 "It's like making sure your colors look right on any device."
                >>
            , obj_member<"Mipmap Filter"
                , &descriptor::m_MipmapFilter
                , member_enum_span<mipmap_filter_v>
                , member_dynamic_flags < +[](const descriptor& O)
                {
                    xproperty::flags::type Flags{};
                    Flags.m_bDontShow = O.m_bGenerateMips == false;
                    return Flags;
                }>
                , member_help<"This property specifies the filter to be used for generating mipmaps. "
                                 "Different filters can affect the quality and performance of the mipmaps. "
                                 "It's like choosing the best way to create smaller versions of your picture."
                >>
            , obj_member<"MinSizeIsOne"
                , &descriptor::m_bMinSizeIsOne
                , member_dynamic_flags < +[](const descriptor& O)
                {
                    xproperty::flags::type Flags{};
                    Flags.m_bDontShow = O.m_bGenerateMips == false;
                    return Flags;
                }>
                , member_help<"Ask the mipmap generator to compute down to 1x1 size if enable. If it is disable, "
                                 "then it is up to the compressor to decide how small to go. There are pros and const "
                                 "as always:\nKeeping 1x1 sizes:\n"
                                 "Pros: More standard\n"
                                 "Cons: A bit more memory usage (more diskspace, less performance, less memory)\n"
                >>
            >
        , obj_member<"FillAveColorByAlpha"
            , &descriptor::m_bFillAveColorByAlpha
            , member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::COLOR_AND_ALPHA && O.m_UsageType != usage_type::INTENSITY;
                return Flags;
            }>
            , member_help<"This property indicates whether the average color should be filled by the alpha channel. "
                            "It's used when the texture has transparency or is an intensity map. It's like using the "
                            "transparency information to fill in the average color of the texture."
            >>
        , obj_member<"AlphaThreshold"
            , &descriptor::m_AlphaThreshold 
            , member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontShow = !(O.m_UsageType == usage_type::COLOR_AND_ALPHA && (O.m_bFillAveColorByAlpha || O.m_Compression == compression_format::RGBA_BC1_A1));
                return Flags;
            }>
            , member_help<"Specifies the alpha threshold value, which determines how transparent parts of the texture are "
                             "handled.It's like setting a cutoff point for what is considered see-through in your picture."
            >>

        , obj_scope< "Tillable Filter"
            , obj_member<"Tillable Filter"
                , &descriptor::m_bTillableFilter
                , member_help<"Makes the final image tillable by blending the edges. You can enable or disable this filter"
                >>
            , obj_member<"Width Blend Percentage"
                , &descriptor::m_TilableWidthPercentage
                , member_ui<float>::scroll_bar<0, 0.5f>
                , member_dynamic_flags < +[](const descriptor& O)
                {
                    xproperty::flags::type Flags{};
                    Flags.m_bDontShow = O.m_bTillableFilter == false;
                    return Flags;
                } >
                , member_help<"Permanganate on how much to blend relative to the Width of the image. Values ranges from 0 to 0.5f"
                >>
            , obj_member<"Height Blend Percentage"
                , &descriptor::m_TilableHeightPercentage
                , member_ui<float>::scroll_bar<0, 0.5f>
                , member_dynamic_flags < +[](const descriptor& O)
                {
                    xproperty::flags::type Flags{};
                    Flags.m_bDontShow = O.m_bTillableFilter == false;
                    return Flags;
                } >
                , member_help<"Permanganate on how much to blend relative to the Height of the image. Values ranges from 0 to 0.5f"
                >>
            , member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_InputVariant.index() >= static_cast<std::size_t>(variant_enum::CUBE_INPUT);
                return Flags;
            } 
            >>
        , obj_member < "Normalize Normals"
            , &descriptor::m_bNormalizeNormals
            , member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::TANGENT_NORMAL;
                return Flags;
            } >
            , member_help<"Normalize the normals from the image before compression"
            >>
        , obj_member < "Normal Flip Y"
            , &descriptor::m_bNormalMapFlipY
            , member_dynamic_flags < +[](const descriptor& O)
            {
                xproperty::flags::type Flags{};
                Flags.m_bDontShow = O.m_UsageType != usage_type::TANGENT_NORMAL;
                return Flags;
            } >
            , member_help<"Flips the Y in the normal map making it compatible with OpenGL or DX"
            >>



        )
    };
    XPROPERTY_VREG(descriptor)
    XPROPERTY_REG(single_input)
    XPROPERTY_REG(single_input_array)
    XPROPERTY_REG(mix_source)
    XPROPERTY_REG(mix_source_array)
    XPROPERTY_REG(cube_input)
    XPROPERTY_REG(cube_input_array)
    XPROPERTY_REG(cube_input_mix)
    XPROPERTY_REG(cube_input_mix_array)
    XPROPERTY_REG(mix)

    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor( void ) const noexcept override
        {
            return std::make_unique<descriptor>();
        };

        std::uint64_t ResourceTypeGUID( void ) const noexcept override
        {
            return full_guid_v.m_Instance.m_Value;
        }

        const char* ResourceTypeName( void ) const noexcept override
        {
            return "Texture";
        }

        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override
        {
            return *xproperty::getObjectByType<descriptor>();
        }
    };

    inline static factory g_Factory{};
}
#endif