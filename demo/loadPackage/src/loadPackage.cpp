#include "vulkanexamplebase.h"
#include "VulkanFrameBuffer.hpp"
#include "VulkanglTFModel.h"

#include "SimScene.h"
#include "LightSystem.h"

#include "GPUTimestamps.h"
#include "PipelineStatistics.h"

#define ENABLE_VALIDATION true
#define USE_STAGING true

#define INSTANCE_BUFFER_BIND_ID 1
#define X_COUNT 1
#define Y_COUNT 1
#define Z_COUNT 1

class VulkanExample : public VulkanExampleBase {
	SimScene scene; // City
	vkglTF::Model model; // Car

	struct Instance {
		glm::vec3 pos;
	};
	vks::Buffer instanceBuffer;
	uint32_t instanceCount;

	struct {
		glm::mat4 view;
		glm::mat4 viewInv;
		glm::mat4 projection;
		glm::mat4 projectionInv;
		glm::vec3 position;
	} uniformCamera;
	vks::Buffer uboCamera;

	LightSystem lightSystem;

	struct PushConstantModel {
		glm::mat4 model;
	};
	PushConstantModel pcCar;
	// audi
	glm::vec3 translate = glm::vec3(2.0f, -6.55f, -12.4f);
	float angle = -118.0f;
	glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
	float scale = 0.3f;

	// lamborghini
	//glm::vec3 translate = glm::vec3(4.8f, -6.75f, -12.4f);           
	//float angle = 55.0f;
	//glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
	//float scale = 0.6f;

	VkDescriptorSetLayout dsLayout;

	struct {
		VkPipelineLayout city;
		VkPipelineLayout car;
		VkPipelineLayout lighting;
	} pLayouts;

	struct {
		VkPipeline geometryCity;
		VkPipeline geometryCar;
		VkPipeline lighting;
	} pipelines;

	struct {
		VkDescriptorSet geometry;
		VkDescriptorSet lighting;
	} descriptorSets;

	std::unique_ptr<vks::Framebuffer> geometryPass;

	GPUTimestamps GPUTimer;
	std::vector<TimeStamp> timeStamps;

