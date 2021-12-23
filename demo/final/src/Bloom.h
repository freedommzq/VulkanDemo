#include <vulkan/vulkan.h>

#include "vulkanexamplebase.h"

#include "VulkanFrameBuffer.hpp"

class Bloom {
	static constexpr uint32_t MAX_MIP_LEVEL = 7;
public:
	void init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, vks::FramebufferAttachment* hdrScene, uint32_t width, uint32_t height);
	void draw(VkCommandBuffer cb);
	void onResized(vks::FramebufferAttachment* hdrScene, uint32_t width, uint32_t height);
	void destroy();
private:
	void prepareMipmap();
	void destroyMipmap();

	void preparePrefilter();
	void prepareDownsample();
	void prepareUpsample();
	void prepareComposite();
	void updateDescriptorSets();
private:
	vks::VulkanDevice* m_vkDevice;
	VulkanExampleBase* m_example;

	vks::FramebufferAttachment* m_hdrScene;

	uint32_t m_width;
	uint32_t m_height;

	uint32_t m_mipLevel;
	VkImage m_mipmap;
	VkDeviceMemory m_mipmapMem;
	VkImageView m_mipmapViews[MAX_MIP_LEVEL] = {};

	VkImage m_mipmapIntermediate;
	VkDeviceMemory m_mipmapIntermediateMem;
	VkImageView m_mipmapIntermediateViews[MAX_MIP_LEVEL - 1] = {};

	VkSampler m_linearSampler;
	VkSampler m_nearestSampler;

	VkDescriptorPool m_descriptorPool;

	struct Prefilter {
		struct {
			float threshold;
		} uniforms;
		vks::Buffer ubo;

		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSet;
	} prefilter;

	struct Downsample {
		struct PushConstant {
			glm::vec2 outputTextureSize;
			glm::vec2 inputInvTextureSize;
		};

		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		struct {
			VkPipeline horizontal;
			VkPipeline vertical;
		} pipelines;
		struct {
			VkDescriptorSet horizontal[MAX_MIP_LEVEL - 1];
			VkDescriptorSet vertical[MAX_MIP_LEVEL - 1];
		} descriptorSets;
	} downsample;

	struct Upsample {
		struct PushConstant {
			glm::vec2 outputTextureSize;
		};

		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSets[MAX_MIP_LEVEL - 1];
	} upsample;

	struct Composite {
		VkDescriptorSet descriptorSet;
	} composite;
};