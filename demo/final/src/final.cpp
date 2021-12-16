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

#include "GPUTimestamps.h"
#include "ParticleEffect.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION true

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
		vks::Buffer ssrBlur;
		vks::Buffer composition;
		vks::Buffer tonemapping;
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
		float maxDistance = 4.0f;
		float resolution = 0.1f;
		float thickness = 0.2f;
	}uboSsr;

	struct {
		int32_t size = 1;
	} uboSsrBlur;

	struct {
		float blendFactor = 0.2f;
	}uboComposition;

	struct {
		float exposure = 1.0f;
	} uboTonemapping;

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
		VkPipeline ssrBlur;
		VkPipeline composition;
		VkPipeline tonemapping;
	} pipelines;

	struct BlurSpecData {
		uint32_t channelCount;
	};
	std::array<VkSpecializationMapEntry, 1> blurSpecMapEntries = {
		vks::initializers::specializationMapEntry(0, offsetof(BlurSpecData, channelCount), sizeof(BlurSpecData::channelCount))
	};

	struct {
		VkDescriptorSet shadow;
		VkDescriptorSet model;
		VkDescriptorSet background;
		VkDescriptorSet ssao;
		VkDescriptorSet ssaoBlur;
		VkDescriptorSet lighting;
		VkDescriptorSet ssr;
		VkDescriptorSet ssrBlur;
		VkDescriptorSet composition;
	} descriptorSets;
	VkDescriptorSet descriptorSet; // tonemapping

	struct {
		std::unique_ptr<vks::Framebuffer> shadow;
		std::unique_ptr<vks::Framebuffer> geometry;
		std::unique_ptr<vks::Framebuffer> ssao;
		std::unique_ptr<vks::Framebuffer> ssaoBlur;
		std::unique_ptr<vks::Framebuffer> lighting;
		std::unique_ptr<vks::Framebuffer> ssr;
		std::unique_ptr<vks::Framebuffer> ssrBlur;
		std::unique_ptr<vks::Framebuffer> composition;
	} passes;

	GPUTimestamps GPUTimer;
	std::vector<TimeStamp> timeStamps;

	ParticleEffect particles;

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
		//camera.flipY = true;
		//camera.position = { 2.15f, 0.3f, -8.75f };
		//camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.position = { 0.0f, 0.0f, -8.75f };
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, zNear, zFar);
		timerSpeed *= 0.25f;
		paused = true;
		settings.overlay = true;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.shadowpass, nullptr);
		vkDestroyPipeline(device, pipelines.geometry, nullptr);
		vkDestroyPipeline(device, pipelines.ssao, nullptr);
		vkDestroyPipeline(device, pipelines.ssaoBlur, nullptr);
		vkDestroyPipeline(device, pipelines.lighting, nullptr);
		vkDestroyPipeline(device, pipelines.ssr, nullptr);
		vkDestroyPipeline(device, pipelines.ssrBlur, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);
		vkDestroyPipeline(device, pipelines.tonemapping, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		// Uniform buffers
		uniformBuffers.shadowGeometryShader.destroy();
		uniformBuffers.geometry.destroy();
		uniformBuffers.ssao.destroy();
		uniformBuffers.ssaoBlur.destroy();
		uniformBuffers.lighting.destroy();
		uniformBuffers.ssr.destroy();
		uniformBuffers.ssrBlur.destroy();
		uniformBuffers.composition.destroy();
		uniformBuffers.tonemapping.destroy();

		// Textures
		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.background.colorMap.destroy();
		textures.background.normalMap.destroy();
		textures.ssaoNoise.destroy();

		GPUTimer.OnDestroy();
		particles.destroy();
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

	void prepareGraphicsPasses() {
		// Shadow
		//
		passes.shadow = std::make_unique<vks::Framebuffer>(vulkanDevice, SHADOWMAP_DIM, SHADOWMAP_DIM);
		passes.shadow->addAttachment(SHADOWMAP_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, LIGHT_COUNT);
		VK_CHECK_RESULT(passes.shadow->createSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.shadow->createRenderPass());

		// Geometry
		//
		passes.geometry = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // position
		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // normal
		passes.geometry->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // albedo

		// depth
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
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};
		depth.weakRef = true;
		passes.geometry->attachments.push_back(depth);

		VK_CHECK_RESULT(passes.geometry->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.geometry->createRenderPass());

		// SSAO
		//
		passes.ssao = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.ssao->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.ssao->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.ssao->createRenderPass());

		// SSAO Blur
		//
		passes.ssaoBlur = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.ssaoBlur->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.ssaoBlur->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.ssaoBlur->createRenderPass());

		// Direct Lighting
		//
		passes.lighting = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.lighting->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.lighting->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.lighting->createRenderPass());

		// SSR
		//
		passes.ssr = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.ssr->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.ssr->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.ssr->createRenderPass());

		// SSR Blur
		//
		passes.ssrBlur = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.ssrBlur->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.ssrBlur->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.ssrBlur->createRenderPass());

		// Composition
		//
		passes.composition = std::make_unique<vks::Framebuffer>(vulkanDevice, width, height);
		passes.composition->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		VK_CHECK_RESULT(passes.composition->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.composition->createRenderPass());
	}

	virtual void setupRenderPass() override {
		// Tonemapping Pass
		//
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

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[4];

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			GPUTimer.OnBeginFrame(drawCmdBuffers[i]);

			VkViewport viewport = vks::initializers::viewport((float)passes.shadow->width, (float)passes.shadow->height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(passes.shadow->width, passes.shadow->height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			renderPassBeginInfo.renderArea.extent.width = passes.shadow->width;
			renderPassBeginInfo.renderArea.extent.height = passes.shadow->height;

			// Shadow
			//
			{
				renderPassBeginInfo.renderPass = passes.shadow->renderPass;
				renderPassBeginInfo.framebuffer = passes.shadow->framebuffer;
				
				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].depthStencil = { 1.0f, 0 };

				// Set depth bias (aka "Polygon offset")
				vkCmdSetDepthBias(
					drawCmdBuffers[i],
					depthBiasConstant,
					0.0f,
					depthBiasSlope);

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.shadowpass);
				renderScene(drawCmdBuffers[i], true);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;

			// Geometry
			//
			{
				renderPassBeginInfo.renderPass = passes.geometry->renderPass;
				renderPassBeginInfo.framebuffer = passes.geometry->framebuffer;

				renderPassBeginInfo.clearValueCount = 4;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
				clearValues[3].depthStencil = { 1.0f, 0 };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.geometry);
				renderScene(drawCmdBuffers[i], false);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// SSAO
			//
			{
				renderPassBeginInfo.renderPass = passes.ssao->renderPass;
				renderPassBeginInfo.framebuffer = passes.ssao->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssao);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssao, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// SSAO Blur
			//
			{
				renderPassBeginInfo.renderPass = passes.ssaoBlur->renderPass;
				renderPassBeginInfo.framebuffer = passes.ssaoBlur->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssaoBlur);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssaoBlur, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// Direct Lighting
			//
			{
				renderPassBeginInfo.renderPass = passes.lighting->renderPass;
				renderPassBeginInfo.framebuffer = passes.lighting->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lighting);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.lighting, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// SSR
			//
			{
				renderPassBeginInfo.renderPass = passes.ssr->renderPass;
				renderPassBeginInfo.framebuffer = passes.ssr->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssr);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssr, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// SSR Blur
			//
			{
				renderPassBeginInfo.renderPass = passes.ssrBlur->renderPass;
				renderPassBeginInfo.framebuffer = passes.ssrBlur->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssrBlur);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.ssrBlur, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

			// Composition
			//
			{
				renderPassBeginInfo.renderPass = passes.composition->renderPass;
				renderPassBeginInfo.framebuffer = passes.composition->framebuffer;

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.composition, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);

				// Particles
				//
				particles.draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}
			
			// Tonemapping
			//
			{
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];

				renderPassBeginInfo.clearValueCount = 1;
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.tonemapping);

				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);

				drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				GPUTimer.NextTimeStamp(drawCmdBuffers[i]);
			}

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
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 20)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				10);

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
				passes.geometry->sampler,
				passes.geometry->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorNormal =
			vks::initializers::descriptorImageInfo(
				passes.geometry->sampler,
				passes.geometry->attachments[1].view,
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
				passes.ssao->sampler,
				passes.ssao->attachments[0].view,
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
				passes.geometry->sampler,
				passes.geometry->attachments[2].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorShadowMap =
			vks::initializers::descriptorImageInfo(
				passes.shadow->sampler,
				passes.shadow->attachments[0].view,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorSsaoBlur =
			vks::initializers::descriptorImageInfo(
				passes.ssaoBlur->sampler,
				passes.ssaoBlur->attachments[0].view,
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
				passes.lighting->sampler,
				passes.lighting->attachments[0].view,
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
			SSR BLUR
		*/
		VkDescriptorImageInfo texDescriptorReflectColor =
			vks::initializers::descriptorImageInfo(
				passes.ssr->sampler,
				passes.ssr->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.ssrBlur));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.ssrBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorReflectColor),
			vks::initializers::writeDescriptorSet(descriptorSets.ssrBlur, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssrBlur.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			Composition
		*/
		VkDescriptorImageInfo texDescriptorReflectColorBlur =
			vks::initializers::descriptorImageInfo(
				passes.ssrBlur->sampler,
				passes.ssrBlur->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition));
		writeDescriptorSets = {
			// Binding 1: Direct lighting color texture
			vks::initializers::writeDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDirectColor),
			// Binding 2: Reflect color blur texture
			vks::initializers::writeDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorReflectColorBlur),
			// Binding 4: Fragment shader uniform buffer
			vks::initializers::writeDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.composition.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			Tonemapping
		*/
		VkDescriptorImageInfo texDescriptorComposition =
			vks::initializers::descriptorImageInfo(
				passes.composition->sampler,
				passes.composition->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorComposition),
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.tonemapping.descriptor),
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
		pipelineCI.renderPass = passes.geometry->renderPass;

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
		pipelineCI.renderPass = passes.ssao->renderPass;

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
		pipelineCI.renderPass = passes.ssaoBlur->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/blur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		BlurSpecData blurSpecDataSSAO;
		blurSpecDataSSAO.channelCount = 1;
		VkSpecializationInfo specInfoSSAO = vks::initializers::specializationInfo(
			static_cast<uint32_t>(blurSpecMapEntries.size()),
			blurSpecMapEntries.data(),
			sizeof(BlurSpecData),
			&blurSpecDataSSAO
		);
		shaderStages[1].pSpecializationInfo = &specInfoSSAO;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssaoBlur));

		/*
			LIGHTING
		*/
		pipelineCI.renderPass = passes.lighting->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/lighting.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.lighting));

		/*
			SSR
		*/
		pipelineCI.renderPass = passes.ssr->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/ssr_world.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssr));

		/*
			SSR BLUR
		*/
		pipelineCI.renderPass = passes.ssrBlur->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/blur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		BlurSpecData blurSpecDataSSR;
		blurSpecDataSSR.channelCount = 3;
		VkSpecializationInfo specInfoSSR = vks::initializers::specializationInfo(
			static_cast<uint32_t>(blurSpecMapEntries.size()),
			blurSpecMapEntries.data(),
			sizeof(BlurSpecData),
			&blurSpecDataSSR
		);
		shaderStages[1].pSpecializationInfo = &specInfoSSR;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.ssrBlur));

		/*
			Composition
		*/
		pipelineCI.renderPass = passes.composition->renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		/*
			Tonemapping
		*/
		pipelineCI.renderPass = renderPass;

		shaderStages[1] = loadShader(getShadersPath() + "final/spirv/tonemapping.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.tonemapping));

		/*
			SHADOW
		*/
		pipelineCI.renderPass = passes.shadow->renderPass;

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

		// SSR blur fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssrBlur,
			sizeof(uboSsrBlur)));

		// Composition fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.composition,
			sizeof(uboComposition)));

		// Tonemapping fragment shader
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.tonemapping,
			sizeof(uboTonemapping)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.shadowGeometryShader.map());
		VK_CHECK_RESULT(uniformBuffers.geometry.map());
		VK_CHECK_RESULT(uniformBuffers.ssao.map());
		VK_CHECK_RESULT(uniformBuffers.ssaoBlur.map());
		VK_CHECK_RESULT(uniformBuffers.lighting.map());
		VK_CHECK_RESULT(uniformBuffers.ssr.map());
		VK_CHECK_RESULT(uniformBuffers.ssrBlur.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());
		VK_CHECK_RESULT(uniformBuffers.tonemapping.map());

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
		updateUniformBufferSsrBlur();
		updateUniformBufferComposition();
		updateUniformBufferTonemapping();
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

	void updateUniformBufferSsrBlur() {
		memcpy(uniformBuffers.ssrBlur.mapped, &uboSsrBlur, sizeof(uboSsrBlur));
	}

	void updateUniformBufferComposition() {
		memcpy(uniformBuffers.composition.mapped, &uboComposition, sizeof(uboComposition));
	}

	void updateUniformBufferTonemapping() {
		memcpy(uniformBuffers.tonemapping.mapped, &uboTonemapping, sizeof(uboTonemapping));
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

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();

		GPUTimer.GetQueryResult(&timeStamps);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();

		std::vector<std::string> labels = {
			"Begin Frame",
			"Shadow Map",
			"Geometry",
			"SSAO",
			"SSAO Blur",
			"Direct Lighting",
			"SSR Intersection",
			"SSR Blur",
			"Composition",
			"Particles",
			"Tonemapping",
			"ImGUI"
		};
		GPUTimer.OnCreate(vulkanDevice, std::move(labels));
		prepareGraphicsPasses();
		particles.init(vulkanDevice, this, passes.composition->renderPass);

		initLights();
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
		particles.update();
		draw();
		updateUniformBufferLighting();
		if (camera.updated) 
		{
			updateUniformBufferGeometry();
			updateUniformBufferSsao();
			updateUniformBufferSsr();
		}
	}

	virtual void windowResized() override {
		// resource that have been recreated: depthStencil, swapchain image & its framebuffer

		// remaining part of the resources to be recreated
		// render target & its framebuffer
		recreateIntermediateFramebuffer();

		// descriptor set related with those rendertarget
		updateDescriptorSetOnResize();
	}

	void recreateIntermediateFramebuffer() {
		// Since Fixed-size Shadow map, no need to recreate

		// Geometry
		//
		passes.geometry->clearBeforeRecreate(width, height);
		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // position
		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // normal
		passes.geometry->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // albedo

		// depth
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
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		};
		depth.weakRef = true;
		passes.geometry->attachments.push_back(depth);

		passes.geometry->createFrameBuffer();

		// SSAO
		//
		passes.ssao->clearBeforeRecreate(width, height);
		passes.ssao->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.ssao->createFrameBuffer();

		// SSAO Blur
		//
		passes.ssaoBlur->clearBeforeRecreate(width, height);
		passes.ssaoBlur->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.ssaoBlur->createFrameBuffer();

		// Direct Lighting
		//
		passes.lighting->clearBeforeRecreate(width, height);
		passes.lighting->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.lighting->createFrameBuffer();

		// SSR
		//
		passes.ssr->clearBeforeRecreate(width, height);
		passes.ssr->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.ssr->createFrameBuffer();

		// SSR Blur
		//
		passes.ssrBlur->clearBeforeRecreate(width, height);
		passes.ssrBlur->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.ssrBlur->createFrameBuffer();

		// Composition
		//
		passes.composition->clearBeforeRecreate(width, height);
		passes.composition->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		passes.composition->createFrameBuffer();
	}

	void updateDescriptorSetOnResize() {
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		/*
			SSAO
		*/
		VkDescriptorImageInfo texDescriptorPosition =
			vks::initializers::descriptorImageInfo(
				passes.geometry->sampler,
				passes.geometry->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorNormal =
			vks::initializers::descriptorImageInfo(
				passes.geometry->sampler,
				passes.geometry->attachments[1].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			SSAO BLUR
		*/
		VkDescriptorImageInfo texDescriptorSsao =
			vks::initializers::descriptorImageInfo(
				passes.ssao->sampler,
				passes.ssao->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.ssaoBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorSsao)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			LIGHTING
		*/
		VkDescriptorImageInfo texDescriptorAlbedo =
			vks::initializers::descriptorImageInfo(
				passes.geometry->sampler,
				passes.geometry->attachments[2].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorSsaoBlur =
			vks::initializers::descriptorImageInfo(
				passes.ssaoBlur->sampler,
				passes.ssaoBlur->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3: Albedo texture
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorAlbedo),
			// Binding 6: Blured ao
			vks::initializers::writeDescriptorSet(descriptorSets.lighting, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6, &texDescriptorSsaoBlur)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			SSR
		*/
		VkDescriptorImageInfo texDescriptorDirectColor =
			vks::initializers::descriptorImageInfo(
				passes.lighting->sampler,
				passes.lighting->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			// Binding 1: World space position texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorPosition),
			// Binding 2: World space normals texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorNormal),
			// Binding 3: Direct lighting color texture
			vks::initializers::writeDescriptorSet(descriptorSets.ssr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &texDescriptorDirectColor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			SSR BLUR
		*/
		VkDescriptorImageInfo texDescriptorReflectColor =
			vks::initializers::descriptorImageInfo(
				passes.ssr->sampler,
				passes.ssr->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSets.ssrBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorReflectColor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			Composition
		*/
		VkDescriptorImageInfo texDescriptorReflectColorBlur =
			vks::initializers::descriptorImageInfo(
				passes.ssrBlur->sampler,
				passes.ssrBlur->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			// Binding 1: Direct lighting color texture
			vks::initializers::writeDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorDirectColor),
			// Binding 2: Reflect color blur texture
			vks::initializers::writeDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &texDescriptorReflectColorBlur)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		/*
			Tonemapping
		*/
		VkDescriptorImageInfo texDescriptorComposition =
			vks::initializers::descriptorImageInfo(
				passes.composition->sampler,
				passes.composition->attachments[0].view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &texDescriptorComposition)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("SSAO Settings")) {
			if (overlay->sliderFloat("Radius", &uboSsao.radius, 0.0f, 2.0f)) {
				updateUniformBufferSsao();
			}
			if (overlay->sliderFloat("Bias", &uboSsao.bias, 0.0f, 0.2f)) {
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
			if (overlay->sliderFloat("Max Distance", &uboSsr.maxDistance, 0.0f, 5.0f)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Resolution", &uboSsr.resolution, 0.0f, 1.0f)) {
				updateUniformBufferSsr();
			}
			if (overlay->sliderFloat("Thickness", &uboSsr.thickness, 0.0f, 2.0f)) {
				updateUniformBufferSsr();
			}
			// blur
			if (overlay->sliderInt("Blur Size", &uboSsrBlur.size, 0, 10)) {
				updateUniformBufferSsrBlur();
			}
			// blend
			if (overlay->sliderFloat("Blend Factor", &uboComposition.blendFactor, 0.0f, 1.0f)) {
				updateUniformBufferComposition();
			}
		}
		if (overlay->header("HDR Settings")) {
			if (overlay->sliderFloat("Exposure", &uboTonemapping.exposure, 0.0f, 5.0f)) {
				updateUniformBufferTonemapping();
			}
		}
		if (overlay->header("GPU Profile")) {
			for (auto& timeStamp : timeStamps) {
				ImGui::Text("%-22s: %7.1f", timeStamp.m_label.c_str(), timeStamp.m_microseconds);
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
