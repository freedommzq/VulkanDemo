#include "LightSystem.h"

LightSystem::LightSystem()
{

}

void LightSystem::init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, glm::vec3 position, float range, uint32_t lightCount)
{
	m_vkDevice = vkDevice;
	m_example = example;

	initLights(position, range, lightCount);
	createResources();

	prepareDescriptotSetLayout();
	preparePipelineLayout();
	preparePipeline();

	prepareDescriptorPool();
	prepareDescriptorSet();
}

void LightSystem::calculateFrustum(VkCommandBuffer cb)
{
	// Frustum XY
	//
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_frustum.pipelines.frustumXY);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_frustum.pLayout, 0, 1, &m_frustum.descriptorSets.frustumXY, 0, 0);
	vkCmdDispatch(cb, (CLUSTER_X + 7) / 8, (CLUSTER_Y + 7) / 8, 1);

	// Frustum Z
	//
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_frustum.pipelines.frustumZ);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_frustum.pLayout, 0, 1, &m_frustum.descriptorSets.frustumZ, 0, 0);
	vkCmdDispatch(cb, (CLUSTER_Z + 15) / 16, 1, 1);
}

void LightSystem::doLightCulling(VkCommandBuffer cb)
{
	// Add memory barrier to ensure that the clusterImage and clusterDataBuffer have been consumed before the compute shader updates them
	std::array<VkImageMemoryBarrier, 1> imageBarriersBefore{};
	imageBarriersBefore[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarriersBefore[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarriersBefore[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	imageBarriersBefore[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL; // todo: using general image layout for simplicity
	imageBarriersBefore[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageBarriersBefore[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarriersBefore[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarriersBefore[0].image = m_clusterDataImage.image;
	imageBarriersBefore[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	std::array<VkBufferMemoryBarrier, 1> bufferBarriersBefore{};
	bufferBarriersBefore[0] = vks::initializers::bufferMemoryBarrier();
	bufferBarriersBefore[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	bufferBarriersBefore[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	bufferBarriersBefore[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarriersBefore[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarriersBefore[0].buffer = m_lightListBuffer.buffer;
	bufferBarriersBefore[0].size = m_lightListBuffer.size;

	// since light is static, no barrier is needed for light buffer

	// the frustum calculation is doing on CPU side, we'll use fence to make sure it accessible before execute compute shader

	vkCmdPipelineBarrier(
		cb,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_FLAGS_NONE,
		0, nullptr,
		bufferBarriersBefore.size(), bufferBarriersBefore.data(),
		imageBarriersBefore.size(), imageBarriersBefore.data());

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightCulling.pipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightCulling.pLayout, 0, 1, &m_lightCulling.descriptorSet, 0, 0);

	vkCmdDispatch(cb, (CLUSTER_X + 7) / 8, (CLUSTER_Y + 7) / 8, CLUSTER_Z);

	// Add memory barrier to ensure that the compute shader has finished writing the clusterImage and clusterDataBuffer before it's consumed
	std::array<VkImageMemoryBarrier, 1> imageBarriersAfter{};
	imageBarriersAfter[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarriersAfter[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	imageBarriersAfter[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarriersAfter[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL; // todo: using general image layout for simplicity
	imageBarriersAfter[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageBarriersAfter[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarriersAfter[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarriersAfter[0].image = m_clusterDataImage.image;
	imageBarriersAfter[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	std::array<VkBufferMemoryBarrier, 1> bufferBarriersAfter{};
	bufferBarriersAfter[0] = vks::initializers::bufferMemoryBarrier();
	bufferBarriersAfter[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	bufferBarriersAfter[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	bufferBarriersAfter[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarriersAfter[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarriersAfter[0].buffer = m_lightListBuffer.buffer;
	bufferBarriersAfter[0].size = m_lightListBuffer.size;

	vkCmdPipelineBarrier(
		cb,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_FLAGS_NONE,
		0, nullptr,
		bufferBarriersAfter.size(), bufferBarriersAfter.data(),
		imageBarriersAfter.size(), imageBarriersAfter.data()
	);
}

void LightSystem::destroy()
{
	vkDestroyDescriptorPool(m_vkDevice->logicalDevice, m_descriptorPool, nullptr);

	vkDestroyPipeline(m_vkDevice->logicalDevice, m_frustum.pipelines.frustumXY, nullptr);
	vkDestroyPipeline(m_vkDevice->logicalDevice, m_frustum.pipelines.frustumZ, nullptr);
	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, m_frustum.pLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, m_frustum.dsLayout, nullptr);

	vkDestroyPipeline(m_vkDevice->logicalDevice, m_lightCulling.pipeline, nullptr);
	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, m_lightCulling.pLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, m_lightCulling.dsLayout, nullptr);

	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, m_dsLayout, nullptr);

	m_clusterDataImage.destroy();
	m_lightListBuffer.destroy();
	m_pointLightBuffer.destroy();
	m_spotLightBuffer.destroy();
	m_cameraBuffer.destroy();
	m_frustumXYImage.destroy();
	m_frustumZImage.destroy();
}

void LightSystem::initLights(glm::vec3 position, float range, uint32_t lightCount)
{
	std::default_random_engine e;
	std::uniform_real_distribution<float> d(0.0f, 1.0f);

	float maxRange = 100.0f;
	float maxIntensity = 10.0f;

	uint32_t pointLightCount = lightCount / 2;
	m_pointLights.reserve(pointLightCount);
	for (int i = 0; i < pointLightCount; ++i) {
		glm::vec3 pos = position + glm::vec3(d(e), d(e), d(e));
		glm::vec3 color = glm::vec3(1.0f);
		//glm::vec3 color = glm::vec3(d(e), d(e), d(e));
		float range = d(e) * maxRange;
		float intensity = d(e) * maxIntensity;

		PointLight l;
		l.positionRange = glm::vec4(pos, range);
		l.colorIntensity = glm::vec4(color, intensity);
		m_pointLights.push_back(l);
	}

	uint32_t spotLightCount = lightCount - pointLightCount;
	m_spotLights.reserve(spotLightCount);
	for (int i = 0; i < spotLightCount; ++i) {
		glm::vec3 pos = position + glm::vec3(d(e), d(e), d(e));
		glm::vec3 color = glm::vec3(1.0f);
		//glm::vec3 color = glm::vec3(d(e), d(e), d(e));
		glm::vec3 dir = glm::normalize(glm::vec3(d(e) * 2.0f - 1.0f, d(e) * 2.0f - 1.0f, d(e) * 2.0f - 1.0f));
		float range = d(e) * maxRange;
		float intensity = d(e) * maxIntensity;
		float innerCutoff = glm::cos(glm::radians(d(e) * 90.0f));

		SpotLight l;
		l.positionRange = glm::vec4(pos, range);
		l.colorIntensity = glm::vec4(color, intensity);
		l.directionCutoff = glm::vec4(dir, innerCutoff);
		m_spotLights.push_back(l);
	}
}

void LightSystem::createResources()
{
	// Cluster Data
	//
	{
		VkFormat clusterImageFormat = VK_FORMAT_R16_UINT;
		m_clusterDataImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		// Get device properties for the requested texture format
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(m_vkDevice->physicalDevice, clusterImageFormat, &formatProperties);
		// Check if requested image format supports image storage operations
		assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

		VkImageCreateInfo imageInfo = vks::initializers::imageCreateInfo();
		imageInfo.imageType = VK_IMAGE_TYPE_3D;
		imageInfo.format = clusterImageFormat;
		imageInfo.extent = { CLUSTER_X, CLUSTER_Y, CLUSTER_Z };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(m_vkDevice->logicalDevice, &imageInfo, nullptr, &m_clusterDataImage.image));

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(m_vkDevice->logicalDevice, m_clusterDataImage.image, &memReq);
		memAllocInfo.allocationSize = memReq.size;
		memAllocInfo.memoryTypeIndex = m_vkDevice->getMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(m_vkDevice->logicalDevice, &memAllocInfo, nullptr, &m_clusterDataImage.deviceMemory));

		VK_CHECK_RESULT(vkBindImageMemory(m_vkDevice->logicalDevice, m_clusterDataImage.image, m_clusterDataImage.deviceMemory, 0));

		auto layoutCmd = m_vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vks::tools::setImageLayout(layoutCmd, m_clusterDataImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, m_clusterDataImage.imageLayout);
		m_vkDevice->flushCommandBuffer(layoutCmd, m_example->queue, true);

		auto imageViewInfo = vks::initializers::imageViewCreateInfo();
		imageViewInfo.image = m_clusterDataImage.image;
		imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
		imageViewInfo.format = clusterImageFormat;
		imageViewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VK_CHECK_RESULT(vkCreateImageView(m_vkDevice->logicalDevice, &imageViewInfo, nullptr, &m_clusterDataImage.view));

		auto samplerInfo = vks::initializers::samplerCreateInfo();
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = false;
		samplerInfo.compareEnable = false;
		samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(m_vkDevice->logicalDevice, &samplerInfo, nullptr, &m_clusterDataImage.sampler));

		m_clusterDataImage.descriptor.imageView = m_clusterDataImage.view;
		m_clusterDataImage.descriptor.sampler = m_clusterDataImage.sampler;
		m_clusterDataImage.descriptor.imageLayout = m_clusterDataImage.imageLayout;

		m_clusterDataImage.device = m_vkDevice;
	}

	// Light List
	//
	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&m_lightListBuffer,
		CLUSTER_SIZE * MAX_LIST_LENGTH * sizeof(uint32_t)
	));

	// Light Data
	//
	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_pointLightBuffer,
		m_pointLights.size() * sizeof(PointLight),
		m_pointLights.data()));

	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_spotLightBuffer,
		m_spotLights.size() * sizeof(SpotLight),
		m_spotLights.data()));

	// Camera
	//
	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_cameraBuffer,
		sizeof(m_uniformCamera)));
	m_cameraBuffer.map();
	updateCamera();

	// Frustum
	//
	m_frustumXYImage.createEmpty(m_vkDevice, m_example->queue, { CLUSTER_X, CLUSTER_Y, 4 }, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_LAYOUT_GENERAL);
	m_frustumZImage.createEmpty(m_vkDevice, m_example->queue, { CLUSTER_Z, 2 }, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_LAYOUT_GENERAL);
}

void LightSystem::prepareDescriptotSetLayout()
{
	// Frustum
	//
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorSetLayoutCI, nullptr, &m_frustum.dsLayout));
	}

	// Light culling
	//
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 4),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 5)
		};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorSetLayoutCI, nullptr, &m_lightCulling.dsLayout));
	}

	// External lighting
	//
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &m_dsLayout));
	}
}

