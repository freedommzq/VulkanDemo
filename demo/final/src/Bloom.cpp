#include "Bloom.h"

void Bloom::init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, vks::FramebufferAttachment* hdrScene, uint32_t width, uint32_t height)
{
	m_vkDevice = vkDevice;
	m_example = example;
	m_hdrScene = hdrScene;
	m_width = width;
	m_height = height;

	// Descriptor Pool
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 + 2 * (MAX_MIP_LEVEL - 1)),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 + 2 * (MAX_MIP_LEVEL - 1) + 2 * MAX_MIP_LEVEL)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1 + 2 * (MAX_MIP_LEVEL - 1) + MAX_MIP_LEVEL);
		VK_CHECK_RESULT(vkCreateDescriptorPool(m_vkDevice->logicalDevice, &descriptorPoolInfo, nullptr, &m_descriptorPool));
	}

	// Sampler
	{
		VkSamplerCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = VK_FILTER_LINEAR;
		info.minFilter = VK_FILTER_LINEAR;
		info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.minLod = -1000;
		info.maxLod = 1000;
		info.maxAnisotropy = 1.0f;
		VK_CHECK_RESULT(vkCreateSampler(m_vkDevice->logicalDevice, &info, nullptr, &m_linearSampler));

		info.magFilter = VK_FILTER_NEAREST;
		info.minFilter = VK_FILTER_NEAREST;
		VK_CHECK_RESULT(vkCreateSampler(m_vkDevice->logicalDevice, &info, nullptr, &m_nearestSampler));
	}

	prepareMipmap();

	preparePrefilter();
	prepareDownsample();
	prepareUpsample();
	prepareComposite();
	updateDescriptorSets();
}

void Bloom::draw(VkCommandBuffer cb)
{
	// Prefilter
	//
	{
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, prefilter.pipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, prefilter.pLayout, 0, 1, &prefilter.descriptorSet, 0, nullptr);

		Downsample::PushConstant data;
		data.outputTextureSize = glm::vec2(m_width >> 1, m_height >> 1);
		data.inputInvTextureSize = glm::vec2(1.0f / m_width, 1.0f / m_height);
		vkCmdPushConstants(cb, downsample.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Downsample::PushConstant), (void*)&data);

		uint32_t dispatchX = ((m_width >> 1) + 7) / 8;
		uint32_t dispatchY = ((m_height >> 1) + 7) / 8;
		uint32_t dispatchZ = 1;

		vkCmdDispatch(cb, dispatchX, dispatchY, dispatchZ);
	}

	// Downsample
	//
	{
		uint32_t dispatchX;
		uint32_t dispatchY;
		uint32_t dispatchZ = 1;

		for (uint32_t i = 0; i < m_mipLevel - 1; ++i) {
			// First: Downsample + Horizontal Gaussian Blur
			//

			// m_mipmap[i]: VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1 };
			imageMemoryBarrier.image = m_mipmap;

			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, downsample.pipelines.horizontal);
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, downsample.pLayout, 0, 1, &downsample.descriptorSets.horizontal[i], 0, nullptr);
			
			Downsample::PushConstant data;
			data.outputTextureSize = glm::vec2(m_width >> (i + 2), m_height >> (i + 2));
			data.inputInvTextureSize = glm::vec2(1.0f / (m_width >> (i + 1)), 1.0f / (m_height >> (i + 1)));
			vkCmdPushConstants(cb, downsample.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Downsample::PushConstant), (void*)&data);

			dispatchX = ((m_width >> (i + 2)) + 7) / 8;
			dispatchY = ((m_height >> (i + 2)) + 7) / 8;

			vkCmdDispatch(cb, dispatchX, dispatchY, dispatchZ);

			// Second: Vertical Gaussian Blur
			//

			// m_mipmapIntermediate[i]: VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			imageMemoryBarrier.image = m_mipmapIntermediate;

			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, downsample.pipelines.vertical);
			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, downsample.pLayout, 0, 1, &downsample.descriptorSets.vertical[i], 0, nullptr);

			data.inputInvTextureSize = glm::vec2(1.0f / (m_width >> (i + 2)), 1.0f / (m_height >> (i + 2)));
			vkCmdPushConstants(cb, downsample.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Downsample::PushConstant), (void*)&data);

			vkCmdDispatch(cb, dispatchX, dispatchY, dispatchZ);
		}
	}

	// Upsample
	//
	{
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, upsample.pipeline);

		uint32_t dispatchX;
		uint32_t dispatchY;
		uint32_t dispatchZ = 1;
		for (uint32_t i = m_mipLevel - 1; i > 0; --i) {
			// m_mipmap[i - 1]: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL -> VK_IMAGE_LAYOUT_GENERAL
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 };
			imageMemoryBarrier.image = m_mipmap;

			vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, upsample.pLayout, 0, 1, &upsample.descriptorSets[(m_mipLevel - 1) - i], 0, nullptr);

			Upsample::PushConstant data;
			data.outputTextureSize = glm::vec2(m_width >> i, m_height >> i);
			vkCmdPushConstants(cb, upsample.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Upsample::PushConstant), (void*)&data);

			dispatchX = ((m_width >> i) + 7) / 8;
			dispatchY = ((m_height >> i) + 7) / 8;

			vkCmdDispatch(cb, dispatchX, dispatchY, dispatchZ);
		}
	}

	// Composite
	//
	{
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, upsample.pipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, upsample.pLayout, 0, 1, &composite.descriptorSet, 0, nullptr);

		// m_hdrScene: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL -> VK_IMAGE_LAYOUT_GENERAL
		VkImageMemoryBarrier imageMemoryBarrier = {};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarrier.image = m_hdrScene->image;

		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		Upsample::PushConstant data;
		data.outputTextureSize = glm::vec2(m_width, m_height);
		vkCmdPushConstants(cb, upsample.pLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Upsample::PushConstant), (void*)&data);

		uint32_t dispatchX = (m_width + 7) / 8;
		uint32_t dispatchY = (m_height + 7) / 8;
		uint32_t dispatchZ = 1;

		vkCmdDispatch(cb, dispatchX, dispatchY, dispatchZ);

		// m_hdrScene: VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}

	// m_mipmapIntermediate[0 ~ m_mipLevel - 2]: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL -> VK_IMAGE_LAYOUT_GENERAL
	VkImageMemoryBarrier imageMemoryBarrier = {};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevel - 1, 0, 1 };
	imageMemoryBarrier.image = m_mipmapIntermediate;

	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
}