	PipelineStatistics statistics;

public:
	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Load Package Example";
		camera.type = Camera::CameraType::firstperson;
		camera.flipY = true;
		camera.setPosition(glm::vec3(-1500.0f, 5.0f, 4000.0f + 2.5f)); // 0.0f, 0.0f, -2.5f
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 1500.0f); // 256.0f
		settings.overlay = true;

		vkglTF::descriptorBindingFlags |= 
			vkglTF::DescriptorBindingFlags::ImageNormalMap | 
			vkglTF::DescriptorBindingFlags::ImageMetallicRoughness |
			vkglTF::DescriptorBindingFlags::ImageEmissive;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.geometryCity, nullptr);
		vkDestroyPipeline(device, pipelines.geometryCar, nullptr);
		vkDestroyPipeline(device, pipelines.lighting, nullptr);

		vkDestroyPipelineLayout(device, pLayouts.city, nullptr);
		vkDestroyPipelineLayout(device, pLayouts.car, nullptr);
		vkDestroyPipelineLayout(device, pLayouts.lighting, nullptr);

		vkDestroyDescriptorSetLayout(device, dsLayout, nullptr);

		uboCamera.destroy();
		instanceBuffer.destroy();

		scene.destroy();
		lightSystem.destroy();

		GPUTimer.OnDestroy();
		statistics.destroy();
	}

	virtual void getEnabledFeatures(){
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		};

		// Support for pipeline statistics is optional
		if (deviceFeatures.pipelineStatisticsQuery) {
			enabledFeatures.pipelineStatisticsQuery = VK_TRUE;
		}
		else {
			vks::tools::exitFatal("Selected GPU does not support pipeline statistics!", VK_ERROR_FEATURE_NOT_PRESENT);
		}
	}

	virtual void setupDepthStencil() override
	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent = { width, height, 1 };
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));
		VkMemoryRequirements memReqs{};
		vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);

		VkMemoryAllocateInfo memAllloc{};
		memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllloc.allocationSize = memReqs.size;
		memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &depthStencil.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

		VkImageViewCreateInfo imageViewCI{};
		imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.image = depthStencil.image;
		imageViewCI.format = depthFormat;
		imageViewCI.subresourceRange.baseMipLevel = 0;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.baseArrayLayer = 0;
		imageViewCI.subresourceRange.layerCount = 1;
		imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &depthStencil.view));

		{
			VkCommandBuffer singleCB = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

			// depthStencil imageLayout transition
			VkImageMemoryBarrier imageMemoryBarrier = {};
			imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
			imageMemoryBarrier.image = depthStencil.image;

			vkCmdPipelineBarrier(singleCB, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			vulkanDevice->flushCommandBuffer(singleCB, queue, true);
		}
	}

	virtual void setupRenderPass() override {
		VkAttachmentDescription attachment = {};
		attachment.format = swapChain.colorFormat;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorRef;
		subpassDescription.pDepthStencilAttachment = nullptr;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;
		subpassDescription.pResolveAttachments = nullptr;

		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = 0;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = 0;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &attachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	}

	virtual void setupFrameBuffer() override {
		VkImageView attachment;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = 1;
		frameBufferCreateInfo.pAttachments = &attachment;
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;

		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			attachment = swapChain.buffers[i].view;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
		}
	}

	void loadAssets() {
		//scene.init(vulkanDevice, this, getAssetPath() + "models/mzq/Jetta_a_008_test_0011.simpkg");
		scene.init(vulkanDevice, this, getAssetPath() + "models/mzq/Area057_58_OutSide_06_ZhengHe_XHD_Night_WZB.simpkg");

		model.loadFromFile(
			//getAssetPath() + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf",
			//getAssetPath() + "models/lamborghini/scene.gltf",
			//getAssetPath() + "models/toyota/scene.gltf",
			//getAssetPath() + "models/sponza/sponza.gltf",
			getAssetPath() + "models/audi/scene.gltf",
			vulkanDevice,
			queue,
			vkglTF::FileLoadingFlags::PreTransformVertices// | vkglTF::FileLoadingFlags::FlipY
		);
	}

	VkCommandBuffer beginSingleTimeCB() {
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = vulkanDevice->commandPool;
		allocInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void endSingleTimeCB(VkCommandBuffer commandBuffer) {
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(queue);

		vkFreeCommandBuffers(device, vulkanDevice->commandPool, 1, &commandBuffer);
	}

#if 0
	void prepareQuad() {
		std::vector<Vertex> vertices =
		{
			   // Position            // Normal             // UV           // Color
			{ {  1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0, 0.0, 0.0 } },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f }, { 0.0, 1.0, 0.0 } },
			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0, 0.0, 1.0 } },
			{ {  1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0, 1.0, 0.0 } }
		};

		// Setup indices
		std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
		indexCount = static_cast<uint32_t>(indices.size());

#if USE_STAGING
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertexBuffer,
			vertices.size() * sizeof(Vertex)));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indexBuffer,
			indices.size() * sizeof(uint32_t)));

		vks::Buffer stagingBufferVertex, stagingBufferIndex;

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBufferVertex,
			vertices.size() * sizeof(Vertex),
			vertices.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBufferIndex,
			indices.size() * sizeof(uint32_t),
			indices.data()));

		VkCommandBuffer cb = beginSingleTimeCB();

		VkBufferCopy region;
		region = { 0, 0, vertices.size() * sizeof(Vertex) };
		vkCmdCopyBuffer(cb, stagingBufferVertex.buffer, vertexBuffer.buffer, 1, &region);

		region = { 0, 0, indices.size() * sizeof(uint32_t) };
		vkCmdCopyBuffer(cb, stagingBufferIndex.buffer, indexBuffer.buffer, 1, &region);

		endSingleTimeCB(cb);

		stagingBufferVertex.destroy();
		stagingBufferIndex.destroy();
#else
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexBuffer,
			vertices.size() * sizeof(Vertex),
			vertices.data()));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexBuffer,
			indices.size() * sizeof(uint32_t),
			indices.data()));
