/*
* Vulkan Example - Physical based shading basics
*
* See http://graphicrants.blogspot.de/2013/08/specular-brdf-reference.html for a good reference to the different functions that make up a specular BRDF
*
* Copyright (C) 2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define DEFERRED_OPAQUE

#ifdef DEFERRED_OPAQUE
#include "VulkanFrameBuffer.hpp"
#define FB_WIDTH width //2048
#define FB_HEIGHT height //2048
#endif

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION true


class VulkanExample : public VulkanExampleBase
{
public:
	vkglTF::Model model;

	struct {
		vks::Buffer object;
		vks::Buffer params;
	} uniformBuffers;

	struct UBOMatrices {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec3 camPos;
	} uboMatrices;

	struct LightSource {
		glm::vec3 color = glm::vec3(1.0f);
		glm::vec3 rotation = glm::vec3(-150.0f, -125.0f, 0.0f);
	} lightSource;

	struct UBOParams {
		glm::vec4 lightDir;
	} uboParams;

#ifdef DEFERRED_OPAQUE
	struct {
		std::unique_ptr<vks::Framebuffer> geometry;
	}passes;

	struct {
		VkDescriptorSetLayout geometry;
		VkDescriptorSetLayout shading;
	}dsLayoutsCustomDeferred;

	struct {
		VkDescriptorSet geometry;
		VkDescriptorSet shading;
	}descriptorSetsDeferred;

	struct {
		VkPipelineLayout geometry;
		VkPipelineLayout shading;
	}ppLayoutsDeferred;

	struct {
		VkPipeline geometry;
		VkPipeline shading;
	}pipelinesDeferred;

	bool isFirst = true;
#endif

	VkPipelineLayout pipelineLayout;
	struct {
		VkPipeline pbr;
		VkPipeline pbrAlphaBlend;
	}pipelines;

	VkDescriptorSetLayout dsLayoutCustom;
	VkDescriptorSet dsCustom;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Physical based shading basics";

		camera.type = Camera::CameraType::firstperson;
		camera.setPosition(glm::vec3(10.0f, 13.0f, 1.8f));
		camera.setRotation(glm::vec3(-62.5f, 90.0f, 0.0f));
		camera.movementSpeed = 4.0f;
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.rotationSpeed = 0.25f;

		//camera.type = Camera::CameraType::lookat;
		//camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
		//camera.rotationSpeed = 0.25f;
		//camera.movementSpeed = 0.1f;
		//camera.setPosition({ 0.0f, 0.0f, 1.0f });
		//camera.setRotation({ 0.0f, 0.0f, 0.0f });

		//paused = true;
		timerSpeed *= 0.25f;

		settings.overlay = true;

		vkglTF::descriptorBindingFlags |= vkglTF::DescriptorBindingFlags::ImageNormalMap | vkglTF::DescriptorBindingFlags::ImageMetallicRoughness;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.pbr, nullptr);
		vkDestroyPipeline(device, pipelines.pbrAlphaBlend, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, dsLayoutCustom, nullptr);

#ifdef DEFERRED_OPAQUE
		vkDestroyPipeline(device, pipelinesDeferred.geometry, nullptr);
		vkDestroyPipeline(device, pipelinesDeferred.shading, nullptr);

		vkDestroyPipelineLayout(device, ppLayoutsDeferred.geometry, nullptr);
		vkDestroyPipelineLayout(device, ppLayoutsDeferred.shading, nullptr);

		vkDestroyDescriptorSetLayout(device, dsLayoutsCustomDeferred.geometry, nullptr);
		vkDestroyDescriptorSetLayout(device, dsLayoutsCustomDeferred.shading, nullptr);
#endif

		uniformBuffers.object.destroy();
		uniformBuffers.params.destroy();
	}

	virtual void getEnabledFeatures() {
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
	}

#ifdef DEFERRED_OPAQUE
	virtual void setupRenderPass() override {
		std::array<VkAttachmentDescription, 2> attachments = {};
		// Color attachment
		attachments[0].format = swapChain.colorFormat;
		attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		// Depth attachment
		attachments[1].format = depthFormat;
		attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // [POI] Use the depth of opaque geometry
		attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = {};
		colorReference.attachment = 0;
		colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;
		subpassDescription.pResolveAttachments = nullptr;

		// Subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
	}

	virtual void setupFrameBuffer() override {
		VulkanExampleBase::setupFrameBuffer();
		setupGeometryPass();
	}

	void setupGeometryPass() {
		if (isFirst) {
			isFirst = false;
			return;
		}

		bool recreate = false;
		if (passes.geometry) {
			recreate = true;
			passes.geometry->clearBeforeRecreate(FB_WIDTH, FB_HEIGHT);
		}
		else {
			passes.geometry = std::make_unique<vks::Framebuffer>(vulkanDevice, FB_WIDTH, FB_HEIGHT);
		}

		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // Position
		passes.geometry->addAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // Normal	
		passes.geometry->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // Base Color
		passes.geometry->addAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT); // Physical

		vks::FramebufferAttachment depth;
		depth.image = depthStencil.image;
		depth.memory = depthStencil.mem;
		depth.view = depthStencil.view;
		depth.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
		depth.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
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
		passes.geometry->attachments.push_back(depth);

		if (recreate) {
			passes.geometry->createFrameBuffer();

			VkDescriptorImageInfo descriptorPos =
			{ passes.geometry->sampler, passes.geometry->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo descriptorNormal =
			{ passes.geometry->sampler, passes.geometry->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo descriptorBaseColor =
			{ passes.geometry->sampler, passes.geometry->attachments[2].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			VkDescriptorImageInfo descriptorPhysical =
			{ passes.geometry->sampler, passes.geometry->attachments[3].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.object.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers.params.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorPos),
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &descriptorNormal),
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &descriptorBaseColor),
				vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &descriptorPhysical)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

			return;
		}

		VK_CHECK_RESULT(passes.geometry->createSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
		VK_CHECK_RESULT(passes.geometry->createRenderPass());

		/*
			create dsLayout, pipelineLayout
		*/
		std::vector<VkDescriptorSetLayoutBinding> dsLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
		};
		VkDescriptorSetLayoutCreateInfo dsLayoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(dsLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &dsLayoutsCustomDeferred.geometry));

		std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayoutsCustomDeferred.geometry, vkglTF::descriptorSetLayoutImage };
		VkPipelineLayoutCreateInfo pipelineLayoutCI =
			vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

		VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vkglTF::PushConstBlockMaterial) };
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &ppLayoutsDeferred.geometry));

		dsLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5)
		};
		dsLayoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(dsLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &dsLayoutInfo, nullptr, &dsLayoutsCustomDeferred.shading));
		pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&dsLayoutsCustomDeferred.shading, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &ppLayoutsDeferred.shading));

		/*
			create pipeline
		*/
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		std::array<VkPipelineColorBlendAttachmentState, 4> blendAttachmentStates = {
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(blendAttachmentStates.size(), blendAttachmentStates.data());
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(ppLayoutsDeferred.geometry, passes.geometry->renderPass);
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
			{
				vkglTF::VertexComponent::Position,
				vkglTF::VertexComponent::Normal,
				vkglTF::VertexComponent::UV
			}
		);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		shaderStages[0] = loadShader(getShadersPath() + "pbr/spirv/pbr_deferred_geom.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pbr/spirv/pbr_deferred_geom.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelinesDeferred.geometry));

		pipelineCI.layout = ppLayoutsDeferred.shading;
		pipelineCI.renderPass = renderPass;

		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;

		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;

		depthStencilState.depthTestEnable = VK_FALSE;
		depthStencilState.depthWriteEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		pipelineCI.pColorBlendState = &colorBlendState;

		shaderStages[0] = loadShader(getShadersPath() + "pbr/spirv/pbr_deferred_shad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pbr/spirv/pbr_deferred_shad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelinesDeferred.shading));
	}
#endif

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

#ifdef DEFERRED_OPAQUE
		std::array<VkClearValue, 5> geomClearValues;
		geomClearValues[0].color = { 0.0f, 0.0f, 0.0f, 0.0f };
		geomClearValues[1].color = { 0.0f, 0.0f, 0.0f, 0.0f };
		geomClearValues[2].color = { 0.0f, 0.0f, 0.0f, 0.0f };
		geomClearValues[3].color = { 0.0f, 0.0f, 0.0f, 0.0f };
		geomClearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo geomPassInfo = vks::initializers::renderPassBeginInfo();
		geomPassInfo.renderPass = passes.geometry->renderPass;
		geomPassInfo.framebuffer = passes.geometry->framebuffer;
		geomPassInfo.renderArea.extent.width = passes.geometry->width;
		geomPassInfo.renderArea.extent.height = passes.geometry->height;
		geomPassInfo.clearValueCount = static_cast<uint32_t>(geomClearValues.size());
		geomPassInfo.pClearValues = geomClearValues.data();
#endif

		VkClearValue clearValues[2];
		clearValues[0].color = { 1.0, 1.0, 1.0, 1.0 };
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
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

#ifdef DEFERRED_OPAQUE
			{
				vkCmdBeginRenderPass(drawCmdBuffers[i], &geomPassInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)passes.geometry->width, (float)passes.geometry->height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				VkRect2D scissor = vks::initializers::rect2D(passes.geometry->width, passes.geometry->height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
				VkDeviceSize offsets[1] = { 0 };

				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
				if (model.indices.buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesDeferred.geometry);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ppLayoutsDeferred.geometry, 0, 1, &descriptorSetsDeferred.geometry, 0, NULL);

				for (auto node : model.nodes) {
					model.drawNode(
						node, drawCmdBuffers[i],
						vkglTF::RenderFlags::RenderOpaqueNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
						ppLayoutsDeferred.geometry, 1
					);
				}

				for (auto node : model.nodes) {
					model.drawNode(
						node, drawCmdBuffers[i],
						vkglTF::RenderFlags::RenderAlphaMaskedNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
						ppLayoutsDeferred.geometry, 1
					);
				}

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
#endif
			{
				renderPassBeginInfo.framebuffer = frameBuffers[i];

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
				VkDeviceSize offsets[1] = { 0 };

#ifdef DEFERRED_OPAQUE
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinesDeferred.shading);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, ppLayoutsDeferred.shading, 0, 1, &descriptorSetsDeferred.shading, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
				if (model.indices.buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsCustom, 0, NULL);
#else
				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &model.vertices.buffer, offsets);
				if (model.indices.buffer != VK_NULL_HANDLE) {
					vkCmdBindIndexBuffer(drawCmdBuffers[i], model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				}

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbr);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &dsCustom, 0, NULL);
				//q7.draw(drawCmdBuffers[i], vkglTF::RenderFlags::BindImages, pipelineLayout);

				for (auto node : model.nodes) {
					model.drawNode(
						node, drawCmdBuffers[i],
						vkglTF::RenderFlags::RenderOpaqueNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
						pipelineLayout, 1
					);
				}

				for (auto node : model.nodes) {
					model.drawNode(
						node, drawCmdBuffers[i],
						vkglTF::RenderFlags::RenderAlphaMaskedNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
						pipelineLayout, 1
					);
				}