void Bloom::onResized(vks::FramebufferAttachment* hdrScene, uint32_t width, uint32_t height)
{
	m_hdrScene = hdrScene;
	m_width = width;
	m_height = height;

	destroyMipmap();
	prepareMipmap();
	
	updateDescriptorSets();
}

void Bloom::destroy()
{
	destroyMipmap();

	vkDestroySampler(m_vkDevice->logicalDevice, m_linearSampler, nullptr);
	vkDestroySampler(m_vkDevice->logicalDevice, m_nearestSampler, nullptr);

	vkDestroyDescriptorPool(m_vkDevice->logicalDevice, m_descriptorPool, nullptr);

	// Prefilter
	prefilter.ubo.destroy();
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, prefilter.dsLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, prefilter.pLayout, nullptr);
	vkDestroyPipeline(m_vkDevice->logicalDevice, prefilter.pipeline, nullptr);

	// Downsample
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, downsample.dsLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, downsample.pLayout, nullptr);
	vkDestroyPipeline(m_vkDevice->logicalDevice, downsample.pipelines.horizontal, nullptr);
	vkDestroyPipeline(m_vkDevice->logicalDevice, downsample.pipelines.vertical, nullptr);

	// Upsample
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, upsample.dsLayout, nullptr);
	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, upsample.pLayout, nullptr);
	vkDestroyPipeline(m_vkDevice->logicalDevice, upsample.pipeline, nullptr);
}

