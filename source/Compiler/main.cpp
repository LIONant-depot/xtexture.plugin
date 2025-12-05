#include "xtexture_compiler.h"

//---------------------------------------------------------------------------------

int main( int argc, const char* argv[] )
{
    //
    // This is just for debugging
    //
    if constexpr (false)
    {
        static const char* pDebugArgs[] = 
        { "TextureCompiler"
        , "-PROJECT"
        , "D:\\xgpu_test\\xGPU\\example.lionprj"
        , "-DEBUG"
        , "D1"
        , "-DESCRIPTOR"
        , "Descriptors\\Texture\\33\\00\\189E352FD8840033.desc" 
        , "-OUTPUT"
        , "D:\\xgpu_test\\xGPU\\example.lionproj\\Cache\\Resources\\Platforms"
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
        Err.ForEachInChain( [&](xerr Error)
        {
            auto Hint   = Err.getHint();
            auto String = std::format("Error: {}\n", Err.getMessage());
            printf("%s", String.c_str());
            if (Hint.empty() == false ) 
                printf("Hint: %s\n", Hint.data() );
        });
        return 1;
    }

    //
    // Start compilation
    //
    if( auto Err = TextureCompilerPipeline->Compile(); Err )
    {
        Err.ForEachInChain([&](xerr Error)
        {
            auto Hint   = Err.getHint();
            auto String = std::format("Error: {}\n", Err.getMessage());
            printf("%s", String.c_str());
            if (Hint.empty() == false ) 
                printf("Hint: %s\n", Hint.data() );
        });
        return 1;
    }

    return 0;
}