void LightSystem::preparePipelineLayout()
{
	// Frustum
	//
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&m_frustum.dsLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pipelineLayoutCI, nullptr, &m_frustum.pLayout));
	}

	// Light Culling
	//
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&m_lightCulling.dsLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pipelineLayoutCI, nullptr, &m_lightCulling.pLayout));
	}

}

void LightSystem::preparePipeline()
{
	// Frustum XY
	//
	{
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_frustum.pLayout);
		computePipelineCreateInfo.stage = m_example->loadShader(m_example->getShadersPath() + "loadPackage/spirv/frustumXY.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_frustum.pipelines.frustumXY));
	}

	// Frustum Z
	//
	{
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_frustum.pLayout);
		computePipelineCreateInfo.stage = m_example->loadShader(m_example->getShadersPath() + "loadPackage/spirv/frustumZ.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_frustum.pipelines.frustumZ));
	}

	// Light Culling
	//
	{
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(m_lightCulling.pLayout);
		computePipelineCreateInfo.stage = m_example->loadShader(m_example->getShadersPath() + "loadPackage/spirv/lightCulling.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		struct ClusterSpecializationData {
			uint32_t maxListLength;
		} clusterSpecializationData;

		clusterSpecializationData.maxListLength = MAX_LIST_LENGTH;

		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, offsetof(ClusterSpecializationData, maxListLength), sizeof(ClusterSpecializationData::maxListLength))
		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(specializationMapEntries, sizeof(clusterSpecializationData), &clusterSpecializationData);

		//computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &computePipelineCreateInfo, nullptr, &m_lightCulling.pipeline));
	}

}

