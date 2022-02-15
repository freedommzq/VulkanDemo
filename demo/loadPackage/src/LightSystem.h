#pragma once

#include "vulkanexamplebase.h"

#include <vulkan/vulkan.h>

class LightSystem {
	static constexpr uint32_t CLUSTER_X = 36;
	static constexpr uint32_t CLUSTER_Y = 20;
	static constexpr uint32_t CLUSTER_Z = 64;
	static constexpr uint32_t CLUSTER_SIZE = CLUSTER_X * CLUSTER_Y * CLUSTER_Z;
	static constexpr uint32_t MAX_LIST_LENGTH = 256;
public:
	struct PointLight {
		glm::vec4 positionRange;
		glm::vec4 colorIntensity;
	};
	struct SpotLight {
		glm::vec4 positionRange;
		glm::vec4 colorIntensity;
		glm::vec4 directionCutoff;
	};
public:
	LightSystem();
	void init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, glm::vec3 position, float range, uint32_t lightCount);
	void calculateFrustum(VkCommandBuffer cb);
	void doLightCulling(VkCommandBuffer cb);
	void destroy();

	void updateCamera();
private:
	void initLights(glm::vec3 position, float range, uint32_t lightCount);
	void createResources();

	void prepareDescriptotSetLayout();
	void preparePipelineLayout();
	void preparePipeline();
	void prepareDescriptorPool();
	void prepareDescriptorSet();
public:
	VkDescriptorSetLayout m_dsLayout;
	VkDescriptorSet m_descriptorSet;
private:
	vks::VulkanDevice* m_vkDevice;
	VulkanExampleBase* m_example;

	VkDescriptorPool m_descriptorPool;

	struct {
		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;

		struct {
			VkPipeline frustumXY;
			VkPipeline frustumZ;
		}pipelines;
		struct {
			VkDescriptorSet frustumXY;
			VkDescriptorSet frustumZ;
		}descriptorSets;
	}m_frustum;

	struct {
		VkDescriptorSetLayout dsLayout;
		VkPipelineLayout pLayout;
		VkPipeline pipeline;
		VkDescriptorSet descriptorSet;
	}m_lightCulling;

	vks::Texture m_clusterDataImage;
	vks::Buffer m_pointLightBuffer;
	vks::Buffer m_spotLightBuffer;
	vks::Buffer m_lightListBuffer;

	vks::Buffer m_cameraBuffer;
	vks::Texture3D m_frustumXYImage;
	vks::Texture2D m_frustumZImage;

	std::vector<PointLight> m_pointLights;
	std::vector<SpotLight> m_spotLights;
	struct {
		glm::mat4 viewProjInv;
		glm::mat4 viewInv;
	}m_uniformCamera;
};