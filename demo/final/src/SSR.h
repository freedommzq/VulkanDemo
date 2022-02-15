
#include "vulkanexamplebase.h"

class SSR {
	static constexpr uint32_t MAX_MIP_LEVEL = 13;
public:
	void init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, uint32_t width, uint32_t height);
	void draw(VkCommandBuffer cb);
	void destroy();
private:
	vks::VulkanDevice* m_vkDevice;
	VulkanExampleBase* m_example;

	VkDescriptorPool m_descriptorPool;

	VkImage mipmap;
	VkDeviceMemory mipmapMem;
	VkImageView mipmapView;
	VkImageView mipmapViews[MAX_MIP_LEVEL] = {};

	VkImage reflection;
	VkDeviceMemory reflectionMem;
	VkImageView reflectionView;

	// Generate min-z pyramid
	struct MinDepthPyramid {
		struct PushConstant {

		};

		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSets[MAX_MIP_LEVEL - 1];
	};

	struct Intersection {
		VkRenderPass renderPass;
		VkFramebuffer framebuffer;

		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSet;
	};

	// (Blur &) Composition
	struct Composition {
		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSet;
	};
};