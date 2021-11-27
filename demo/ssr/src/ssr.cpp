/*
* Vulkan Example - Deferred shading with multiple render targets (aka G-Buffer) example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include "VulkanFrameBuffer.hpp"

#include "DepthHierarchy.h"

#define ENABLE_VALIDATION true

class VulkanExample : public VulkanExampleBase
{
public:
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

	struct ScreenQuad{
		// Vertex layout for screen quad
		struct Vertex {
			glm::vec3 pos;
			glm::vec2 uv;
			glm::vec3 viewRay; // z-normalized view ray
		};
		vks::Buffer vertices;
		vks::Buffer indices;
		uint32_t indexCount;
		void destroy() {
			vertices.destroy();
			indices.destroy();
		}
	}screenQuad;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
	} uboVS;

	struct Light {
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct {
		Light lights[6];
		glm::mat4 view;
	} uboLighting;

	struct {
		glm::mat4 projection;

		// SSR settings
		float maxDistance = 4.0;
		float resolution = 0.1;
		float thickness = 0.2;
	}uboSsr;

	struct {
		int32_t size = 2;
		float roughness = 0.5;
	}uboComposition;

	struct {
		vks::Buffer vs; // used in vertex shader both in prez pass and direct lighting pass
		vks::Buffer lighting; // used in fragment shader in lighting pass
		vks::Buffer ssr;
		vks::Buffer composition;
	} uniformBuffers;

	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSetLayout dsLayoutDownsampleCS;
	VkPipelineLayout pipelineLayout;
	VkPipelineLayout ppLayoutDownsampleCS;

	VkDescriptorSet dsPrez;
	struct {
		VkDescriptorSet model;
		VkDescriptorSet floor;
	} dsLighting;
	VkDescriptorSet dsSsr;
	VkDescriptorSet dsComposition;

	struct {
		VkPipeline prez;
		VkPipeline lighting;
		VkPipeline ssr;
		VkPipeline composition;
		VkPipeline downsampleCS;
	} pipelines;

	struct {
		std::unique_ptr<vks::Framebuffer> prez;
		std::unique_ptr<vks::Framebuffer> lighting;
		std::unique_ptr<vks::Framebuffer> ssr;
	}passes;

	struct cbDownsample {
		float outputSize[2];
		float invInputSize[2];
		uint32_t slice;
		uint32_t padding[3];
	};
	DepthHierarchy depthHierarchy;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Deferred shading";
		//camera.flipY = true;
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 0.0f, 0.3f, -8.75f };
		//camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.prez, nullptr);
		vkDestroyPipeline(device, pipelines.lighting, nullptr);
		vkDestroyPipeline(device, pipelines.ssr, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);
		vkDestroyPipeline(device, pipelines.downsampleCS, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, ppLayoutDownsampleCS, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, dsLayoutDownsampleCS, nullptr);

		// Uniform buffers
		uniformBuffers.vs.destroy();
		uniformBuffers.lighting.destroy();
		uniformBuffers.ssr.destroy();
		uniformBuffers.composition.destroy();

		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.floor.colorMap.destroy();
		textures.floor.normalMap.destroy();

		screenQuad.destroy();
		depthHierarchy.destroy(device);
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

	void graphicsPassPrepare() {
		// prez pass
		//
		passes.prez = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);

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
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
		};
		depth.weakRef = true;
		passes.prez->attachments.push_back(depth);

		VK_CHECK_RESULT(passes.prez->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.prez->createRenderPass());

		// lighting pass
		//
		passes.lighting = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);

		// Normal
		passes.lighting->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		// Color
		passes.lighting->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		depth.description.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.description.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		depth.description.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		passes.lighting->attachments.push_back(depth);

		VK_CHECK_RESULT(passes.lighting->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.lighting->createRenderPass());

		// ssr pass
		//
		passes.ssr = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);

		// Reflect Color
		passes.ssr->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		VK_CHECK_RESULT(passes.ssr->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.ssr->createRenderPass());
	}

	virtual void setupDepthStencil() override {
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
	}

	virtual void setupRenderPass() override {
		VkAttachmentDescription attachment = {};
		// swap chain image
		attachment.format = swapChain.colorFormat;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// composition subpass
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

	void prepareScreenQuad() {
		glm::vec3 clipCorners[4] = {
			{  1.0f, -1.0f, 0.0f },
			{ -1.0f, -1.0f, 0.0f },
			{ -1.0f,  1.0f, 0.0f },
			{  1.0f,  1.0f, 0.0f }
		};
		auto inverseProj = glm::inverse(camera.matrices.perspective);
		glm::vec4 viewRays[4] = {};
		for (uint32_t i = 0; i < 4; ++i) {
			viewRays[i] = inverseProj * glm::vec4(clipCorners[i], 1.0);
			viewRays[i] /= viewRays[i].w;
			viewRays[i] /= viewRays[i].z;
		}
		// Setup vertices for a single uv-mapped quad made from two triangles
		std::vector<ScreenQuad::Vertex> vertices =
		{
			{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }, glm::vec3(viewRays[0]) },
			{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }, glm::vec3(viewRays[1]) },
			{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f }, glm::vec3(viewRays[2]) },
			{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f }, glm::vec3(viewRays[3]) }
		};

		// Setup indices
		std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
		screenQuad.indexCount = static_cast<uint32_t>(indices.size());

		// Create buffers
		// For the sake of simplicity we won't stage the vertex data to the gpu memory
		// Vertex buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&screenQuad.vertices,
			vertices.size() * sizeof(ScreenQuad::Vertex),
			vertices.data()));
		// Index buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&screenQuad.indices,
			indices.size() * sizeof(uint32_t),
			indices.data()));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Offscreen vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.vs,
			sizeof(uboVS)));

		// Deferred fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.lighting,
			sizeof(uboLighting)));

		// SSR fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssr,
			sizeof(uboSsr)));

		// Composition fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.composition,
			sizeof(uboComposition)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.vs.map());
		VK_CHECK_RESULT(uniformBuffers.lighting.map());
		VK_CHECK_RESULT(uniformBuffers.ssr.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());

		// Setup instanced model positions
		uboVS.instancePos[0] = glm::vec4(0.0f);
		uboVS.instancePos[1] = glm::vec4(-4.0f, 0.0, -4.0f, 0.0f);
		uboVS.instancePos[2] = glm::vec4(4.0f, 0.0, -4.0f, 0.0f);

		// Update
		updateUniformBufferVS();
		updateUniformBufferLighting();
		updateUniformBufferSsr();
		updateUniformBufferComposition();
	}

	// Update matrices used for the offscreen rendering of the scene
	void updateUniformBufferVS()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.view = camera.matrices.view;
		uboVS.model = glm::mat4(1.0f);
		memcpy(uniformBuffers.vs.mapped, &uboVS, sizeof(uboVS));
	}

	// Update lights and parameters passed to the composition shaders
	void updateUniformBufferLighting()
	{
		// White
		uboLighting.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uboLighting.lights[0].color = glm::vec3(1.5f);
		uboLighting.lights[0].radius = 15.0f * 0.25f;
		// Red
		uboLighting.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uboLighting.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uboLighting.lights[1].radius = 15.0f;
		// Blue
		uboLighting.lights[2].position = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
		uboLighting.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uboLighting.lights[2].radius = 5.0f;
		// Yellow
		uboLighting.lights[3].position = glm::vec4(0.0f, -0.9f, 0.5f, 0.0f);
		uboLighting.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uboLighting.lights[3].radius = 2.0f;
		// Green
		uboLighting.lights[4].position = glm::vec4(0.0f, -0.5f, 0.0f, 0.0f);
		uboLighting.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uboLighting.lights[4].radius = 5.0f;
		// Yellow
		uboLighting.lights[5].position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
		uboLighting.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uboLighting.lights[5].radius = 25.0f;

		uboLighting.lights[0].position.x = sin(glm::radians(360.0f * timer)) * 5.0f;
		uboLighting.lights[0].position.z = cos(glm::radians(360.0f * timer)) * 5.0f;

		uboLighting.lights[1].position.x = -4.0f + sin(glm::radians(360.0f * timer) + 45.0f) * 2.0f;
		uboLighting.lights[1].position.z = 0.0f + cos(glm::radians(360.0f * timer) + 45.0f) * 2.0f;

		uboLighting.lights[2].position.x = 4.0f + sin(glm::radians(360.0f * timer)) * 2.0f;
		uboLighting.lights[2].position.z = 0.0f + cos(glm::radians(360.0f * timer)) * 2.0f;

		uboLighting.lights[4].position.x = 0.0f + sin(glm::radians(360.0f * timer + 90.0f)) * 5.0f;
		uboLighting.lights[4].position.z = 0.0f - cos(glm::radians(360.0f * timer + 45.0f)) * 5.0f;

		uboLighting.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f * timer + 135.0f)) * 10.0f;
		uboLighting.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f * timer - 45.0f)) * 10.0f;

		// Current view position
		uboLighting.view = camera.matrices.view;

		memcpy(uniformBuffers.lighting.mapped, &uboLighting, sizeof(uboLighting));
	}

	void updateUniformBufferSsr() {
		uboSsr.projection = camera.matrices.perspective;
		memcpy(uniformBuffers.ssr.mapped, &uboSsr, sizeof(uboSsr));
	}

	void updateUniformBufferComposition() {
		memcpy(uniformBuffers.composition.mapped, &uboComposition, sizeof(uboComposition));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1 : Color map / Normal buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2 : Normal map / Depth buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 3 : Direct lighting buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			// Binding 4 : Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Shared pipeline layout used by all pipelines
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// downsampler compute pipeline
		// create the descriptor set layout
		// the shader needs
		// source image: texture + sampler
		// destination image: storage image
		// single pass: destination images count # of mips
		// single pass: global atomic counter, storage buffer
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				// Binding 0 : 
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0),
				// Binding 1 : 
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
				// Binding 2 : 
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 2)
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &dsLayoutDownsampleCS));

			VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
			pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

			// push constants: input size, inverse output size
			VkPushConstantRange pushConstantRange = {};
			pushConstantRange.offset = 0;
			pushConstantRange.size = sizeof(cbDownsample);
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
			pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

			pPipelineLayoutCreateInfo.setLayoutCount = 1;
			pPipelineLayoutCreateInfo.pSetLayouts = &dsLayoutDownsampleCS;

			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, NULL, &ppLayoutDownsampleCS));
		}
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

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo();
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		pipelineCI.layout = pipelineLayout;
		pipelineCI.subpass = 0;

		// prez pass
		//
		pipelineCI.renderPass = passes.prez->renderPass;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position });

		shaderStages[0] = loadShader(getShadersPath() + "ssr/spirv/prez.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/spirv/prez.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.prez));

		// direct lighting pass
		//
		pipelineCI.renderPass = passes.lighting->renderPass;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({
			vkglTF::VertexComponent::Position,
			vkglTF::VertexComponent::UV,
			vkglTF::VertexComponent::Normal,
			vkglTF::VertexComponent::Tangent
			});
		std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();
		depthStencilState.depthWriteEnable = VK_FALSE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_EQUAL;

		shaderStages[0] = loadShader(getShadersPath() + "ssr/spirv/lighting.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/spirv/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lighting));

		// ssr pass
		//
		pipelineCI.renderPass = passes.ssr->renderPass;
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		// Binding description
		std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(ScreenQuad::Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		// Attribute descriptions
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ScreenQuad::Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT,offsetof(ScreenQuad::Vertex, uv)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ScreenQuad::Vertex, viewRay))
		};
		inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();
		pipelineCI.pVertexInputState = &inputState;

		colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		pipelineCI.pDepthStencilState = nullptr;

		shaderStages[0] = loadShader(getShadersPath() + "ssr/spirv/ssr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/spirv/ssr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssr));

		// composition pass
		//
		pipelineCI.renderPass = renderPass;
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		shaderStages[0] = loadShader(getShadersPath() + "ssr/spirv/composition.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "ssr/spirv/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		// compute pipeline
		// downsampler pipeline
		{
			VkPipelineShaderStageCreateInfo shader = loadShader(getShadersPath() + "ssr/spirv/downsamplerCS.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

			VkComputePipelineCreateInfo pipeline = {};
			pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
			pipeline.layout = ppLayoutDownsampleCS;
			pipeline.stage = shader;
			pipeline.basePipelineHandle = VK_NULL_HANDLE;
			pipeline.basePipelineIndex = 0;

			VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &pipeline, NULL, &pipelines.downsampleCS));
		}
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 7),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DepthHierarchy::MAX_MIP_LEVEL),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_SAMPLER, DepthHierarchy::MAX_MIP_LEVEL),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DepthHierarchy::MAX_MIP_LEVEL)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 5 + DepthHierarchy::MAX_MIP_LEVEL);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSet()
	{
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		/*
			Pre-z pass
		*/
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsPrez));
		writeDescriptorSets = {
			// Binding 0: Transform
			vks::initializers::writeDescriptorSet(dsPrez, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.vs.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Direct lighting pass
		*/
		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsLighting.model));
		writeDescriptorSets = {
			// Binding 0: Transform
			vks::initializers::writeDescriptorSet(dsLighting.model, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.vs.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(dsLighting.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.model.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(dsLighting.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.model.normalMap.descriptor),
			// Binding 4: Light info
			vks::initializers::writeDescriptorSet(dsLighting.model, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.lighting.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Floor
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsLighting.floor));
		writeDescriptorSets = {
			// Binding 0: Transform
			vks::initializers::writeDescriptorSet(dsLighting.floor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.vs.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(dsLighting.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.floor.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(dsLighting.floor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.floor.normalMap.descriptor),
			// Binding 4: Light calculation info
			vks::initializers::writeDescriptorSet(dsLighting.floor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.lighting.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			SSR pass
		*/
		VkDescriptorImageInfo descriptorNormal = { passes.lighting->sampler, passes.lighting->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorDepth = { passes.prez->sampler, depthHierarchy.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorDirect = { passes.lighting->sampler, passes.lighting->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsSsr));
		writeDescriptorSets = {
			// Binding 1: Normal buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &descriptorNormal),
			// Binding 2: Depth buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorDepth),
			// Binding 3: Direct lighting buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &descriptorDirect),
			// Binding 4: SSR calculation info
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssr.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Composition pass
		*/
		VkDescriptorImageInfo descriptorReflect = { passes.ssr->sampler, passes.ssr->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsComposition));
		writeDescriptorSets = {
			// Binding 1: Direct lighting buffer
			vks::initializers::writeDescriptorSet(dsComposition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &descriptorDirect),
			// Binding 2: Reflect color buffer
			vks::initializers::writeDescriptorSet(dsComposition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorReflect),
			// Binding 4: composition info (blur + blend)
			vks::initializers::writeDescriptorSet(dsComposition, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.composition.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Downsample pass
		*/
		{
			allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayoutDownsampleCS, 1);
			assert(depthHierarchy.mipLevel != -1);
			for (int i = 0; i < depthHierarchy.mipLevel; ++i) {
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &depthHierarchy.descriptorSets[i]));

				VkDescriptorImageInfo descriptorSrc = { VK_NULL_HANDLE, i == 0 ? depthStencil.view : depthHierarchy.splitViews[i - 1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
				VkDescriptorImageInfo descriptorSampler;
				descriptorSampler.sampler = depthHierarchy.sampler;
				VkDescriptorImageInfo descriptorDst = { VK_NULL_HANDLE, depthHierarchy.splitViews[i], VK_IMAGE_LAYOUT_GENERAL };

				writeDescriptorSets = {
					vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0, &descriptorSrc),
					vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_SAMPLER, 1, &descriptorSampler),
					vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, &descriptorDst)
				};
				vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			}
		}
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		std::array<VkClearValue, 3> clearValues;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;	
		renderPassBeginInfo.pClearValues = clearValues.data();

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			/*
				prez pass
			*/
			{
				renderPassBeginInfo.renderPass = passes.prez->renderPass;
				renderPassBeginInfo.framebuffer = passes.prez->framebuffer;

				clearValues[0].depthStencil = { 1.0f, 0 };
				renderPassBeginInfo.clearValueCount = 1;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsPrez, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.prez);

				// Models
				models.model.bindBuffers(drawCmdBuffers[i]);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indices.count, 3, 0, 0, 0);
				// Floor
				models.floor.draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			/*
				direct lighting pass
			*/
			{
				renderPassBeginInfo.renderPass = passes.lighting->renderPass;
				renderPassBeginInfo.framebuffer = passes.lighting->framebuffer;

				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clearValues[2].depthStencil = { 1.0f, 0 };
				renderPassBeginInfo.clearValueCount = 3;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lighting);

				// Models
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsLighting.model, 0, nullptr);
				models.model.bindBuffers(drawCmdBuffers[i]);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.model.indices.count, 3, 0, 0, 0);
				// Floor
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsLighting.floor, 0, nullptr);
				models.floor.draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			/*
				downsamplerCS
			*/
			{
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.downsampleCS);

				// depthHierarchy[0 ~ mipLevel-1]: VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_GENERAL
				VkImageMemoryBarrier imageMemoryBarrier = {};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
				imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, depthHierarchy.mipLevel, 0, 1 };
				imageMemoryBarrier.image = depthHierarchy.image;

				vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

				for (uint32_t j = 0; j < depthHierarchy.mipLevel; j++)
				{
					uint32_t dispatchX, dispatchY, dispatchZ;
					if (j == 0) {
						dispatchX = (width + 7) / 8;
						dispatchY = (height + 7) / 8;
						dispatchZ = 1;
					}
					else {
						dispatchX = ((width >> j) + 7) / 8;
						dispatchY = ((height >> j) + 7) / 8;
						dispatchZ = 1;
					}

					vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_COMPUTE, ppLayoutDownsampleCS, 0, 1, &depthHierarchy.descriptorSets[j], 0, nullptr);

					// Bind push constants
					//
					cbDownsample data;
					data.slice = j;
					if (j == 0) {
						data.outputSize[0] = (float)(width);
						data.outputSize[1] = (float)(height);
						data.invInputSize[0] = 1.0f / (float)(width);
						data.invInputSize[1] = 1.0f / (float)(height);
					}
					else {
						data.outputSize[0] = (float)(width >> j);
						data.outputSize[1] = (float)(height >> j);
						data.invInputSize[0] = 1.0f / (float)(width >> (j - 1));
						data.invInputSize[1] = 1.0f / (float)(height >> (j - 1));
					}
					vkCmdPushConstants(drawCmdBuffers[i], ppLayoutDownsampleCS, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cbDownsample), (void*)&data);

					// Draw
					//
					vkCmdDispatch(drawCmdBuffers[i], dispatchX, dispatchY, dispatchZ);

					// depthHierarchy[0 ~ mipLevel-1]: VK_IMAGE_LAYOUT_GENERAL -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, j, 1, 0, 1 };
					imageMemoryBarrier.image = depthHierarchy.image;

					// transition general layout if destination image to shader read only for source image
					vkCmdPipelineBarrier(drawCmdBuffers[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			/*
				ssr pass
			*/
			{
				renderPassBeginInfo.renderPass = passes.ssr->renderPass;
				renderPassBeginInfo.framebuffer = passes.ssr->framebuffer;

				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				renderPassBeginInfo.clearValueCount = 1;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &screenQuad.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], screenQuad.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsSsr, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssr);

				vkCmdDrawIndexed(drawCmdBuffers[i], screenQuad.indexCount, 1, 0, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			/*
				composition pass
			*/
			{
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];

				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				renderPassBeginInfo.clearValueCount = 1;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsComposition, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);

				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

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
		prepareScreenQuad();
		prepareUniformBuffers();

		depthHierarchy.init(vulkanDevice, width, height, VK_FORMAT_R32_SFLOAT);

		graphicsPassPrepare();

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
			updateUniformBufferLighting();
		}
		if (camera.updated)
		{
			updateUniformBufferVS();
			updateUniformBufferLighting();
			updateUniformBufferSsr();
		}
	}

	virtual void windowResized() override {
		// resource that have been recreated: depthStencil, swapchain image & its framebuffer

		depthHierarchy.recreateOnResize(vulkanDevice, width, height, VK_FORMAT_R32_SFLOAT);

		// remaining part of the resources to be recreated
		// render target & its framebuffer
		recreateIntermediateFramebuffer();

		// descriptor set related with those rendertarget
		updateDescriptorSetOnResize();
	}

	void recreateIntermediateFramebuffer() {
		// prez pass
		//
		passes.prez->clearBeforeRecreate(width, height);

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
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
		};
		depth.weakRef = true;
		passes.prez->attachments.push_back(depth);

		passes.prez->createFrameBuffer();

		// lighting pass
		//
		passes.lighting->clearBeforeRecreate(width, height);

		// Normal
		passes.lighting->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		// Color
		passes.lighting->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		depth.description.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.description.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		depth.description.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		passes.lighting->attachments.push_back(depth);

		passes.lighting->createFrameBuffer();

		// ssr pass
		//
		passes.ssr->clearBeforeRecreate(width, height);

		// Reflect Color
		passes.ssr->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		passes.ssr->createFrameBuffer();
	}

	void updateDescriptorSetOnResize() {
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		/*
			Downsample pass
		*/
		for (int i = 0; i < depthHierarchy.mipLevel; ++i) {
			VkDescriptorImageInfo descriptorSrc = { VK_NULL_HANDLE, i == 0 ? depthStencil.view : depthHierarchy.splitViews[i - 1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo descriptorSampler;
			descriptorSampler.sampler = depthHierarchy.sampler;
			VkDescriptorImageInfo descriptorDst = { VK_NULL_HANDLE, depthHierarchy.splitViews[i], VK_IMAGE_LAYOUT_GENERAL };

			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0, &descriptorSrc),
				vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_SAMPLER, 1, &descriptorSampler),
				vks::initializers::writeDescriptorSet(depthHierarchy.descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, &descriptorDst)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}

		/*
			SSR pass
		*/
		VkDescriptorImageInfo descriptorNormal = { passes.lighting->sampler, passes.lighting->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorDepth = { passes.prez->sampler, depthHierarchy.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorDirect = { passes.lighting->sampler, passes.lighting->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		writeDescriptorSets = {
			// Binding 1: Normal buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &descriptorNormal),
			// Binding 2: Depth buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorDepth),
			// Binding 3: Direct lighting buffer
			vks::initializers::writeDescriptorSet(dsSsr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &descriptorDirect),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			Composition pass
		*/
		VkDescriptorImageInfo descriptorReflect = { passes.ssr->sampler, passes.ssr->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		writeDescriptorSets = {
			// Binding 1: Direct lighting buffer
			vks::initializers::writeDescriptorSet(dsComposition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &descriptorDirect),
			// Binding 2: Reflect color buffer
			vks::initializers::writeDescriptorSet(dsComposition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorReflect),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("SSR Settings")) {
			if (overlay->sliderFloat("Max Distance", &uboSsr.maxDistance, 0.0f, 5.0)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Resolution", &uboSsr.resolution, 0.0f, 1.0)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Thickness", &uboSsr.thickness, 0.0f, 2.0)) {
				updateUniformBufferSsr();
			}
		}
		if (overlay->header("Composition Settings")) {
			if (overlay->sliderInt("Blur Size", &uboComposition.size, 0, 5)) {
				updateUniformBufferComposition();
			}
			if (overlay->sliderFloat("Roughness", &uboComposition.roughness, 0.0, 1.0)) {
				updateUniformBufferComposition();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