void LightSystem::prepareDescriptorPool()
{
	std::vector<VkDescriptorPoolSize> typeCounts = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
	};

	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(typeCounts, 4);
	VK_CHECK_RESULT(vkCreateDescriptorPool(m_vkDevice->logicalDevice, &descriptorPoolInfo, nullptr, &m_descriptorPool));
}

void LightSystem::prepareDescriptorSet()
{
	// Frustum XY
	//
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_frustum.dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_frustum.descriptorSets.frustumXY));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(m_frustum.descriptorSets.frustumXY, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &m_cameraBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_frustum.descriptorSets.frustumXY, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &m_frustumXYImage.descriptor)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// Frustum Z
	//
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_frustum.dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_frustum.descriptorSets.frustumZ));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(m_frustum.descriptorSets.frustumZ, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &m_cameraBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_frustum.descriptorSets.frustumZ, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &m_frustumZImage.descriptor)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// Light Culling
	//
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_lightCulling.dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_lightCulling.descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &m_clusterDataImage.descriptor),
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &m_lightListBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &m_pointLightBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &m_spotLightBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4, &m_frustumXYImage.descriptor),
			vks::initializers::writeDescriptorSet(m_lightCulling.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5, &m_frustumZImage.descriptor)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// Lighting
	//
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &m_clusterDataImage.descriptor),
			vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &m_lightListBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &m_pointLightBuffer.descriptor),
			vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &m_spotLightBuffer.descriptor)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}

void LightSystem::updateCamera()
{
	m_uniformCamera.viewProjInv = glm::inverse(m_example->camera.matrices.perspective * m_example->camera.matrices.view);
	m_uniformCamera.viewInv = glm::inverse(m_example->camera.matrices.view);

	memcpy(m_cameraBuffer.mapped, &m_uniformCamera, sizeof(m_uniformCamera));
}