void Bloom::prepareMipmap()
{
	// Mipmap chain
	{
		m_mipLevel = std::min(MAX_MIP_LEVEL, static_cast<uint32_t>(std::log2((std::max)(m_width, m_height))) + 1);

		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.extent = { m_width >> 1, m_height >> 1, 1 };
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.mipLevels = m_mipLevel;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		VK_CHECK_RESULT(vkCreateImage(m_vkDevice->logicalDevice, &imageCreateInfo, nullptr, &m_mipmap));

		VkMemoryRequirements mem_reqs;
		vkGetImageMemoryRequirements(m_vkDevice->logicalDevice, m_mipmap, &mem_reqs);
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = 0;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = m_vkDevice->getMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(m_vkDevice->logicalDevice, &alloc_info, nullptr, &m_mipmapMem));
		VK_CHECK_RESULT(vkBindImageMemory(m_vkDevice->logicalDevice, m_mipmap, m_mipmapMem, 0));

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = m_mipmap;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.subresourceRange.layerCount = 1;
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		for (uint32_t i = 0; i < m_mipLevel; ++i) {
			info.subresourceRange.baseMipLevel = i;
			VK_CHECK_RESULT(vkCreateImageView(m_vkDevice->logicalDevice, &info, nullptr, &m_mipmapViews[i]));
		}
	}

	// Mipmap intermediate 
	// Use to store intermediate result
	{
		VkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.extent = { m_width >> 2, m_height >> 2, 1 };
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.mipLevels = m_mipLevel - 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
		VK_CHECK_RESULT(vkCreateImage(m_vkDevice->logicalDevice, &imageCreateInfo, nullptr, &m_mipmapIntermediate));

		VkMemoryRequirements mem_reqs;
		vkGetImageMemoryRequirements(m_vkDevice->logicalDevice, m_mipmapIntermediate, &mem_reqs);
		VkMemoryAllocateInfo alloc_info = {};
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.allocationSize = 0;
		alloc_info.allocationSize = mem_reqs.size;
		alloc_info.memoryTypeIndex = m_vkDevice->getMemoryType(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(m_vkDevice->logicalDevice, &alloc_info, nullptr, &m_mipmapIntermediateMem));
		VK_CHECK_RESULT(vkBindImageMemory(m_vkDevice->logicalDevice, m_mipmapIntermediate, m_mipmapIntermediateMem, 0));

		VkImageViewCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = m_mipmapIntermediate;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.subresourceRange.layerCount = 1;
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		for (uint32_t i = 0; i < m_mipLevel - 1; ++i) {
			info.subresourceRange.baseMipLevel = i;
			VK_CHECK_RESULT(vkCreateImageView(m_vkDevice->logicalDevice, &info, nullptr, &m_mipmapIntermediateViews[i]));
		}
	}

	{
		VkCommandBuffer singleCB = m_vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// m_mipmap[0 ~ m_mipLevel - 1]: VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_GENERAL
		VkImageMemoryBarrier imageMemoryBarrier = {};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevel, 0, 1 };
		imageMemoryBarrier.image = m_mipmap;

		vkCmdPipelineBarrier(singleCB, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		// m_mipmapIntermediate[0 ~ m_mipLevel - 2]: VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_GENERAL
		imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_mipLevel - 1, 0, 1 };
		imageMemoryBarrier.image = m_mipmapIntermediate;

		vkCmdPipelineBarrier(singleCB, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		m_vkDevice->flushCommandBuffer(singleCB, m_example->queue, true);
	}
}

void Bloom::destroyMipmap()
{
	vkDestroyImage(m_vkDevice->logicalDevice, m_mipmap, nullptr);
	for (int i = 0; i < MAX_MIP_LEVEL; ++i) {
		if (m_mipmapViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(m_vkDevice->logicalDevice, m_mipmapViews[i], nullptr);
			m_mipmapViews[i] = VK_NULL_HANDLE;
		}
	}
	vkFreeMemory(m_vkDevice->logicalDevice, m_mipmapMem, nullptr);

	vkDestroyImage(m_vkDevice->logicalDevice, m_mipmapIntermediate, nullptr);
	for (int i = 0; i < MAX_MIP_LEVEL - 1; ++i) {
		if (m_mipmapIntermediateViews[i] != VK_NULL_HANDLE) {
			vkDestroyImageView(m_vkDevice->logicalDevice, m_mipmapIntermediateViews[i], nullptr);
			m_mipmapIntermediateViews[i] = VK_NULL_HANDLE;
		}
	}
	vkFreeMemory(m_vkDevice->logicalDevice, m_mipmapIntermediateMem, nullptr);
}

void Bloom::preparePrefilter()
{
	{
		VK_CHECK_RESULT(m_vkDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&prefilter.ubo,
			sizeof(prefilter.uniforms)));

		VK_CHECK_RESULT(prefilter.ubo.map());

		prefilter.uniforms.threshold = 5.0f;
		memcpy(prefilter.ubo.mapped, &prefilter.uniforms, sizeof(prefilter.uniforms));
	}

	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &prefilter.dsLayout));

		// Shared pipeline layout used by all pipelines
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&prefilter.dsLayout, 1);
		VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Downsample::PushConstant) };
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pPipelineLayoutCreateInfo, nullptr, &prefilter.pLayout));
	}

	{
		VkPipelineShaderStageCreateInfo shader = m_example->loadShader(m_example->getShadersPath() + "final/spirv/bloomPrefilter.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		VkComputePipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.layout = prefilter.pLayout;
		pipeline.stage = shader;
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		pipeline.basePipelineIndex = 0;

		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &pipeline, nullptr, &prefilter.pipeline));
	}

	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &prefilter.dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &prefilter.descriptorSet));
	}
}

void Bloom::prepareDownsample()
{
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &downsample.dsLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&downsample.dsLayout, 1);
		VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Downsample::PushConstant) };
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pPipelineLayoutCreateInfo, nullptr, &downsample.pLayout));
	}

	{
		VkPipelineShaderStageCreateInfo shader = m_example->loadShader(m_example->getShadersPath() + "final/spirv/bloomDownsampleHorizontal.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		VkComputePipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.layout = downsample.pLayout;
		pipeline.stage = shader;
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		pipeline.basePipelineIndex = 0;

		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &pipeline, nullptr, &downsample.pipelines.horizontal));

		shader = m_example->loadShader(m_example->getShadersPath() + "final/spirv/bloomDownsampleVertical.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		pipeline.stage = shader;

		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &pipeline, nullptr, &downsample.pipelines.vertical));
	}

	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &downsample.dsLayout, 1);
		for (uint32_t i = 0; i < MAX_MIP_LEVEL - 1; ++i) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &downsample.descriptorSets.horizontal[i]));
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &downsample.descriptorSets.vertical[i]));
		}
	}
}

