workspace "VulkanDemo"

    architecture "x64"
    characterset "MBCS"

    configurations{
        "Debug",
        "Release"
    }

    vpaths{
        ["shaders"] = { "**.vert", "**.frag", "**.comp" }
    }

    cfgDir = "%{cfg.buildcfg}-%{cfg.architecture}"
    vulkanLib = "C:/VulkanSDK/1.2.148.0/Lib/vulkan-1.lib"

    startproject "gpuCull"

    include "demo/base"
    include "demo/gpuCull"
    include "demo/gpuCluster"
    include "demo/sponza"
    include "demo/textureDDS"
    include "demo/ArrayOfDescriptor"
    include "demo/renderpasses"
    include "demo/ssr"