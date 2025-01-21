#include "xtexture_compiler.h"

//---------------------------------------------------------------------------------

int main( int argc, const char* argv[] )
{
    xcore::Init("xtexture_compiler");

    //
    // This is just for debugging
    //
    if constexpr (false)
    {
        static const char* pDebugArgs[] = 
        { "TextureCompiler"
        , "-PROJECT"
        , "D:\\LIONant\\xGPU\\dependencies\\xtexture.plugin\\bin\\example.lion_project"
        , "-DEBUG"
        , "-Dz"
        , "-DESCRIPTOR"
        , "Descriptors\\Texture\\A9\\EF\\34DBB69E8762EFA9.desc"
        , "-OUTPUT"
        , "D:\\LIONant\\xGPU\\dependencies\\xtexture.plugin\\bin\\example.lion_project\\Cache\\Resources\\Platforms"
        };

        argv = pDebugArgs;
        argc = static_cast<int>(sizeof(pDebugArgs) / sizeof(pDebugArgs[0]));
    }

    //
    // Create the compiler instance
    //
    auto TextureCompilerPipeline = xtexture_compiler::instance::Create();

    //
    // Parse parameters
    //
    if( auto Err = TextureCompilerPipeline->Parse( argc, argv ); Err )
    {
        printf( "%s\nERROR: Fail to compile\n", Err.getCode().m_pString );
        return Err.getCode().m_RawState;
    }

    //
    // Start compilation
    //
    if( auto Err = TextureCompilerPipeline->Compile(); Err )
    {
        printf("%s\nERROR: Fail to compile(2)\n", Err.getCode().m_pString);
        return Err.getCode().m_RawState;
    }

    return 0;
}


