#include <vulkan/vulkan.h>

#include <VulkanDevice.h>

class DepthHierarchy {
public:
	static constexpr uint32_t MAX_MIP_LEVEL = 13;
public:
	DepthHierarchy();
	~DepthHierarchy();
	void init(vks::VulkanDevice* device, uint32_t width, uint32_t height, VkFormat format);
	void recreateOnResize(vks::VulkanDevice* device, uint32_t width, uint32_t height, VkFormat format);
	void destroy(VkDevice device);
public:
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkImageView splitViews[MAX_MIP_LEVEL] = {};

	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorSet descriptorSets[MAX_MIP_LEVEL];

	uint32_t mipLevel = -1;
};