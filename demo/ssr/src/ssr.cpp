/*
* Vulkan Example - Deferred shading with multiple render targets (aka G-Buffer) example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION true

class VulkanExample : public VulkanExampleBase
{
public:
	int32_t debugDisplayTarget = 0;

	struct {
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} model;
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} floor;
	} textures;

	struct {
		vkglTF::Model model;
		vkglTF::Model floor;
	} models;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
	} uboOffscreenVS;

	struct Light {
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct {
		Light lights[6];
		glm::vec4 viewPos;
		int debugDisplayTarget = 0;
	} uboComposition;

	struct {
		vks::Buffer offscreen;
		vks::Buffer composition;
	} uniformBuffers;

	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	VkDescriptorSet dsPrez;
	struct {
		VkDescriptorSet model;
		VkDescriptorSet floor;
	} dsGeometry;
	VkDescriptorSet dsLighting;

	struct {
		VkPipeline prez;
		VkPipeline geometry;
		VkPipeline lighting;
	} pipelines;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
	};
	FrameBufferAttachment positionFBA, normalFBA, colorFBA; // gBuffers

	// One sampler for the frame buffer color attachments
	VkSampler colorSampler;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Deferred shading";
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
		UIOverlay.subpass = 2;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class

		vkDestroySampler(device, colorSampler, nullptr);

		// Frame buffer

		// Color attachments
		vkDestroyImageView(device, positionFBA.view, nullptr);
		vkDestroyImage(device, positionFBA.image, nullptr);
		vkFreeMemory(device, positionFBA.mem, nullptr);

		vkDestroyImageView(device, normalFBA.view, nullptr);
		vkDestroyImage(device, normalFBA.image, nullptr);
		vkFreeMemory(device, normalFBA.mem, nullptr);

		vkDestroyImageView(device, colorFBA.view, nullptr);
		vkDestroyImage(device, colorFBA.image, nullptr);
		vkFreeMemory(device, colorFBA.mem, nullptr);

		vkDestroyPipeline(device, pipelines.prez, nullptr);
		vkDestroyPipeline(device, pipelines.geometry, nullptr);
		vkDestroyPipeline(device, pipelines.lighting, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		// Uniform buffers
		uniformBuffers.offscreen.destroy();
		uniformBuffers.composition.destroy();

		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.floor.colorMap.destroy();
		textures.floor.normalMap.destroy();
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	// Create a frame buffer attachment
	void createAttachment(VkFormat format, VkImageUsageFlags usage, uint32_t width, uint32_t height, FrameBufferAttachment *attachment) {
		VkImageAspectFlags aspectMask = 0;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));

		VkImageViewCreateInfo imageView = vks::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	void setupFBAs() {
		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
			width, height,
			&positionFBA
		);

		createAttachment(
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
			width, height,
			&normalFBA
		);

		createAttachment(
			VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
			width, height,
			&colorFBA
		);

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorSampler));
	}

	virtual void setupRenderPass() override {
		setupFBAs();

		std::array<VkAttachmentDescription, 5> attachments = {};
		// swap chain image
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		// position target
		attachments[1].format = positionFBA.format;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// normal target
		attachments[2].format = normalFBA.format;
		attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// color target
		attachments[3].format = colorFBA.format;
		attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// depth
		attachments[4].format = depthFormat;
		attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		// pre-z subpass
		VkAttachmentReference prezDepthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		// geometry subpass
		std::array<VkAttachmentReference, 3> gColorRefs = {{
			{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
			{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
		}};
		VkAttachmentReference gDepthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

		// lighting subpass
		VkAttachmentReference lightColorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		std::array<VkAttachmentReference, 3> lightInputRefs = { {
			{ 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			{ 3, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
		} };

		std::array<VkSubpassDescription, 3> subpassDescriptions = {};
		subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[0].colorAttachmentCount = 0;
		subpassDescriptions[0].pColorAttachments = nullptr;
		subpassDescriptions[0].pDepthStencilAttachment = &prezDepthRef;
		subpassDescriptions[0].inputAttachmentCount = 0;
		subpassDescriptions[0].pInputAttachments = nullptr;
		subpassDescriptions[0].preserveAttachmentCount = 0;
		subpassDescriptions[0].pPreserveAttachments = nullptr;
		subpassDescriptions[0].pResolveAttachments = nullptr;

		subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[1].colorAttachmentCount = static_cast<uint32_t>(gColorRefs.size());
		subpassDescriptions[1].pColorAttachments = gColorRefs.data();
		subpassDescriptions[1].pDepthStencilAttachment = &gDepthRef;
		subpassDescriptions[1].inputAttachmentCount = 0;
		subpassDescriptions[1].pInputAttachments = nullptr;
		subpassDescriptions[1].preserveAttachmentCount = 0;
		subpassDescriptions[1].pPreserveAttachments = nullptr;
		subpassDescriptions[1].pResolveAttachments = nullptr;

		subpassDescriptions[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[2].colorAttachmentCount = 1;
		subpassDescriptions[2].pColorAttachments = &lightColorRef;
		subpassDescriptions[2].pDepthStencilAttachment = nullptr;
		subpassDescriptions[2].inputAttachmentCount = static_cast<uint32_t>(lightInputRefs.size());
		subpassDescriptions[2].pInputAttachments = lightInputRefs.data();
		subpassDescriptions[2].preserveAttachmentCount = 0;
		subpassDescriptions[2].pPreserveAttachments = nullptr;
		subpassDescriptions[2].pResolveAttachments = nullptr;

		// Subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 3> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 2;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = 0;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = 0;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[2].srcSubpass = 1;
		dependencies[2].dstSubpass = 2;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassInfo.pSubpasses = subpassDescriptions.data();
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	}

	virtual void setupFrameBuffer() override {
		std::array<VkImageView, 5> attachments;
		attachments[1] = positionFBA.view;
		attachments[2] = normalFBA.view;
		attachments[3] = colorFBA.view;
		attachments[4] = depthStencil.view;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		frameBufferCreateInfo.pAttachments = attachments.data();
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;

		// Create frame buffers for every swap chain image
		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			attachments[0] = swapChain.buffers[i].view;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.model.loadFromFile(getAssetPath() + "models/armor/armor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.floor.loadFromFile(getAssetPath() + "models/deferred_floor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.model.colorMap.loadFromFile(getAssetPath() + "models/armor/colormap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.model.normalMap.loadFromFile(getAssetPath() + "models/armor/normalmap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.floor.colorMap.loadFromFile(getAssetPath() + "textures/stonefloor01_color_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.floor.normalMap.loadFromFile(getAssetPath() + "textures/stonefloor01_normal_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Offscreen vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.offscreen,
			sizeof(uboOffscreenVS)));

		// Deferred fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.composition,
			sizeof(uboComposition)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.offscreen.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());

		// Setup instanced model positions
		uboOffscreenVS.instancePos[0] = glm::vec4(0.0f);
		uboOffscreenVS.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
		uboOffscreenVS.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);

		// Update
		updateUniformBufferOffscreen();
		updateUniformBufferComposition();
	}

	// Update matrices used for the offscreen rendering of the scene
	void updateUniformBufferOffscreen()
	{
		uboOffscreenVS.projection = camera.matrices.perspective;
		uboOffscreenVS.view = camera.matrices.view;
		uboOffscreenVS.model = glm::mat4(1.0f);
		memcpy(uniformBuffers.offscreen.mapped, &uboOffscreenVS, sizeof(uboOffscreenVS));
	}

	// Update lights and parameters passed to the composition shaders
	void updateUniformBufferComposition()
	{
		// White
		uboComposition.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uboComposition.lights[0].color = glm::vec3(1.5f);
		uboComposition.lights[0].radius = 15.0f * 0.25f;
		// Red
		uboComposition.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uboComposition.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uboComposition.lights[1].radius = 15.0f;
		// Blue
		uboComposition.lights[2].position = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
		uboComposition.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uboComposition.lights[2].radius = 5.0f;
		// Yellow
		uboComposition.lights[3].position = glm::vec4(0.0f, -0.9f, 0.5f, 0.0f);
		uboComposition.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uboComposition.lights[3].radius = 2.0f;
		// Green
		uboComposition.lights[4].position = glm::vec4(0.0f, -0.5f, 0.0f, 0.0f);
		uboComposition.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uboComposition.lights[4].radius = 5.0f;
		// Yellow
		uboComposition.lights[5].position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
		uboComposition.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uboComposition.lights[5].radius = 25.0f;

		uboComposition.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
		uboComposition.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;

		uboComposition.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
		uboComposition.lights[1].position.z = 0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;

		uboComposition.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
		uboComposition.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;

		uboComposition.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
		uboComposition.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;

		uboComposition.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
		uboComposition.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;

		// Current view position
		uboComposition.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		uboComposition.debugDisplayTarget = debugDisplayTarget;

		memcpy(uniformBuffers.composition.mapped, &uboComposition, sizeof(uboComposition));
	}

	void setupDescriptorSetLayout()
	{
		// Deferred shading layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1 : Scene colormap
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2 : Scene normalmap
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 3 : Position texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			// Binding 4 : Normals texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
			// Binding 5 : Albedo texture target
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_SHADER_STAGE_FRAGMENT_BIT, 5),
			// Binding 6 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 6),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Shared pipeline layout used by all pipelines
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// prez subpass pipeline
		pipelineCI.subpass = 0;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position });

		shaderStages[0] = loadShader(getShadersPath() + "ssr/prez.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/prez.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.prez));

		// geometry subpass pipeline
		pipelineCI.subpass = 1;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({
			vkglTF::VertexComponent::Position,
			vkglTF::VertexComponent::UV,
			vkglTF::VertexComponent::Color,
			vkglTF::VertexComponent::Normal,
			vkglTF::VertexComponent::Tangent
			});
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();
		depthStencilState.depthWriteEnable = VK_FALSE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_EQUAL;

		shaderStages[0] = loadShader(getShadersPath() + "ssr/geometry.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/geometry.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.geometry));

		// lighting subpass pipeline
		pipelineCI.subpass = 2;
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCI.pDepthStencilState = nullptr;

		shaderStages[0] = loadShader(getShadersPath() + "ssr/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lighting));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 3)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSet()
	{
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		/*
			Pre-z subpass
		*/
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsPrez));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(dsPrez, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.offscreen.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Geometry subpass
		*/
		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsGeometry.model));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(dsGeometry.model, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.offscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(dsGeometry.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.model.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(dsGeometry.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.model.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Floor
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsGeometry.floor));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(dsGeometry.floor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.offscreen.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(dsGeometry.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.floor.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(dsGeometry.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.floor.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Lighting subpass
		*/
		// Image descriptors for the offscreen color attachments
		VkDescriptorImageInfo texDescriptorPosition = { VK_NULL_HANDLE, positionFBA.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo texDescriptorNormal = { VK_NULL_HANDLE, normalFBA.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo texDescriptorAlbedo = { VK_NULL_HANDLE, colorFBA.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		// Deferred composition
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsLighting));
		writeDescriptorSets = {
			// Binding 1 : Position texture target
			vks::initializers::writeDescriptorSet(dsLighting, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 3, &texDescriptorPosition),
			// Binding 2 : Normals texture target
			vks::initializers::writeDescriptorSet(dsLighting, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 4, &texDescriptorNormal),
			// Binding 3 : Albedo texture target
			vks::initializers::writeDescriptorSet(dsLighting, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 5, &texDescriptorAlbedo),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(dsLighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6, &uniformBuffers.composition.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		std::array<VkClearValue, 5> clearValues;
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			/*
				subpass 0: prez
			*/
			{
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsPrez, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.prez);

				// Models
				models.model.bindBuffers(drawCmdBuffers[i]);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indices.count, 3, 0, 0, 0);
				// Floor
				models.floor.draw(drawCmdBuffers[i]);
			}

			/*
				subpass 1: geometry
			*/
			{
				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometry);

				// Models
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsGeometry.model, 0, nullptr);
				models.model.bindBuffers(drawCmdBuffers[i]);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indices.count, 3, 0, 0, 0);
				// Floor
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsGeometry.floor, 0, nullptr);
				models.floor.draw(drawCmdBuffers[i]);
			}

			/*
				subpass 2: lighting
			*/
			{
				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsLighting, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lighting);

				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();

		setupDescriptorSetLayout();
		preparePipelines();

		setupDescriptorPool();
		setupDescriptorSet();

		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		if (!paused)
		{
			updateUniformBufferComposition();
		}
		if (camera.updated)
		{
			updateUniformBufferOffscreen();	
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->comboBox("Display", &debugDisplayTarget, {"Final composition", "Position", "Normals", "Albedo", "Specular" }))
			{
				updateUniformBufferComposition();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
