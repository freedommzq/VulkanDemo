workspace "VulkanDemo"

    architecture "x64"
    characterset "MBCS"

    configurations{
        "Debug",
        "Release"
    }

    cfgDir = "%{cfg.buildcfg}-%{cfg.architecture}"
    vulkanLib = "C:/VulkanSDK/1.2.148.0/Lib/vulkan-1.lib"

    startproject "gpuCull"

    project "base"

        location "base"
        kind "StaticLib"
        language "C++"
        cppdialect "C++11"
        staticruntime "on"
        systemversion "latest"

        targetdir ("bin/"..cfgDir.."/%{prj.name}")
        objdir ("intermediate/"..cfgDir.."/%{prj.name}")

        files{
            "%{prj.name}/**.h",
            "%{prj.name}/**.hpp",
            "%{prj.name}/**.cpp",

            "external/imgui/**.cpp",
            "external/ktx/lib/checkheader.c",
            "external/ktx/lib/filestream.c",
            "external/ktx/lib/hashlist.c",
            "external/ktx/lib/memstream.c",
            "external/ktx/lib/swap.c",
            "external/ktx/lib/texture.c"
        }

        includedirs{
            "external",
            "external/glm",
            "external/imgui",
            "external/ktx/include",
            "external/ktx/other_include",
            "external/stb",
            "external/tinygltf",
            "external/vulkan"
        }

        defines{
            "WIN32",
            "_WINDOWS",
            "VK_USE_PLATFORM_WIN32_KHR",
            "NOMINMAX",
            "VK_EXAMPLE_DATA_DIR=\"D:/Vulkan/Vulkan/data/\""
        }

        filter "configurations:Debug"
            targetname "%{prj.name}d"
            symbols "on"

        filter "configurations:Release"
            optimize "on"
            defines{
                "NDEBUG"
            }


    project "gpuCull"

        location "gpuCull"
        kind "WindowedApp"
        language "C++"
        cppdialect "C++11"
        staticruntime "on"
        systemversion "latest"

        targetdir ("bin/"..cfgDir.."/%{prj.name}")
        objdir ("intermediate/"..cfgDir.."/%{prj.name}")

        files{
            "%{prj.name}/**.cpp"
        }

        includedirs{
            "base",
            "external",
            "external/glm",
            "external/imgui",
            "external/ktx/include",
            "external/ktx/other_include",
            "external/stb",
            "external/tinygltf",
            "external/vulkan"
        }

        links{
            "base",
            vulkanLib
        }

        filter "configurations:Debug"
            targetname "%{prj.name}d"
            symbols "on"

        filter "configurations:Release"
            optimize "on"
            defines{
                "NDEBUG"
            }