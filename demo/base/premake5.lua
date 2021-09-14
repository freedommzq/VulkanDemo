project "base"

    kind "StaticLib"
    language "C++"
    cppdialect "C++11"
    staticruntime "on"
    systemversion "latest"

    targetdir ("%{wks.location}/bin/"..cfgDir.."/%{prj.name}")
    objdir ("%{wks.location}/bin-intermediate/"..cfgDir.."/%{prj.name}")

    files{
        "src/**.h",
        "src/**.hpp",
        "src/**.cpp",

        "../../external/imgui/**.cpp",
        "../../external/ktx/lib/checkheader.c",
        "../../external/ktx/lib/filestream.c",
        "../../external/ktx/lib/hashlist.c",
        "../../external/ktx/lib/memstream.c",
        "../../external/ktx/lib/swap.c",
        "../../external/ktx/lib/texture.c"
    }

    includedirs{
        "../../external",
        "../../external/glm",
        "../../external/imgui",
        "../../external/ktx/include",
        "../../external/ktx/other_include",
        "../../external/stb",
        "../../external/tinygltf",
        "../../external/vulkan"
    }

    defines{
        "WIN32",
        "_WINDOWS",
        "VK_USE_PLATFORM_WIN32_KHR",
        "NOMINMAX",
    }

    filter "configurations:Debug"
        targetname "%{prj.name}d"
        symbols "on"

    filter "configurations:Release"
        optimize "on"
        defines{
            "NDEBUG"
        }