#endif
	}
#endif

	void preparePasses() {
		geometryPass = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);

		geometryPass->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		geometryPass->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		geometryPass->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		vks::FramebufferAttachment depth;
		depth.image = depthStencil.image;
		depth.memory = depthStencil.mem;
		depth.view = depthStencil.view;
		depth.format = depthFormat;
		depth.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		depth.description = {
			0,
			depth.format,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};
		depth.weakRef = true;
		geometryPass->attachments.push_back(depth);

		VK_CHECK_RESULT(geometryPass->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(geometryPass->createRenderPass());
	}

	void recreatePasses() {
		geometryPass->clearBeforeRecreate(width, height);
		geometryPass->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		geometryPass->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		geometryPass->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		vks::FramebufferAttachment depth;
		depth.image = depthStencil.image;
		depth.memory = depthStencil.mem;
		depth.view = depthStencil.view;
		depth.format = depthFormat;
		depth.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		depth.description = {
			0,
			depth.format,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};
		depth.weakRef = true;
		geometryPass->attachments.push_back(depth);

		geometryPass->createFrameBuffer();
	}

	void prepareInstanceBuffer() {
		instanceCount = X_COUNT * Y_COUNT * Z_COUNT;

		std::vector<Instance> instances;
		instances.reserve(instanceCount);
		float scale = 5.0f;
		for (uint32_t x = 0; x < X_COUNT; x++) {
			for (uint32_t y = 0; y < Y_COUNT; y++) {
				for (uint32_t z = 0; z < Z_COUNT; z++) {
					Instance ins;	
					ins.pos = scale * glm::vec3(
						(float)x - (float)X_COUNT / 2.0f,
						(float)y - (float)Y_COUNT / 2.0f,
						(float)z - (float)Z_COUNT / 2.0f
					);
					instances.push_back(ins);
				}
			}
		}

		vks::Buffer stagingBuffer;

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			instances.size() * sizeof(Instance),
			instances.data()));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&instanceBuffer,
			stagingBuffer.size));

		vulkanDevice->copyBuffer(&stagingBuffer, &instanceBuffer, queue);

		stagingBuffer.destroy();
	}

	void prepareUniformBuffer() {
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uboCamera,
			sizeof(uniformCamera),
			&uniformCamera));
		VK_CHECK_RESULT(uboCamera.map());

		updateCameraUniforms();
		updateCarUniform();
	}

	void updateCameraUniforms(){
		uniformCamera.view = camera.matrices.view;
		uniformCamera.viewInv = glm::inverse(camera.matrices.view);
		uniformCamera.projection = camera.matrices.perspective;
		uniformCamera.projectionInv = glm::inverse(camera.matrices.perspective);
		uniformCamera.position = camera.viewPos;

		memcpy(uboCamera.mapped, &uniformCamera, sizeof(uniformCamera));
	}

	void updateCarUniform() {
		glm::mat4 trans;
		trans = glm::translate(trans, glm::vec3(1500.0f, 10.0f, -4000.0f) + translate);
		trans = glm::rotate(trans, glm::radians(angle), axis);
		trans = glm::scale(trans, glm::vec3(scale));
		pcCar.model = trans;
	}

	void setupLayouts() {
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4)
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &dsLayout));

		// City
		{
			std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayout, scene.m_dsLayout };
			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

			VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantModel) };
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pLayouts.city));
		}

		// Car
		{
			std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayout, vkglTF::descriptorSetLayoutImage };
			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

			VkPushConstantRange pushConstantRange = { 
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantModel) + sizeof(vkglTF::PushConstBlockMaterial)
			};
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pLayouts.car));
		}

		// General
		{
			std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayout, lightSystem.m_dsLayout };
			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
				vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pLayouts.lighting));
		}
	}

	void prepareDescriptorSet() {
		// Descriptor pool
		//
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * 2),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * 2)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Allocate descriptor set
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayout, 1);

		// Geometry
		//
		{
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.geometry));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets.geometry, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uboCamera.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}

		// Lighting
		//
		{
			VkDescriptorImageInfo texDescriptorDepth =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[3].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT0 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[0].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT1 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[1].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT2 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[2].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.lighting));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uboCamera.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDepth),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorRT0),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorRT1),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &texDescriptorRT2)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}
	}

	void updateDescriptorSetOnResize() {
		// Lighting
		//
		{
			VkDescriptorImageInfo texDescriptorDepth =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[3].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT0 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[0].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT1 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[1].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			VkDescriptorImageInfo texDescriptorRT2 =
				vks::initializers::descriptorImageInfo(
					geometryPass->sampler,
					geometryPass->attachments[2].view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDepth),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorRT0),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorRT1),
				vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &texDescriptorRT2)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}
	}

	void preparePipeline() {
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		// Binding description
		std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(SimScene::Geometry::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
			vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE)
		};
		// Attribute descriptions
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, normal)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv0)),
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv1)),
			vks::initializers::vertexInputAttributeDescription(0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv2)),
			vks::initializers::vertexInputAttributeDescription(0, 5, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv3)),

			vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 6, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, pos))
		};

		inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(static_cast<uint32_t>(blendAttachmentStates.size()), blendAttachmentStates.data());
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShadersPath() + "loadPackage/spirv/deferredGeometryCity.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "loadPackage/spirv/deferredGeometryCity.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		struct SpecData {
			uint32_t texCount;
		} specData;
		specData.texCount = static_cast<uint32_t>(scene.textures.size());
		std::array<VkSpecializationMapEntry, 1> specMapEntries = {
			vks::initializers::specializationMapEntry(0, offsetof(SpecData, texCount), sizeof(SpecData::texCount))
		};
		VkSpecializationInfo specInfo = vks::initializers::specializationInfo(static_cast<uint32_t>(specMapEntries.size()), specMapEntries.data(), sizeof(SpecData), &specData);
		shaderStages[1].pSpecializationInfo = &specInfo;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pLayouts.city, geometryPass->renderPass, 0);
		pipelineCreateInfo.pVertexInputState = &inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.geometryCity));

		// Geometry Car
		//
		pipelineCreateInfo.layout = pLayouts.car;

		pipelineCreateInfo.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
			{
				vkglTF::VertexComponent::Position,
				vkglTF::VertexComponent::Normal,
				vkglTF::VertexComponent::UV
			}
		);

		shaderStages[0] = loadShader(getShadersPath() + "loadPackage/spirv/deferredGeometryCar.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "loadPackage/spirv/deferredGeometryCar.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.geometryCar));

		// Lighting
		//
		pipelineCreateInfo.layout = pLayouts.lighting;
		pipelineCreateInfo.renderPass = renderPass;

		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCreateInfo.pVertexInputState = &emptyInputState;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;

		colorBlendState.attachmentCount = 1;
		depthStencilState.depthTestEnable = VK_FALSE;

		shaderStages[0] = loadShader(getShadersPath() + "loadPackage/spirv/deferredLighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "loadPackage/spirv/deferredLighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.lighting));
	}

	virtual void buildCommandBuffers() override {
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[4];

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();

		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			GPUTimer.OnBeginFrame(drawCmdBuffers[i]);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			// Geometry
			//
			{
				renderPassBeginInfo.renderPass = geometryPass->renderPass;
				renderPassBeginInfo.framebuffer = geometryPass->framebuffer;

				renderPassBeginInfo.clearValueCount = 4;
				clearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearValues[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearValues[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearValues[3].depthStencil = { 1.0f, 0 };

				statistics.reset(drawCmdBuffers[i]);

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				statistics.begin(drawCmdBuffers[i]);

				// City
				{
					vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pLayouts.city, 0, 1, &descriptorSets.geometry, 0, NULL);
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometryCity);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

					PushConstantModel pc;
					pc.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
					vkCmdPushConstants(drawCmdBuffers[i], pLayouts.city, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantModel), &pc);

					scene.draw(drawCmdBuffers[i], pLayouts.city, instanceCount);

					GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
				}

				// Car
				{
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometryCar);
					vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pLayouts.car, 0, 1, &descriptorSets.geometry, 0, NULL);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
					if (model.indices.buffer != VK_NULL_HANDLE) {
						vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
					}

					vkCmdPushConstants(drawCmdBuffers[i], pLayouts.car, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantModel), &pcCar);

					for (auto node : model.nodes) {
						model.drawNode(
							node, drawCmdBuffers[i],
							vkglTF::RenderFlags::RenderOpaqueNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
							pLayouts.car, 1, sizeof(PushConstantModel)
						);
					}

					GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
				}

				statistics.end(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			// Light Culling
			//
			{
				//lightSystem.calculateFrustum(drawCmdBuffers[i]);
				//GPUTimer.NextTimeStamp(drawCmdBuffers[i]);

				//lightSystem.doLightCulling(drawCmdBuffers[i]);
				//GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// Lighting
			//
			{
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };

				{
					// depthStencil imageLayout transition
					VkImageMemoryBarrier imageMemoryBarrier = {};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
					imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
					imageMemoryBarrier.image = depthStencil.image;

					vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lighting);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pLayouts.lighting, 0, 1, &descriptorSets.lighting, 0, NULL);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pLayouts.lighting, 1, 1, &lightSystem.m_descriptorSet, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);

				drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			{
				// depthStencil imageLayout transition
				VkImageMemoryBarrier imageMemoryBarrier = {};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
				imageMemoryBarrier.image = depthStencil.image;

				vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			GPUTimer.NextTimeStamp(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void draw(){
		VulkanExampleBase::prepareFrame();

		// Command buffer to be submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();

		GPUTimer.GetQueryResult(&timeStamps);
		statistics.getResult();
	}

	void prepare() {
		VulkanExampleBase::prepare();

		std::vector<std::string> labels = {
			"Begin Frame",
			"Geometry City",
			"Geometry Car",
			//"Frustum Calculate",
			//"Light Culling",
			"Lighting",
			"ImGUI"
		};
		GPUTimer.OnCreate(vulkanDevice, std::move(labels));
		statistics.init(vulkanDevice);

		lightSystem.init(vulkanDevice, this, glm::vec3(1500.0f, 20.0f, -4000.0f), 50.0f, 10);
		loadAssets();
		preparePasses();
		prepareInstanceBuffer();
		prepareUniformBuffer();

		setupLayouts();
		preparePipeline();
		prepareDescriptorSet();

		buildCommandBuffers();
		prepared = true;
	}

	virtual void render(){
		if (!prepared) return;

		draw();
	}

	virtual void viewChanged(){
		updateCameraUniforms();
		lightSystem.updateCamera();
	}

	virtual void windowResized() override {
		// resource that have been recreated: depthStencil, swapchain image & its framebuffer

		// remaining part of the resources to be recreated
		// render target & its framebuffer
		recreatePasses();

		// descriptor set related with those rendertarget
		updateDescriptorSetOnResize();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay){
		ImGui::PushItemWidth(200.0f);
		if (overlay->header("Car")) {
			if (overlay->sliderFloat3("Translate", &translate.x, -20.0f, 20.0f)) {
				updateCarUniform();
			}
			if (overlay->sliderFloat("Angle", &angle, -180.0f, 180.0f)) {
				updateCarUniform();
			}
			if (overlay->sliderFloat3("Axis", &axis.x, -1.0f, 1.0f)) {
				updateCarUniform();
			}
			if (overlay->sliderFloat("Scale", &scale, 0.0f, 1.0f)) {
				updateCarUniform();
			}
		}
		if (overlay->header("Pipeline statistics")) {
			for (auto i = 0; i < statistics.pipelineStats.size(); i++) {
				std::string caption = statistics.pipelineStatNames[i] + ": %d";
				overlay->text(caption.c_str(), statistics.pipelineStats[i]);
			}
		}
		if (overlay->header("GPU Profile")) {
			for (auto& timeStamp : timeStamps) {
				ImGui::Text("%-22s: %7.1f", timeStamp.m_label.c_str(), timeStamp.m_microseconds);
			}
		}
		ImGui::PopItemWidth();
	}
};

VULKAN_EXAMPLE_MAIN()