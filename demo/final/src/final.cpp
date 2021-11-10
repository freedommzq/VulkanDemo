/*
* Vulkan Example - Deferred shading with shadows from multiple light sources using geometry shader instancing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanFrameBuffer.hpp"
#include "VulkanglTFModel.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

#define SSAO_KERNEL_SIZE 32
#define SSAO_NOISE_DIM 4

// Shadowmap properties
#if defined(__ANDROID__)
#define SHADOWMAP_DIM 1024
#else
#define SHADOWMAP_DIM 2048
#endif
// 16 bits of depth is enough for such a small scene
#define SHADOWMAP_FORMAT VK_FORMAT_D32_SFLOAT_S8_UINT

#if defined(__ANDROID__)
// Use max. screen dimension as deferred framebuffer size
#define FB_DIM std::max(width,height)
#else
#define FB_DIM 2048
#define SSAO_FB_DIM 512
#define LIGHTING_FB_DIM 2048
#define SSR_FB_DIM 512
#endif

// Must match the LIGHT_COUNT define in the shadow and deferred shaders
#define LIGHT_COUNT 3

class VulkanExample : public VulkanExampleBase
{
public:
	// Keep depth range as small as possible
	// for better shadow map precision
	float zNear = 0.1f;
	float zFar = 64.0f;
	float lightFOV = 100.0f;

	// Depth bias (and slope) are used to avoid shadowing artifacts
	float depthBiasConstant = 1.25f;
	float depthBiasSlope = 1.75f;

	struct {
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} model;
		struct {
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} background;

		vks::Texture2D ssaoNoise;
	} textures;

	struct {
		vkglTF::Model model;
		vkglTF::Model background;
	} models;

	/*
		UNIFORM BUFFER
	*/
	struct {
		vks::Buffer shadowGeometryShader;
		vks::Buffer geometry;
		vks::Buffer ssao;
		vks::Buffer ssaoBlur;
		vks::Buffer lighting;
		vks::Buffer ssr;
		vks::Buffer composition;
	} uniformBuffers;

	// This UBO stores the shadow matrices for all of the light sources
	// The matrices are indexed using geometry shader instancing
	// The instancePos is used to place the models using instanced draws
	struct {
		glm::mat4 mvp[LIGHT_COUNT];
		glm::vec4 instancePos[3];
	} uboShadow;
	
	// Geometry Pass (generate g-buffer)
	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
		int layer;
	} uboGeometry;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		alignas(4) float radius = 0.5f;
		alignas(4) float bias = 0.04f;
		alignas(16) glm::vec4 kernel[SSAO_KERNEL_SIZE];
	} uboSsao;

	struct {
		int32_t size = 1;
	} uboSsaoBlur;

	// Direct Lighting Pass (generate direct scene color)
	struct Light {
		glm::vec4 position;
		glm::vec4 target;
		glm::vec4 color;
		glm::mat4 viewMatrix;
	};
	struct {
		glm::vec4 viewPos;
		Light lights[LIGHT_COUNT];
		int32_t useShadow = 1;
		int32_t useSsao = 1;
	} uboDirectLighting;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 viewPos;

		// SSR settings
		float maxDistance = 4.0;
		float resolution = 0.1;
		float thickness = 0.2;
	}uboSsr;

	struct {
		int32_t ssrBlurSize = 1;
		float blendFactor = 0.2;
		float exposure = 1.0;
	}uboComposition;

	/*
		PIPELINE
	*/
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	struct {
		VkPipeline shadowpass;
		VkPipeline geometry;
		VkPipeline ssao;
		VkPipeline ssaoBlur;
		VkPipeline lighting;
		VkPipeline ssr;
		VkPipeline composition;
	} pipelines;

	struct {
		VkDescriptorSet shadow;
		VkDescriptorSet model;
		VkDescriptorSet background;
		VkDescriptorSet ssao;
		VkDescriptorSet ssaoBlur;
		VkDescriptorSet lighting;
		VkDescriptorSet ssr;
	} descriptorSets;
	VkDescriptorSet descriptorSet; // composition

	struct {
		// Framebuffer resources for the deferred pass
		vks::Framebuffer *geometry;
		// Framebuffer resources for the shadow pass
		vks::Framebuffer *shadow;

		vks::Framebuffer *ssao;
		vks::Framebuffer *ssaoBlur;
		vks::Framebuffer *lighting;
		vks::Framebuffer *ssr;
	} frameBuffers;

	struct {
		VkCommandBuffer geometry = VK_NULL_HANDLE; // shadow & geometry
		VkCommandBuffer ssao = VK_NULL_HANDLE;
		VkCommandBuffer ssaoBlur = VK_NULL_HANDLE;
		VkCommandBuffer lighting = VK_NULL_HANDLE;
		VkCommandBuffer ssr = VK_NULL_HANDLE;
	} commandBuffers;

	VkSemaphore geometrySemaphore = VK_NULL_HANDLE;
	VkSemaphore ssaoSemaphore = VK_NULL_HANDLE;
	VkSemaphore ssaoBlurSemaphore = VK_NULL_HANDLE;
	VkSemaphore lightingSemaphore = VK_NULL_HANDLE;
	VkSemaphore ssrSemaphore = VK_NULL_HANDLE;

	float lerp(float a, float b, float f) { return a + f * (b - a); }

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Final Scene";
		camera.type = Camera::CameraType::firstperson;
