#include "vulkanexamplebase.h"

#include "SimScene.h"

#define ENABLE_VALIDATION true
#define USE_STAGING true

class VulkanExample : public VulkanExampleBase {
	SimScene scene;

	struct {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	} uniforms;
	vks::Buffer uniformBuffer;

	VkDescriptorSetLayout dsLayout;
	VkPipelineLayout pLayout;
	VkPipeline pipeline;
	VkDescriptorSet descriptorSet;

	VkQueryPool queryPool;
	// Vector for storing pipeline statistics results
	std::vector<uint64_t> pipelineStats;
	std::vector<std::string> pipelineStatNames;

public:
	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Load Package Example";
		camera.type = Camera::CameraType::firstperson;
		camera.flipY = true;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -2.5f));
		camera.setRotation(glm::vec3(0.0f, 15.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, dsLayout, nullptr);
		vkDestroyQueryPool(device, queryPool, nullptr);

		uniformBuffer.destroy();

		scene.destroy();
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

	void prepareStatistics()
	{
		pipelineStatNames = {
			"Input assembly vertex count        ",
			"Input assembly primitives count    ",
			"Vertex shader invocations          ",
			"Clipping stage primitives processed",
			"Clipping stage primitives output    ",
			"Fragment shader invocations        "
		};
		pipelineStats.resize(pipelineStatNames.size());

		VkQueryPoolCreateInfo queryPoolInfo = {};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		// This query pool will store pipeline statistics
		queryPoolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		// Pipeline counters to be returned for this pool
		queryPoolInfo.pipelineStatistics =
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
			VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
			VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
		queryPoolInfo.queryCount = 6;
		VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, NULL, &queryPool));
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

	void prepareUniformBuffer() {
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer,
			sizeof(uniforms),
			&uniforms));
		VK_CHECK_RESULT(uniformBuffer.map());

		uniforms.model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		updateUniformBuffers();
	}

	void updateUniformBuffers(){
		uniforms.view = camera.matrices.view;
		uniforms.projection = camera.matrices.perspective;
		memcpy(uniformBuffer.mapped, &uniforms, sizeof(uniforms));
	}

	void setupLayouts() {
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &dsLayout));

		std::array<VkDescriptorSetLayout, 2> setLayouts = { dsLayout, scene.m_dsLayout };
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pLayout));
	}

	void prepareDescriptorSet() {
		// Descriptor pool
		//
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Allocate descriptor set
		//
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &dsLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor)
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipeline() {
		VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		// Binding description
		std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
			vks::initializers::vertexInputBindingDescription(0, sizeof(SimScene::Geometry::Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
		};
		// Attribute descriptions
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, pos)),
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, normal)),
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv0)),
			vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv1)),
			vks::initializers::vertexInputAttributeDescription(0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv2)),
			vks::initializers::vertexInputAttributeDescription(0, 5, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SimScene::Geometry::Vertex, uv3))
		};

		inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		inputState.pVertexBindingDescriptions = bindingDescriptions.data();
		inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShadersPath() + "loadPackage/spirv/package.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "loadPackage/spirv/package.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		struct SpecData {
			uint32_t texCount;
		} specData;
		specData.texCount = scene.textures.size();
		std::array<VkSpecializationMapEntry, 1> specMapEntries = {
			vks::initializers::specializationMapEntry(0, offsetof(SpecData, texCount), sizeof(SpecData::texCount))
		};
		VkSpecializationInfo specInfo = vks::initializers::specializationInfo(static_cast<uint32_t>(specMapEntries.size()), specMapEntries.data(), sizeof(SpecData), &specData);
		shaderStages[1].pSpecializationInfo = &specInfo;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(pLayout, renderPass, 0);
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

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	virtual void buildCommandBuffers() override {
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { 0.18f, 0.27f, 0.5f, 0.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			// Reset timestamp query pool
			vkCmdResetQueryPool(drawCmdBuffers[i], queryPool, 0, static_cast<uint32_t>(pipelineStats.size()));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 0, 0);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			scene.draw(drawCmdBuffers[i], pLayout);

			vkCmdEndQuery(drawCmdBuffers[i], queryPool, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

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

		uint32_t count = static_cast<uint32_t>(pipelineStats.size());
		vkGetQueryPoolResults(
			device,
			queryPool,
			0,
			1,
			count * sizeof(uint64_t),
			pipelineStats.data(),
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);

		VulkanExampleBase::submitFrame();
	}

	void prepare() {
		VulkanExampleBase::prepare();

		prepareStatistics();

		prepareUniformBuffer();
		scene.init(vulkanDevice, this, getAssetPath() + "models/mzq/Jetta_a_008_test_0011.simpkg");
		//scene.init(vulkanDevice, this, getAssetPath() + "models/mzq/Car_01.simpkg");
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
		updateUniformBuffers();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay){
		if (!pipelineStats.empty()) {
			if (overlay->header("Pipeline statistics")) {
				for (auto i = 0; i < pipelineStats.size(); i++) {
					std::string caption = pipelineStatNames[i] + ": %d";
					overlay->text(caption.c_str(), pipelineStats[i]);
				}
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()