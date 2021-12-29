workspace "VulkanDemo"

    architecture "x64"
    characterset "MBCS"

    configurations{
        "Debug",
        "Release"
    }

    vpaths{
        ["shaders"] = { "**.vert", "**.frag", "**.comp", "**.geom" }
    }

    cfgDir = "%{cfg.buildcfg}-%{cfg.architecture}"
    vulkanLib = "C:/VulkanSDK/1.2.148.0/Lib/vulkan-1.lib"

    startproject "final"

    include "demo/base"
    include "demo/gpuCull"
    include "demo/gpuCluster"
    include "demo/sponza"
    include "demo/textureDDS"
    include "demo/ArrayOfDescriptor"
    include "demo/renderpasses"
    include "demo/ssr"
    include "demo/final"
    include "demo/pbr"
    include "demo/loadPackage"