#endif

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.pbrAlphaBlend);

				for (auto node : model.nodes) {
					model.drawNode(
						node, drawCmdBuffers[i],
						vkglTF::RenderFlags::RenderAlphaBlendedNodes | vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::BindPBRMaterial,
						pipelineLayout, 1
					);
				}

				drawUI(drawCmdBuffers[i]);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void loadAssets(){	
		model.loadFromFile(
			//getAssetPath() + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf",
			//getAssetPath() + "models/lamborghini/scene.gltf",
			//getAssetPath() + "models/toyota/scene.gltf",
			//getAssetPath() + "models/sponza/sponza.gltf",
			getAssetPath() + "models/audi/scene.gltf",
			vulkanDevice,
			queue,
			vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY
		);
	}

	void setupDescriptorSetLayout()
	{
		// Descriptor set layout for ds0
		// ds1 have built in vkglTF for each material
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &dsLayoutCustom));

		// Pipeline Layout
		std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayoutCustom, vkglTF::descriptorSetLayoutImage };
		VkPipelineLayoutCreateInfo pipelineLayoutCI = 
			vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));

		VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vkglTF::PushConstBlockMaterial) };
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));
	}

	void setupDescriptorSets()
	{
		// Descriptor Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(poolSizes, 3);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayoutCustom, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &dsCustom));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(dsCustom, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.object.descriptor),
			vks::initializers::writeDescriptorSet(dsCustom, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers.params.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

#ifdef DEFERRED_OPAQUE
		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayoutsCustomDeferred.geometry, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetsDeferred.geometry));

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.geometry, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.object.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayoutsCustomDeferred.shading, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSetsDeferred.shading));

		VkDescriptorImageInfo descriptorPos = 
			{ passes.geometry->sampler, passes.geometry->attachments[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorNormal = 
			{ passes.geometry->sampler, passes.geometry->attachments[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorBaseColor =
			{ passes.geometry->sampler, passes.geometry->attachments[2].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo descriptorPhysical =
			{ passes.geometry->sampler, passes.geometry->attachments[3].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.object.descriptor),
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, &uniformBuffers.params.descriptor),
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &descriptorPos),
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &descriptorNormal),
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &descriptorBaseColor),
			vks::initializers::writeDescriptorSet(descriptorSetsDeferred.shading, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &descriptorPhysical)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
#endif
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =  vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ 
			vkglTF::VertexComponent::Position,
			vkglTF::VertexComponent::Normal,
			vkglTF::VertexComponent::UV
		});

		// PBR pipeline
		shaderStages[0] = loadShader(getShadersPath() + "pbr/spirv/pbr_khr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pbr/spirv/pbr_khr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbr));

		rasterizationState.cullMode = VK_CULL_MODE_NONE;

		depthStencilState.depthWriteEnable = VK_FALSE;

		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.pbrAlphaBlend));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Object vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.object,
			sizeof(uboMatrices)));

		// Shared parameter uniform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.params,
			sizeof(uboParams)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.object.map());
		VK_CHECK_RESULT(uniformBuffers.params.map());

		updateUniformBuffers();
		updateLights();
	}

	void updateUniformBuffers()
	{
		// 3D object
		uboMatrices.projection = camera.matrices.perspective;
		uboMatrices.view = camera.matrices.view;
		uboMatrices.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		uboMatrices.camPos = camera.position * -1.0f;
		memcpy(uniformBuffers.object.mapped, &uboMatrices, sizeof(uboMatrices));
	}

	void updateLights()
	{
		uboParams.lightDir = glm::vec4(
			sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
			sin(glm::radians(lightSource.rotation.y)),
			cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
			0.0f);

		memcpy(uniformBuffers.params.mapped, &uboParams, sizeof(uboParams));
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
#ifdef DEFERRED_OPAQUE
		setupGeometryPass();
#endif
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorSets();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Light Direction")) {
			if (overlay->sliderFloat("x", &lightSource.rotation.x, -180.0f, 180.0f)) {
				updateLights();
			}
			if (overlay->sliderFloat("y", &lightSource.rotation.y, -180.0f, 180.0f)) {
				updateLights();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()