#pragma once

#include "DataOperation.h"
#include "VulkanTexture.h"
#include "vulkanexamplebase.h"

#include <vulkan/vulkan.h>

class SimScene {
public:
	SimScene();

	void init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, const std::string& filename);
	void loadFromFile(const std::string& filename);
	void draw(VkCommandBuffer cb, VkPipelineLayout pLayout);
	void destroy();
private:
	void prepareDescriptorSetLayout();
	void prepareDescriptor();
public:
	struct Geometry {
		struct Vertex {
			glm::vec3 pos;
			glm::vec3 normal;
			glm::vec3 uv0;
			glm::vec3 uv1;
			glm::vec3 uv2;
			glm::vec3 uv3;
		};
		vks::Buffer vertexBuffer;
		vks::Buffer indexBuffer;
		uint32_t indexCount;
	};

	std::vector<vks::Texture2D> textures;
	std::vector<Geometry> geometries;

	VkDescriptorSetLayout m_dsLayout;
private:
	vks::VulkanDevice* m_vkDevice;
	VulkanExampleBase* m_example;

	VkDescriptorPool m_descriptorPool;
	VkDescriptorSet m_descriptorSet;
};