#if defined(__ANDROID__)
		camera.movementSpeed = 2.5f;
#else
		camera.movementSpeed = 5.0f;
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 2.15f, 0.3f, -8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, zNear, zFar);
		timerSpeed *= 0.25f;
		paused = true;
		settings.overlay = true;
	}

	~VulkanExample()
	{
		// Frame buffers
		if (frameBuffers.shadow) delete frameBuffers.shadow;
		if (frameBuffers.geometry) delete frameBuffers.geometry;
		if (frameBuffers.ssao) delete frameBuffers.ssao;
		if (frameBuffers.ssaoBlur) delete frameBuffers.ssaoBlur;
		if (frameBuffers.lighting) delete frameBuffers.lighting;
		if (frameBuffers.ssr) delete frameBuffers.ssr;

		vkDestroyPipeline(device, pipelines.shadowpass, nullptr);
		vkDestroyPipeline(device, pipelines.geometry, nullptr);
		vkDestroyPipeline(device, pipelines.ssao, nullptr);
		vkDestroyPipeline(device, pipelines.ssaoBlur, nullptr);
		vkDestroyPipeline(device, pipelines.lighting, nullptr);
		vkDestroyPipeline(device, pipelines.ssr, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		// Uniform buffers
		uniformBuffers.shadowGeometryShader.destroy();
		uniformBuffers.geometry.destroy();
		uniformBuffers.ssao.destroy();
		uniformBuffers.ssaoBlur.destroy();
		uniformBuffers.lighting.destroy();
		uniformBuffers.ssr.destroy();
		uniformBuffers.composition.destroy();

		// Textures
		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.background.colorMap.destroy();
		textures.background.normalMap.destroy();
		textures.ssaoNoise.destroy();

		vkDestroySemaphore(device, geometrySemaphore, nullptr);
		vkDestroySemaphore(device, ssaoSemaphore, nullptr);
		vkDestroySemaphore(device, ssaoBlurSemaphore, nullptr);
		vkDestroySemaphore(device, lightingSemaphore, nullptr);
		vkDestroySemaphore(device, ssrSemaphore, nullptr);
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Geometry shader support is required for writing to multiple shadow map layers in one single pass
		if (deviceFeatures.geometryShader) {
			enabledFeatures.geometryShader = VK_TRUE;
		}
		else {
			vks::tools::exitFatal("Selected GPU does not support geometry shaders!", VK_ERROR_FEATURE_NOT_PRESENT);
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
		// Enable texture compression
		if (deviceFeatures.textureCompressionBC) {
			enabledFeatures.textureCompressionBC = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionASTC_LDR) {
			enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionETC2) {
			enabledFeatures.textureCompressionETC2 = VK_TRUE;
		}
	}

	// Prepare a layered shadow map with each layer containing depth from a light's point of view
	// The shadow mapping pass uses geometry shader instancing to output the scene from the different
	// light sources' point of view to the layers of the depth attachment in one single pass
	void shadowSetup()
	{
		frameBuffers.shadow = new vks::Framebuffer(vulkanDevice, SHADOWMAP_DIM, SHADOWMAP_DIM);

		// Create a layered depth attachment for rendering the depth maps from the lights' point of view
		// Each layer corresponds to one of the lights
		// The actual output to the separate layers is done in the geometry shader using shader instancing
		// We will pass the matrices of the lights to the GS that selects the layer by the current invocation
		frameBuffers.shadow->addAttachment(
			{ SHADOWMAP_DIM, SHADOWMAP_DIM, LIGHT_COUNT, SHADOWMAP_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT}
		);

		// Create sampler to sample from to depth attachment
		// Used to sample in the fragment shader for shadowed rendering
		VK_CHECK_RESULT(frameBuffers.shadow->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.shadow->createRenderPass());
	}

	// Prepare the framebuffer for geometry rendering with multiple attachments used as render targets inside the fragment shaders
	void geometrySetup()
	{
		frameBuffers.geometry = new vks::Framebuffer(vulkanDevice, FB_DIM, FB_DIM);

		// Four attachments (3 color, 1 depth)
		vks::AttachmentCreateInfo attachmentInfo = 
			{ FB_DIM, FB_DIM, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };

		// Color attachments
		// Attachment 0: (World space) Positions
		attachmentInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		frameBuffers.geometry->addAttachment(attachmentInfo);

		// Attachment 1: (World space) Normals
		attachmentInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		frameBuffers.geometry->addAttachment(attachmentInfo);

		// Attachment 2: Albedo (color)
		attachmentInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		frameBuffers.geometry->addAttachment(attachmentInfo);

		// Depth attachment
		// Find a suitable depth format
		VkFormat attDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormat);

		attachmentInfo.format = attDepthFormat;
		attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		frameBuffers.geometry->addAttachment(attachmentInfo);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(frameBuffers.geometry->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.geometry->createRenderPass());
	}

	void ssaoSetup()
	{
		frameBuffers.ssao = new vks::Framebuffer(vulkanDevice, SSAO_FB_DIM, SSAO_FB_DIM);

		frameBuffers.ssao->addAttachment(
			{ SSAO_FB_DIM, SSAO_FB_DIM, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }
		);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(frameBuffers.ssao->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.ssao->createRenderPass());
	}

	void ssaoBlurSetup()
	{
		frameBuffers.ssaoBlur = new vks::Framebuffer(vulkanDevice, SSAO_FB_DIM, SSAO_FB_DIM);

		frameBuffers.ssaoBlur->addAttachment(
			{ SSAO_FB_DIM, SSAO_FB_DIM, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }
		);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(frameBuffers.ssaoBlur->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.ssaoBlur->createRenderPass());
	}

	void directLightingSetup()
	{
		frameBuffers.lighting = new vks::Framebuffer(vulkanDevice, LIGHTING_FB_DIM, LIGHTING_FB_DIM);

		frameBuffers.lighting->addAttachment(
			{ LIGHTING_FB_DIM, LIGHTING_FB_DIM, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }
		);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(frameBuffers.lighting->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.lighting->createRenderPass());
	}

	void ssrSetup()
	{
		frameBuffers.ssr = new vks::Framebuffer(vulkanDevice, SSR_FB_DIM, SSR_FB_DIM);

		frameBuffers.ssr->addAttachment(
			{ SSR_FB_DIM, SSR_FB_DIM, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT }
		);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(frameBuffers.ssr->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(frameBuffers.ssr->createRenderPass());
	}

	// Put render commands for the scene into the given command buffer
	void renderScene(VkCommandBuffer cmdBuffer, bool shadow)
	{
		VkDeviceSize offsets[1] = { 0 };

		// Background
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, shadow ? &descriptorSets.shadow : &descriptorSets.background, 0, NULL);
		models.background.draw(cmdBuffer);

		// Objects
		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, shadow ? &descriptorSets.shadow : &descriptorSets.model, 0, NULL);
		models.model.bindBuffers(cmdBuffer);
		vkCmdDrawIndexed(cmdBuffer, models.model.indices.count, 3, 0, 0, 0);
	}

	void buildGeometryCommandBuffer()
	{
		if (commandBuffers.geometry == VK_NULL_HANDLE)
		{
			commandBuffers.geometry = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize geometry rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &geometrySemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		std::array<VkClearValue, 4> clearValues = {};
		VkViewport viewport;
		VkRect2D scissor;

		// First pass: Shadow map generation
		// -------------------------------------------------------------------------------------------------------

		clearValues[0].depthStencil = { 1.0f, 0 };

		renderPassBeginInfo.renderPass = frameBuffers.shadow->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.shadow->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.shadow->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.shadow->height;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers.geometry, &cmdBufInfo));

		viewport = vks::initializers::viewport((float)frameBuffers.shadow->width, (float)frameBuffers.shadow->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.geometry, 0, 1, &viewport);

		scissor = vks::initializers::rect2D(frameBuffers.shadow->width, frameBuffers.shadow->height, 0, 0);
		vkCmdSetScissor(commandBuffers.geometry, 0, 1, &scissor);

		// Set depth bias (aka "Polygon offset")
		vkCmdSetDepthBias(
			commandBuffers.geometry,
			depthBiasConstant,
			0.0f,
			depthBiasSlope);

		vkCmdBeginRenderPass(commandBuffers.geometry, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffers.geometry, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shadowpass);
		renderScene(commandBuffers.geometry, true);
		vkCmdEndRenderPass(commandBuffers.geometry);

		// Second pass: Deferred calculations
		// -------------------------------------------------------------------------------------------------------

		// Clear values for all attachments written in the fragment shader
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		renderPassBeginInfo.renderPass = frameBuffers.geometry->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.geometry->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.geometry->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.geometry->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffers.geometry, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		viewport = vks::initializers::viewport((float)frameBuffers.geometry->width, (float)frameBuffers.geometry->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.geometry, 0, 1, &viewport);

		scissor = vks::initializers::rect2D(frameBuffers.geometry->width, frameBuffers.geometry->height, 0, 0);
		vkCmdSetScissor(commandBuffers.geometry, 0, 1, &scissor);

		vkCmdBindPipeline(commandBuffers.geometry, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometry);
		renderScene(commandBuffers.geometry, false);
		vkCmdEndRenderPass(commandBuffers.geometry);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers.geometry));
	}

	void buildSsaoCommandBuffer()
	{
		if (commandBuffers.ssao == VK_NULL_HANDLE)
		{
			commandBuffers.ssao = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize geometry rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &ssaoSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		std::array<VkClearValue, 1> clearValues = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		renderPassBeginInfo.renderPass = frameBuffers.ssao->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.ssao->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.ssao->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.ssao->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers.ssao, &cmdBufInfo));

		VkViewport viewport = vks::initializers::viewport((float)frameBuffers.ssao->width, (float)frameBuffers.ssao->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.ssao, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(frameBuffers.ssao->width, frameBuffers.ssao->height, 0, 0);
		vkCmdSetScissor(commandBuffers.ssao, 0, 1, &scissor);

		vkCmdBeginRenderPass(commandBuffers.ssao, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers.ssao, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssao);
		vkCmdBindDescriptorSets(commandBuffers.ssao, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssao, 0, NULL);
		vkCmdDraw(commandBuffers.ssao, 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers.ssao);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers.ssao));
	}

	void buildSsaoBlurCommandBuffer()
	{
		if (commandBuffers.ssaoBlur == VK_NULL_HANDLE)
		{
			commandBuffers.ssaoBlur = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize geometry rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &ssaoBlurSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		std::array<VkClearValue, 1> clearValues = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		renderPassBeginInfo.renderPass = frameBuffers.ssaoBlur->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.ssaoBlur->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.ssaoBlur->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.ssaoBlur->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers.ssaoBlur, &cmdBufInfo));

		VkViewport viewport = vks::initializers::viewport((float)frameBuffers.ssaoBlur->width, (float)frameBuffers.ssaoBlur->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.ssaoBlur, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(frameBuffers.ssaoBlur->width, frameBuffers.ssaoBlur->height, 0, 0);
		vkCmdSetScissor(commandBuffers.ssaoBlur, 0, 1, &scissor);

		vkCmdBeginRenderPass(commandBuffers.ssaoBlur, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers.ssaoBlur, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssaoBlur);
		vkCmdBindDescriptorSets(commandBuffers.ssaoBlur, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssaoBlur, 0, NULL);
		vkCmdDraw(commandBuffers.ssaoBlur, 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers.ssaoBlur);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers.ssaoBlur));
	}

	void buildLightingCommandBuffer()
	{
		if (commandBuffers.lighting == VK_NULL_HANDLE)
		{
			commandBuffers.lighting = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize geometry rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &lightingSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		std::array<VkClearValue, 1> clearValues = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		renderPassBeginInfo.renderPass = frameBuffers.lighting->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.lighting->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.lighting->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.lighting->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers.lighting, &cmdBufInfo));

		VkViewport viewport = vks::initializers::viewport((float)frameBuffers.lighting->width, (float)frameBuffers.lighting->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.lighting, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(frameBuffers.lighting->width, frameBuffers.lighting->height, 0, 0);
		vkCmdSetScissor(commandBuffers.lighting, 0, 1, &scissor);

		vkCmdBeginRenderPass(commandBuffers.lighting, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers.lighting, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lighting);
		vkCmdBindDescriptorSets(commandBuffers.lighting, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.lighting, 0, NULL);
		vkCmdDraw(commandBuffers.lighting, 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers.lighting);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers.lighting));
	}

	void buildSsrCommandBuffer()
	{
		if (commandBuffers.ssr == VK_NULL_HANDLE)
		{
			commandBuffers.ssr = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize geometry rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &ssrSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		std::array<VkClearValue, 1> clearValues = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		renderPassBeginInfo.renderPass = frameBuffers.ssr->renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.ssr->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.ssr->width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.ssr->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());;
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers.ssr, &cmdBufInfo));

		VkViewport viewport = vks::initializers::viewport((float)frameBuffers.ssr->width, (float)frameBuffers.ssr->height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffers.ssr, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(frameBuffers.ssr->width, frameBuffers.ssr->height, 0, 0);
		vkCmdSetScissor(commandBuffers.ssr, 0, 1, &scissor);

		vkCmdBeginRenderPass(commandBuffers.ssr, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(commandBuffers.ssr, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssr);
		vkCmdBindDescriptorSets(commandBuffers.ssr, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssr, 0, NULL);
		vkCmdDraw(commandBuffers.ssr, 3, 1, 0, 0);

		vkCmdEndRenderPass(commandBuffers.ssr);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffers.ssr));
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = VulkanExampleBase::frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
			vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.model.loadFromFile(getAssetPath() + "models/armor/armor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.background.loadFromFile(getAssetPath() + "models/deferred_box.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.model.colorMap.loadFromFile(getAssetPath() + "models/armor/colormap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.model.normalMap.loadFromFile(getAssetPath() + "models/armor/normalmap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.colorMap.loadFromFile(getAssetPath() + "textures/stonefloor02_color_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.normalMap.loadFromFile(getAssetPath() + "textures/stonefloor02_normal_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);

		// SSAO
		std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);
		// Random noise
		std::vector<glm::vec4> ssaoNoise(SSAO_NOISE_DIM * SSAO_NOISE_DIM);
		for (uint32_t i = 0; i < static_cast<uint32_t>(ssaoNoise.size()); i++)
		{
			ssaoNoise[i] = glm::vec4(rndDist(rndEngine) * 2.0f - 1.0f, rndDist(rndEngine) * 2.0f - 1.0f, 0.0f, 0.0f);
		}
		// Upload as texture
		textures.ssaoNoise.fromBuffer(ssaoNoise.data(), ssaoNoise.size() * sizeof(glm::vec4), VK_FORMAT_R32G32B32A32_SFLOAT, SSAO_NOISE_DIM, SSAO_NOISE_DIM, vulkanDevice, queue, VK_FILTER_NEAREST);
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 18)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				8);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		// Deferred shading layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0),
			// Binding 1: Position texture
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2: Normals texture
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 3: Albedo texture
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
			// Binding 5: Shadow map
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),
			// Binding 6: Blured ao
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 6),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Shared pipeline layout used by all pipelines
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSet()
	{
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		/*
			SHADOW
		*/
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.shadow));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.shadow, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.shadowGeometryShader.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			GEOMETRY
		*/
		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.model));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.geometry.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.model.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(descriptorSets.model, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.model.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		// Background
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.background));
		writeDescriptorSets = {
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.background, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.geometry.descriptor),
			// Binding 1: Color map
			vks::initializers::writeDescriptorSet(descriptorSets.background, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.background.colorMap.descriptor),
			// Binding 2: Normal map
			vks::initializers::writeDescriptorSet(descriptorSets.background, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.background.normalMap.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

		/*
			SSAO
		*/
		VkDescriptorImageInfo texDescriptorPosition =
			vks::initializers::descriptorImageInfo(
				frameBuffers.geometry->sampler,
				frameBuffers.geometry->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorNormal =
			vks::initializers::descriptorImageInfo(
				frameBuffers.geometry->sampler,
				frameBuffers.geometry->attachments[1].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.ssao));
		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3: SSAO noise texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &textures.ssaoNoise.descriptor),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssao.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			SSAO BLUR
		*/
		VkDescriptorImageInfo texDescriptorSsao =
			vks::initializers::descriptorImageInfo(
				frameBuffers.ssao->sampler,
				frameBuffers.ssao->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.ssaoBlur));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.ssaoBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorSsao),
			vks::initializers::writeDescriptorSet(descriptorSets.ssaoBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssaoBlur.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			LIGHTING
		*/
		VkDescriptorImageInfo texDescriptorAlbedo =
			vks::initializers::descriptorImageInfo(
				frameBuffers.geometry->sampler,
				frameBuffers.geometry->attachments[2].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorShadowMap =
			vks::initializers::descriptorImageInfo(
				frameBuffers.shadow->sampler,
				frameBuffers.shadow->attachments[0].view,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorSsaoBlur =
			vks::initializers::descriptorImageInfo(
				frameBuffers.ssaoBlur->sampler,
				frameBuffers.ssaoBlur->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.lighting));
		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3: Albedo texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorAlbedo),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.lighting.descriptor),
			// Binding 5: Shadow map
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &texDescriptorShadowMap),
			// Binding 6: Blured ao
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6, &texDescriptorSsaoBlur),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			SSR
		*/
		VkDescriptorImageInfo texDescriptorDirectColor =
			vks::initializers::descriptorImageInfo(
				frameBuffers.lighting->sampler,
				frameBuffers.lighting->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.ssr));
		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3: Direct lighting color texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorDirectColor),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssr.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			COMPOSITION
		*/
		VkDescriptorImageInfo texDescriptorReflectColor =
			vks::initializers::descriptorImageInfo(
				frameBuffers.ssr->sampler,
				frameBuffers.ssr->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		writeDescriptorSets = {
			// Binding 1: Direct lighting color texture
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDirectColor),
			// Binding 2: Reflect color texture
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorReflectColor),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.composition.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
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
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
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

		
		/*
			GEOMETRY
		*/
		pipelineCI.renderPass = frameBuffers.geometry->renderPass;

		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Tangent });
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;

		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendState.pAttachments = blendAttachmentStates.data();

		shaderStages[0] = loadShader(getShadersPath() + "final/spirv/geometry.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/geometry.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.geometry));

		/*
			SSAO
		*/
		pipelineCI.renderPass = frameBuffers.ssao->renderPass;

		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;

		colorBlendState.attachmentCount = 1;
		colorBlendState.pAttachments = &blendAttachmentState;

		shaderStages[0] = loadShader(getShadersPath() + "final/spirv/screenQuad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/ssao.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssao));

		/*
			SSAO BLUR
		*/
		pipelineCI.renderPass = frameBuffers.ssaoBlur->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/ssaoBlur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssaoBlur));

		/*
			LIGHTING
		*/
		pipelineCI.renderPass = frameBuffers.lighting->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lighting));

		/*
			SSR
		*/
		pipelineCI.renderPass = frameBuffers.ssr->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/ssr_world.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssr));

		/*
			COMPOSITION
		*/
		pipelineCI.renderPass = renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		/*
			SHADOW
		*/
		pipelineCI.renderPass = frameBuffers.shadow->renderPass;

		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position });
		// Shadow pass doesn't use any color attachments
		colorBlendState.attachmentCount = 0;
		colorBlendState.pAttachments = nullptr;
		// Cull front faces
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		// Enable depth bias
		rasterizationState.depthBiasEnable = VK_TRUE;
		// Add depth bias to dynamic state, so we can change it at runtime
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

		// The shadow mapping pipeline uses geometry shader instancing (invocations layout modifier) to output
		// shadow maps for multiple lights sources into the different shadow map layers in one single render pass
		std::array<VkPipelineShaderStageCreateInfo, 2> shadowStages;
		shadowStages[0] = loadShader(getShadersPath() + "final/spirv/shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shadowStages[1] = loadShader(getShadersPath() + "final/spirv/shadow.geom.spv", VK_SHADER_STAGE_GEOMETRY_BIT);
		pipelineCI.pStages = shadowStages.data();
		pipelineCI.stageCount = static_cast<uint32_t>(shadowStages.size());

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.shadowpass));
	}

	void prepareUniformBuffers()
	{
		// Shadow map vertex shader (matrices from shadow's pov)
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.shadowGeometryShader,
			sizeof(uboShadow)));

		// Geometry vertex shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.geometry,
			sizeof(uboGeometry)));

		// SSAO fragment shader
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssao,
			sizeof(uboSsao));

		// SSAO blur fragment shader
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssaoBlur,
			sizeof(uboSsaoBlur));

		// Direct lighting fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.lighting,
			sizeof(uboDirectLighting)));

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
		VK_CHECK_RESULT(uniformBuffers.shadowGeometryShader.map());
		VK_CHECK_RESULT(uniformBuffers.geometry.map());
		VK_CHECK_RESULT(uniformBuffers.ssao.map());
		VK_CHECK_RESULT(uniformBuffers.ssaoBlur.map());
		VK_CHECK_RESULT(uniformBuffers.lighting.map());
		VK_CHECK_RESULT(uniformBuffers.ssr.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());

		// Init some values
		uboGeometry.instancePos[0] = glm::vec4(0.0f);
		uboGeometry.instancePos[1] = glm::vec4(-7.0f, 0.0, -4.0f, 0.0f);
		uboGeometry.instancePos[2] = glm::vec4(4.0f, 0.0, -6.0f, 0.0f);

		// SSAO
		std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);
		// Camera matrix
		uboSsao.projection = camera.matrices.perspective;
		// Sample kernel
		for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
		{
			glm::vec3 sample(rndDist(rndEngine) * 2.0 - 1.0, rndDist(rndEngine) * 2.0 - 1.0, rndDist(rndEngine));
			sample = glm::normalize(sample);
			sample *= rndDist(rndEngine);
			float scale = float(i) / float(SSAO_KERNEL_SIZE);
			scale = lerp(0.1f, 1.0f, scale * scale);
			uboSsao.kernel[i] = glm::vec4(sample * scale, 0.0f);
		}

		// Update
		updateUniformBufferGeometry();
		updateUniformBufferSsao();
		updateUniformBufferSsaoBlur();
		updateUniformBufferLighting();
		updateUniformBufferSsr();
		updateUniformBufferComposition();
	}

	void updateUniformBufferGeometry()
	{
		uboGeometry.projection = camera.matrices.perspective;
		uboGeometry.view = camera.matrices.view;
		uboGeometry.model = glm::mat4(1.0f);
		memcpy(uniformBuffers.geometry.mapped, &uboGeometry, sizeof(uboGeometry));
	}

	void updateUniformBufferSsao() {
		uboSsao.projection = camera.matrices.perspective;
		uboSsao.view = camera.matrices.view;
		memcpy(uniformBuffers.ssao.mapped, &uboSsao, sizeof(uboSsao));
	}

	void updateUniformBufferSsaoBlur() {
		memcpy(uniformBuffers.ssaoBlur.mapped, &uboSsaoBlur, sizeof(uboSsaoBlur));
	}

	void updateUniformBufferLighting()
	{
		// Animate
		uboDirectLighting.lights[0].position.x = -14.0f + std::abs(sin(glm::radians(timer * 360.0f)) * 20.0f);
		uboDirectLighting.lights[0].position.z = 15.0f + cos(glm::radians(timer *360.0f)) * 1.0f;

		uboDirectLighting.lights[1].position.x = 14.0f - std::abs(sin(glm::radians(timer * 360.0f)) * 2.5f);
		uboDirectLighting.lights[1].position.z = 13.0f + cos(glm::radians(timer *360.0f)) * 4.0f;

		uboDirectLighting.lights[2].position.x = 0.0f + sin(glm::radians(timer *360.0f)) * 4.0f;
		uboDirectLighting.lights[2].position.z = 4.0f + cos(glm::radians(timer *360.0f)) * 2.0f;

		for (uint32_t i = 0; i < LIGHT_COUNT; i++)
		{
			// mvp from light's pov (for shadows)
			glm::mat4 shadowProj = glm::perspective(glm::radians(lightFOV), 1.0f, zNear, zFar);
			glm::mat4 shadowView = glm::lookAt(glm::vec3(uboDirectLighting.lights[i].position), glm::vec3(uboDirectLighting.lights[i].target), glm::vec3(0.0f, 1.0f, 0.0f));
			glm::mat4 shadowModel = glm::mat4(1.0f);

			uboShadow.mvp[i] = shadowProj * shadowView * shadowModel;
			uboDirectLighting.lights[i].viewMatrix = uboShadow.mvp[i];
		}

		memcpy(uboShadow.instancePos, uboGeometry.instancePos, sizeof(uboGeometry.instancePos));
		memcpy(uniformBuffers.shadowGeometryShader.mapped, &uboShadow, sizeof(uboShadow));

		uboDirectLighting.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		memcpy(uniformBuffers.lighting.mapped, &uboDirectLighting, sizeof(uboDirectLighting));
	}

	void updateUniformBufferSsr() {
		uboSsr.projection = camera.matrices.perspective;
		uboSsr.view = camera.matrices.view;
		uboSsr.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
		memcpy(uniformBuffers.ssr.mapped, &uboSsr, sizeof(uboSsr));
	}

	void updateUniformBufferComposition() {
		memcpy(uniformBuffers.composition.mapped, &uboComposition, sizeof(uboComposition));
	}

	Light initLight(glm::vec3 pos, glm::vec3 target, glm::vec3 color)
	{
		Light light;
		light.position = glm::vec4(pos, 1.0f);
		light.target = glm::vec4(target, 0.0f);
		light.color = glm::vec4(color, 0.0f);
		return light;
	}

	void initLights()
	{
		uboDirectLighting.lights[0] = initLight(glm::vec3(-14.0f, -0.5f, 15.0f), glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.5f, 0.5f));
		uboDirectLighting.lights[1] = initLight(glm::vec3(14.0f, -4.0f, 12.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		uboDirectLighting.lights[2] = initLight(glm::vec3(0.0f, -10.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 1.0f));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		submitInfo.pSignalSemaphores = &geometrySemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers.geometry;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		submitInfo.pWaitSemaphores = &geometrySemaphore;
		submitInfo.pSignalSemaphores = &ssaoSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers.ssao;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		submitInfo.pWaitSemaphores = &ssaoSemaphore;
		submitInfo.pSignalSemaphores = &ssaoBlurSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers.ssaoBlur;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		submitInfo.pWaitSemaphores = &ssaoBlurSemaphore;
		submitInfo.pSignalSemaphores = &lightingSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers.lighting;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		submitInfo.pWaitSemaphores = &lightingSemaphore;
		submitInfo.pSignalSemaphores = &ssrSemaphore;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers.ssr;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		submitInfo.pWaitSemaphores = &ssrSemaphore;
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();

		shadowSetup();
		geometrySetup();
		ssaoSetup();
		ssaoBlurSetup();
		directLightingSetup();
		ssrSetup();

		initLights();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();

		buildGeometryCommandBuffer();
		buildSsaoCommandBuffer();
		buildSsaoBlurCommandBuffer();
		buildLightingCommandBuffer();
		buildSsrCommandBuffer();
		buildCommandBuffers();

		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		updateUniformBufferLighting();
		if (camera.updated) 
		{
			updateUniformBufferGeometry();
			updateUniformBufferSsao();
			updateUniformBufferSsr();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("SSAO Settings")) {
			if (overlay->sliderFloat("Radius", &uboSsao.radius, 0.0f, 2.0)) {
				updateUniformBufferSsao();
			}
			if (overlay->sliderFloat("Bias", &uboSsao.bias, 0.0f, 0.2)) {
				updateUniformBufferSsao();
			}
			if (overlay->sliderInt("SSAO Blur Size", &uboSsaoBlur.size, 0, 3)) {
				updateUniformBufferSsaoBlur();
			}
		}
		if (overlay->header("Direct Lighting Settings")) {
			if (overlay->checkBox("Shadow", &uboDirectLighting.useShadow)) {
				updateUniformBufferLighting();
			}
			if (overlay->checkBox("AO", &uboDirectLighting.useSsao)) {
				updateUniformBufferLighting();
			}
		}
		if (overlay->header("SSR Settings")) {
			// ray marching
			if (overlay->sliderFloat("Max Distance", &uboSsr.maxDistance, 0.0f, 5.0)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Resolution", &uboSsr.resolution, 0.0f, 1.0)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Thickness", &uboSsr.thickness, 0.0f, 2.0)) {
				updateUniformBufferSsr();
			}
			// blur
			if (overlay->sliderInt("Blur Size", &uboComposition.ssrBlurSize, 0, 10)) {
				updateUniformBufferComposition();
			}
			// blend
			if (overlay->sliderFloat("Blend Factor", &uboComposition.blendFactor, 0.0, 1.0)) {
				updateUniformBufferComposition();
			}
		}
		if (overlay->header("HDR Settings")) {
			if (overlay->sliderFloat("Exposure", &uboComposition.exposure, 0.0, 5.0)) {
				updateUniformBufferComposition();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
