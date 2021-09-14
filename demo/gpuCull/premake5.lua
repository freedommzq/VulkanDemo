project "gpuCull"

    kind "WindowedApp"
    language "C++"
    cppdialect "C++11"
    staticruntime "on"
    systemversion "latest"

    targetdir ("%{wks.location}/bin/"..cfgDir.."/%{prj.name}")
    objdir ("%{wks.location}/bin-intermediate/"..cfgDir.."/%{prj.name}")

    files{
        "src/**.cpp",

        "../../data/shaders/%{prj.name}/**.vert",
        "../../data/shaders/%{prj.name}/**.frag",
        "../../data/shaders/%{prj.name}/**.comp",
    }

    includedirs{
        "../base/src",
        "../../external",
        "../../external/glm",
        "../../external/imgui",
        "../../external/ktx/include",
        "../../external/ktx/other_include",
        "../../external/stb",
        "../../external/tinygltf",
        "../../external/vulkan"
    }

    links{
        "base",
        vulkanLib
    }

    filter "configurations:Debug"
        symbols "on"

    filter "configurations:Release"
        optimize "on"
        defines{
            "NDEBUG"
        }