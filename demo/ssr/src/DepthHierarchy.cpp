#include "DepthHierarchy.h"

#include "vulkanexamplebase.h"

#include <cassert>
#include <algorithm>

DepthHierarchy::DepthHierarchy()
{

}

DepthHierarchy::~DepthHierarchy()
{

}

void DepthHierarchy::init(vks::VulkanDevice* device, uint32_t width, uint32_t height, VkFormat format)
{
	mipLevel = static_cast<uint32_t>(std::log2((std::max)(width, height))) + 1;

	VkImageCreateInfo imageCreateInfo{};
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.extent = { width, height, 1 };
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.mipLevels = mipLevel;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	VkResult res = vkCreateImage(device->logicalDevice, &imageCreateInfo, NULL, &image);
	assert(res == VK_SUCCESS);

	VkMemoryRequirements mem_reqs;
	vkGetImageMemoryRequirements(device->logicalDevice, image, &mem_reqs);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.allocationSize = 0;
	alloc_info.allocationSize = mem_reqs.size;
	alloc_info.memoryTypeIndex = device->getMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	/* Allocate memory */
	res = vkAllocateMemory(device->logicalDevice, &alloc_info, NULL, &memory);
	assert(res == VK_SUCCESS);

	/* bind memory */
	res = vkBindImageMemory(device->logicalDevice, image, memory, 0);
	assert(res == VK_SUCCESS);

	/* image view */
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = image;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.subresourceRange.layerCount = 1;
	info.format = format;
	info.subresourceRange.aspectMask = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = mipLevel;
	info.subresourceRange.baseArrayLayer = 0;

	res = vkCreateImageView(device->logicalDevice, &info, NULL, &view);
	assert(res == VK_SUCCESS);
	
	info.subresourceRange.levelCount = 1;
	for (uint32_t i = 0; i < (std::min)(13u, mipLevel); ++i)
	{
		info.subresourceRange.baseMipLevel = i;

		res = vkCreateImageView(device->logicalDevice, &info, NULL, &splitViews[i]);
		assert(res == VK_SUCCESS);
	}

	{
		if (sampler == VK_NULL_HANDLE) {
			VkSamplerCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			info.magFilter = VK_FILTER_NEAREST;
			info.minFilter = VK_FILTER_NEAREST;
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
			info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			info.minLod = -1000;
			info.maxLod = 1000;
			info.maxAnisotropy = 1.0f;
			res = vkCreateSampler(device->logicalDevice, &info, NULL, &sampler);
			assert(res == VK_SUCCESS);
		}
	}
}

void DepthHierarchy::recreateOnResize(vks::VulkanDevice* device, uint32_t width, uint32_t height, VkFormat format)
{
	mipLevel = -1;

	vkDestroyImageView(device->logicalDevice, view, nullptr);
	for (int i = 0; i < MAX_MIP_LEVEL; ++i) {
		if (splitViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(device->logicalDevice, splitViews[i], nullptr);
			splitViews[i] = VK_NULL_HANDLE;
		}
	}
	vkDestroyImage(device->logicalDevice, image, nullptr);
	vkFreeMemory(device->logicalDevice, memory, nullptr);

	init(device, width, height, format);
}

void DepthHierarchy::destroy(VkDevice device)
{
	mipLevel = -1;

	vkDestroyImageView(device, view, nullptr);
	for (int i = 0; i < MAX_MIP_LEVEL; ++i) {
		if (splitViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(device, splitViews[i], nullptr);
			splitViews[i] = VK_NULL_HANDLE;
		}
	}
	vkDestroyImage(device, image, nullptr);
	vkFreeMemory(device, memory, nullptr);

	vkDestroySampler(device, sampler, nullptr);
}