void Bloom::prepareUpsample()
{
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &upsample.dsLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&upsample.dsLayout, 1);
		VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Upsample::PushConstant) };
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pPipelineLayoutCreateInfo, nullptr, &upsample.pLayout));
	}

	{
		VkPipelineShaderStageCreateInfo shader = m_example->loadShader(m_example->getShadersPath() + "final/spirv/bloomUpsample.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		VkComputePipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.layout = upsample.pLayout;
		pipeline.stage = shader;
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		pipeline.basePipelineIndex = 0;

		VK_CHECK_RESULT(vkCreateComputePipelines(m_vkDevice->logicalDevice, m_example->pipelineCache, 1, &pipeline, nullptr, &upsample.pipeline));
	}

	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &upsample.dsLayout, 1);
		for (uint32_t i = 0; i < MAX_MIP_LEVEL - 1; ++i) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &upsample.descriptorSets[i]));
		}
	}
}

void Bloom::prepareComposite()
{
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &upsample.dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &composite.descriptorSet));
	}
}

void Bloom::updateDescriptorSets()
{
	// Prefilter
	{
		VkDescriptorImageInfo descriptorInputTexture = vks::initializers::descriptorImageInfo(
			m_linearSampler,
			m_hdrScene->view,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo descriptorOutputTexture = vks::initializers::descriptorImageInfo(
			VK_NULL_HANDLE,
			m_mipmapViews[0],
			VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(prefilter.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &prefilter.ubo.descriptor),
			vks::initializers::writeDescriptorSet(prefilter.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &descriptorInputTexture),
			vks::initializers::writeDescriptorSet(prefilter.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, &descriptorOutputTexture)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// Downsample
	for (uint32_t i = 0; i < m_mipLevel - 1; ++i) {
		{
			VkDescriptorImageInfo descriptorInputTexture = vks::initializers::descriptorImageInfo(
				m_linearSampler,
				m_mipmapViews[i],
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo descriptorOutputTexture = vks::initializers::descriptorImageInfo(
				VK_NULL_HANDLE,
				m_mipmapIntermediateViews[i],
				VK_IMAGE_LAYOUT_GENERAL);

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(downsample.descriptorSets.horizontal[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &descriptorInputTexture),
				vks::initializers::writeDescriptorSet(downsample.descriptorSets.horizontal[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &descriptorOutputTexture)
			};
			vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}

		{
			VkDescriptorImageInfo descriptorInputTexture = vks::initializers::descriptorImageInfo(
				m_nearestSampler,
				m_mipmapIntermediateViews[i],
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo descriptorOutputTexture = vks::initializers::descriptorImageInfo(
				VK_NULL_HANDLE,
				m_mipmapViews[i + 1],
				VK_IMAGE_LAYOUT_GENERAL);

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(downsample.descriptorSets.vertical[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &descriptorInputTexture),
				vks::initializers::writeDescriptorSet(downsample.descriptorSets.vertical[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &descriptorOutputTexture)
			};
			vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	// Upsample
	{
		for (uint32_t i = m_mipLevel - 1; i > 0; --i) {
			VkDescriptorImageInfo descriptorLowLevelTexture = vks::initializers::descriptorImageInfo(
				VK_NULL_HANDLE,
				m_mipmapViews[i - 1],
				VK_IMAGE_LAYOUT_GENERAL);

			VkDescriptorImageInfo descriptorHighLevelTexture = vks::initializers::descriptorImageInfo(
				VK_NULL_HANDLE,
				m_mipmapViews[i],
				VK_IMAGE_LAYOUT_GENERAL);

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(upsample.descriptorSets[(m_mipLevel - 1) - i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &descriptorLowLevelTexture),
				vks::initializers::writeDescriptorSet(upsample.descriptorSets[(m_mipLevel - 1) - i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &descriptorHighLevelTexture)
			};
			vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	// Composite
	{
		VkDescriptorImageInfo descriptorOriginalTexture = vks::initializers::descriptorImageInfo(
			VK_NULL_HANDLE,
			m_hdrScene->view,
			VK_IMAGE_LAYOUT_GENERAL);

		VkDescriptorImageInfo descriptorBloomTexture = vks::initializers::descriptorImageInfo(
			VK_NULL_HANDLE,
			m_mipmapViews[0],
			VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(composite.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &descriptorOriginalTexture),
			vks::initializers::writeDescriptorSet(composite.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &descriptorBloomTexture)
		};
		vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}
