#include "ParticleEffect.h"

static float random(float min, float max) {
	return min + (max - min)*(float)rand() / (float)RAND_MAX;
}

ParticleEffect::ParticleEffect()
{

}

void ParticleEffect::init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, VkRenderPass renderPass)
{
	m_vkDevice = vkDevice;
	m_example = example;
	m_renderPass = renderPass;

	prepareDescriptorSetLayout();
	preparePipelineLayout();
	prepareUniforms();
	prepareDescriptorSet();

	// Prepare geometries for each type of particle (quad, line, point)
	createGeometry(MAX_PARTICLE_COUNT_PER_CELL);

	cull();
}

void ParticleEffect::update()
{
	// Time update
	uniforms.time = m_simuTime;
	m_simuTime += SIMU_DELTA_TIME;

	// When camera's view matrix updated
	if (m_example->camera.updated) {
		
	}

	// Particle update

	updateUniformBuffer();
}

void ParticleEffect::draw(VkCommandBuffer cb)
{
	VkDeviceSize offsets[1] = { 0 };
	// Binding point 0 : Mesh vertex buffer
	vkCmdBindVertexBuffers(cb, VERTEX_BUFFER_ID, 1, &m_vertexBuffer.buffer, offsets);
	// Binding point 1 : Instance data buffer
	vkCmdBindVertexBuffers(cb, INSTANCE_BUFFER_ID, 1, &m_instanceBuffer.buffer, offsets);
	// Bind index buffer
	vkCmdBindIndexBuffer(cb, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pLayout, 0, 1, &m_descriptorSet, 0, NULL);

	m_pointDrawable.draw(cb, m_pLayout);
	m_lineDrawable.draw(cb, m_pLayout);
	m_quadDrawable.draw(cb, m_pLayout);
}

void ParticleEffect::destroy()
{
	vkDestroyDescriptorPool(m_vkDevice->logicalDevice, m_descriptorPool, nullptr);

	m_quadDrawable.destroy(m_vkDevice->logicalDevice);
	m_lineDrawable.destroy(m_vkDevice->logicalDevice);
	m_pointDrawable.destroy(m_vkDevice->logicalDevice);

	vkDestroyPipelineLayout(m_vkDevice->logicalDevice, m_pLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, m_dsLayout, nullptr);

	m_vertexBuffer.destroy();
	m_instanceBuffer.destroy();
	m_indexBuffer.destroy();

	m_uniformBuffer.destroy();
	m_texRaindrop.destroy();
}

void ParticleEffect::rain(float intensity)
{
	m_wind = glm::vec3(0.0f);
	m_particleSpeed = -2.0 + (-5.0 * intensity);
	m_particleSize = 0.01 + 0.02 * intensity;
	m_particleColor = glm::vec4(glm::vec3(0.6 - 0.1 * intensity), 1.0 - intensity);
	m_maxParticleDensity = intensity * 8.5;
	m_cellSize = glm::vec3(5.0 / (0.25 + intensity), 5.0 / (0.25 + intensity), 5.0);
	m_nearTransition = 25.0;
	m_farTransition = 100.0 - 60.0 * sqrtf(intensity);

	float length_u = m_cellSize.x;
	float length_v = m_cellSize.y;
	float length_w = m_cellSize.z;

	// time taken to get from start to the end of cycle
	m_period = fabsf(m_cellSize.z / m_particleSpeed);

	m_du = glm::vec3(length_u, 0.0f, 0.0f);
	m_dv = glm::vec3(0.0f, length_v, 0.0f);
	m_dw = glm::vec3(0.0f, 0.0f, length_w);

	m_duInv = glm::vec3(1.0f / length_u, 0.0f, 0.0f);
	m_dvInv = glm::vec3(0.0f, 1.0f / length_v, 0.0f);
	m_dwInv = glm::vec3(0.0f, 0.0f, 1.0f / length_w);

	uniforms.particleColor = m_particleColor;
	uniforms.particleSize = m_particleSize;
	uniforms.inversePeriod = 1.0f / m_period;

	updateUniformBuffer();
}

void ParticleEffect::snow(float intensity)
{
	m_wind = glm::vec3(0.0f);
	m_particleSpeed = -0.75 - (0.25 * intensity);
	m_particleSize = 0.02 + 0.03 * intensity;
	m_particleColor = glm::vec4(glm::vec3(0.85 - 0.1 * intensity), 1.0 - intensity);
	m_maxParticleDensity = intensity * 8.2;
	m_cellSize = glm::vec3(5.0 / (0.25 + intensity), 5.0 / (0.25 + intensity), 5.0);
	m_nearTransition = 25.0;
	m_farTransition = 100.0 - 60.0 * sqrtf(intensity);

	float length_u = m_cellSize.x;
	float length_v = m_cellSize.y;
	float length_w = m_cellSize.z;

	// time taken to get from start to the end of cycle
	m_period = fabsf(m_cellSize.z / m_particleSpeed);

	m_du = glm::vec3(length_u, 0.0f, 0.0f);
	m_dv = glm::vec3(0.0f, length_v, 0.0f);
	m_dw = glm::vec3(0.0f, 0.0f, length_w);

	m_duInv = glm::vec3(1.0f / length_u, 0.0f, 0.0f);
	m_dvInv = glm::vec3(0.0f, 1.0f / length_v, 0.0f);
	m_dwInv = glm::vec3(0.0f, 0.0f, 1.0f / length_w);

	uniforms.particleColor = m_particleColor;
	uniforms.particleSize = m_particleSize;
	uniforms.inversePeriod = 1.0f / m_period;

	updateUniformBuffer();
}

void ParticleEffect::cull()
{
	float cellVolume = m_cellSize.x * m_cellSize.y * m_cellSize.z;
	int numberOfParticles = (int)(m_maxParticleDensity * cellVolume);

	if (numberOfParticles == 0) return;

	m_quadDrawable.setDrawInstanceCountPerCell(numberOfParticles);
	m_lineDrawable.setDrawInstanceCountPerCell(numberOfParticles);
	m_pointDrawable.setDrawInstanceCountPerCell(numberOfParticles);

	m_quadDrawable.newFrame();
	m_lineDrawable.newFrame();
	m_pointDrawable.newFrame();

	statisticCullFrustum = 0;
	statisticCullFar = 0;

	glm::mat4 inverse_modelview;
	glm::mat4 mv = m_example->camera.matrices.view * matToOSG;
	inverse_modelview = inverse(mv);

	glm::vec3 eyeLocal = inverse_modelview * glm::vec4(glm::vec3(0.0), 1.0);

	float eye_k = glm::dot((eyeLocal - m_origin), m_dwInv);
	glm::vec3 eye_kPlane = eyeLocal - m_dw * eye_k - m_origin;

	float eye_i = glm::dot(eye_kPlane, m_duInv);
	float eye_j = glm::dot(eye_kPlane, m_dvInv);

	glm::mat4 p = m_example->camera.matrices.perspective;
	glm::mat4 convertDepth(
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 2.0, 0.0,
		0.0, 0.0, -1.0, 1.0
	);
	//p = convertDepth * p;
	osg::Matrix proj(
		p[0][0], p[0][1], p[0][2], p[0][3],
		p[1][0], p[1][1], p[1][2], p[1][3],
		p[2][0], p[2][1], p[2][2], p[2][3],
		p[3][0], p[3][1], p[3][2], p[3][3]
	);

	osg::Matrix modelview(
		mv[0][0], mv[0][1], mv[0][2], mv[0][3],
		mv[1][0], mv[1][1], mv[1][2], mv[1][3],
		mv[2][0], mv[2][1], mv[2][2], mv[2][3],
		mv[3][0], mv[3][1], mv[3][2], mv[3][3]
	);

	osg::Polytope frustum;
	frustum.setToUnitFrustum(false, false);
	frustum.transformProvidingInverse(proj);
	frustum.transformProvidingInverse(modelview);

	float i_delta = m_farTransition * m_duInv.x;
	float j_delta = m_farTransition * m_dvInv.y;
	float k_delta = 1; //_nearTransition * _inverse_dw.z();

	int i_min = (int)floor(eye_i - i_delta);
	int j_min = (int)floor(eye_j - j_delta);
	int k_min = (int)floor(eye_k - k_delta);

	int i_max = (int)ceil(eye_i + i_delta);
	int j_max = (int)ceil(eye_j + j_delta);
	int k_max = (int)ceil(eye_k + k_delta);

	unsigned int numTested = 0;
	unsigned int numInFrustum = 0;

	float iCyle = 0.43f;
	float jCyle = 0.64f;

	for (int i = i_min; i <= i_max; ++i)
	{
		for (int j = j_min; j <= j_max; ++j)
		{
			for (int k = k_min; k <= k_max; ++k)
			{
				float startTime = (float)(i)*iCyle + (float)(j)*jCyle;
				startTime = (startTime - floor(startTime)) * m_period;

				if (build(eyeLocal, i, j, k, startTime, frustum)) ++numInFrustum;
				++numTested;
			}
		}
	}
}

uint32_t ParticleEffect::statisticCullFrustum = 0;
uint32_t ParticleEffect::statisticCullFar = 0;

bool ParticleEffect::build(glm::vec3& eyeLocal, int i, int j, int k, float startTime, osg::Polytope& frustum)
{
	glm::vec3 position = m_origin + glm::vec3(float(i) * m_du.x, float(j) * m_dv.y, float(k + 1) * m_dw.z);
	glm::vec3 scale(m_du.x, m_dv.y, -m_dw.z);

	{
		osg::Vec3 position = osg::Vec3(m_origin.x, m_origin.y, m_origin.z) + osg::Vec3(float(i) * m_du.x, float(j) * m_dv.y, float(k + 1) * m_dw.z);
		osg::Vec3 scale(m_du.x, m_dv.y, -m_dw.z);

		osg::BoundingBox bb(position.x(), position.y(), position.z() + scale.z(),
			position.x() + scale.x(), position.y() + scale.y(), position.z());

		if (!frustum.contains(bb)) {
			++statisticCullFrustum;
			return false;
		}
	}

	glm::vec3 center = position + scale * 0.5f;
	float distance = glm::length(center - eyeLocal);

	glm::mat4* mymodelview = 0;
	if (distance < m_nearTransition) {
		ParticalDrawable::DepthMatrixStartTime& mstp = m_quadDrawable.m_curCellMatrixMap[ParticalDrawable::Cell(i, k, j)];
		mstp.depth = distance;
		mstp.startTime = startTime;
		mymodelview = &mstp.modelview;
	}
	else if (distance <= m_farTransition) {
		ParticalDrawable::DepthMatrixStartTime& mstp = m_pointDrawable.m_curCellMatrixMap[ParticalDrawable::Cell(i, k, j)];
		mstp.depth = distance;
		mstp.startTime = startTime;
		mymodelview = &mstp.modelview;
	}
	else {
		++statisticCullFar;
		return false;
	}

	*mymodelview = m_example->camera.matrices.view * matToOSG;
	*mymodelview = glm::translate(*mymodelview, position);
	*mymodelview = glm::scale(*mymodelview, scale);

	//cv->updateCalculatedNearFar(*(cv->getModelViewMatrix()), bb);

	return true;
}

void ParticleEffect::createGeometry(uint32_t numParticles)
{
	std::vector<VertexData> vertices = {
		// Quad
		{{ 0.0f, 0.0f }},
		{{ 0.0f, 1.0f }},
		{{ 1.0f, 0.0f }},
		{{ 1.0f, 1.0f }},
		// Line
		{{ 0.5f, 0.0f }},
		{{ 0.5f, 1.0f }},
		// Point
		{{ 0.5f, 0.5f }}
	};

	std::vector<InstanceData> instances;
	instances.reserve(3 * numParticles);
	for (uint32_t i = 0; i < 3 * numParticles; ++i) {
		glm::vec3 randomPos(random(0.0f, 1.0f), random(0.0f, 1.0f), random(0.0f, 1.0f));

		InstanceData instance = { randomPos };
		instances.push_back(instance);
	}

	std::vector<uint32_t> indices = {
		// Quad
		0,1,2, 1,3,2,
		// Line
		4,5,
		// Point
		6
	};

	m_quadDrawable.init(m_example, m_vkDevice->logicalDevice, m_renderPass, m_pLayout, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 6, numParticles, 0, 0);
	m_lineDrawable.init(m_example, m_vkDevice->logicalDevice, m_renderPass, m_pLayout, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 2, numParticles, 6, numParticles);
	m_pointDrawable.init(m_example, m_vkDevice->logicalDevice, m_renderPass, m_pLayout, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, 1, numParticles, 8, 2 * numParticles);

	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_vertexBuffer,
		vertices.size() * sizeof(VertexData),
		vertices.data()));

	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_instanceBuffer,
		instances.size() * sizeof(InstanceData),
		instances.data()));

	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_indexBuffer,
		indices.size() * sizeof(uint32_t),
		indices.data()));
}

void ParticleEffect::prepareUniforms()
{
	// Uniform Buffer
	//
	VK_CHECK_RESULT(m_vkDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&m_uniformBuffer,
		sizeof(uniforms)));

	VK_CHECK_RESULT(m_uniformBuffer.map());

	// Time
	uniforms.time = m_simuTime;

	// Camera
	uniforms.projection = m_example->camera.matrices.perspective;

	// Particle
	rain(0.2f); // Update uniform happens here

	// Uniform Texture
	//
	createSpotLightImage(glm::vec4(1.0f), glm::vec4(glm::vec3(1.0f), 0.0f), 32, 1.0f);
}

void ParticleEffect::updateUniformBuffer()
{
	memcpy(m_uniformBuffer.mapped, &uniforms, sizeof(uniforms));
}

void ParticleEffect::createSpotLightImage(glm::vec4& centerColor, glm::vec4& backgroundColor, uint32_t size, float power)
{
	std::vector<unsigned char> imageData(size * size * 4);
	fillSpotLightImage(imageData.data(), centerColor, backgroundColor, size, power);
	m_texRaindrop.fromBuffer(imageData.data(), imageData.size(), VK_FORMAT_R8G8B8A8_UNORM, size, size, m_vkDevice, m_example->queue, VK_FILTER_NEAREST);
}

void ParticleEffect::fillSpotLightImage(unsigned char* ptr, glm::vec4& centerColor, glm::vec4& backgroundColor, uint32_t size, float power)
{
	if (size == 1)
	{
		float r = 0.5f;
		glm::vec4 color = centerColor * r + backgroundColor * (1.0f - r);
		*ptr++ = (unsigned char)((color[0])*255.0f);
		*ptr++ = (unsigned char)((color[1])*255.0f);
		*ptr++ = (unsigned char)((color[2])*255.0f);
		*ptr++ = (unsigned char)((color[3])*255.0f);
		return;
	}

	float mid = (float(size) - 1.0f)*0.5f;
	float div = 2.0f / float(size);
	for (unsigned int row = 0; row < size; ++row)
	{
		//unsigned char* ptr = image->data(0,r,0);
		for (unsigned int col = 0; col < size; ++col)
		{
			float dx = (float(col) - mid)*div;
			float dy = (float(row) - mid)*div;
			float r = powf(1.0f - sqrtf(dx*dx + dy * dy), power);
			if (r < 0.0f) r = 0.0f;
			glm::vec4 color = centerColor * r + backgroundColor * (1.0f - r);
			*ptr++ = (unsigned char)((color[0])*255.0f);
			*ptr++ = (unsigned char)((color[1])*255.0f);
			*ptr++ = (unsigned char)((color[2])*255.0f);
			*ptr++ = (unsigned char)((color[3])*255.0f);
		}
	}
}

void ParticleEffect::prepareDescriptorSetLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
	};

	VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &m_dsLayout));
}

void ParticleEffect::preparePipelineLayout()
{
	VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&m_dsLayout, 1);

	VkPushConstantRange pushConstantRange{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ParticalDrawable::PushConstCell) };
	pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK_RESULT(vkCreatePipelineLayout(m_vkDevice->logicalDevice, &pPipelineLayoutCreateInfo, nullptr, &m_pLayout));
}

void ParticleEffect::prepareDescriptorSet()
{
	std::vector<VkDescriptorPoolSize> poolSizes = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
	};

	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
	VK_CHECK_RESULT(vkCreateDescriptorPool(m_vkDevice->logicalDevice, &descriptorPoolInfo, nullptr, &m_descriptorPool));

	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_dsLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_descriptorSet));
	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &m_uniformBuffer.descriptor),
		vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &m_texRaindrop.descriptor)
	};
	vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

//
// ParticalDrawable
//

void ParticleEffect::ParticalDrawable::init(VulkanExampleBase* example, VkDevice device, VkRenderPass renderPass, VkPipelineLayout pLayout, VkPrimitiveTopology topology, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t firstInstance)
{
	if (m_pipeline != VK_NULL_HANDLE)
		return;

	m_indexCount = indexCount;
	m_instanceCount = instanceCount;
	m_firstIndex = firstIndex;
	m_firstInstance = firstInstance;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(topology, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentState = {};
	blendAttachmentState.blendEnable = VK_TRUE;
	blendAttachmentState.colorWriteMask = 0xF;
	blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; // VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
	VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pLayout, renderPass);
	pipelineCI.pInputAssemblyState = &inputAssemblyState;
	pipelineCI.pRasterizationState = &rasterizationState;
	pipelineCI.pColorBlendState = &colorBlendState;
	pipelineCI.pMultisampleState = &multisampleState;
	pipelineCI.pViewportState = &viewportState;
	pipelineCI.pDepthStencilState = &depthStencilState;
	pipelineCI.pDynamicState = &dynamicState;
	pipelineCI.stageCount = shaderStages.size();
	pipelineCI.pStages = shaderStages.data();

	// This example uses two different input states, one for the instanced part and one for non-instanced rendering
	VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
		vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_ID, sizeof(VertexData), VK_VERTEX_INPUT_RATE_VERTEX),
		vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_ID, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
	};
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
		// Per-vertex attributes
		vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_ID, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexData, uv)),
		// Per-Instance attributes
		vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_ID, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos))
	};
	inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
	inputState.pVertexBindingDescriptions = bindingDescriptions.data();
	inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

	pipelineCI.pVertexInputState = &inputState;


	if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) {
		shaderStages[0] = example->loadShader(example->getShadersPath() + "final/spirv/particleQuad.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		VkSpecializationMapEntry entry = vks::initializers::specializationMapEntry(0, 0, sizeof(float));
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &entry, sizeof(float), &SIMU_DELTA_TIME);
		shaderStages[0].pSpecializationInfo = &specializationInfo;

		shaderStages[1] = example->loadShader(example->getShadersPath() + "final/spirv/particle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else if (topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST) {
		shaderStages[0] = example->loadShader(example->getShadersPath() + "final/spirv/particleLine.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		VkSpecializationMapEntry entry = vks::initializers::specializationMapEntry(0, 0, sizeof(float));
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &entry, sizeof(float), &SIMU_DELTA_TIME);
		shaderStages[0].pSpecializationInfo = &specializationInfo;

		shaderStages[1] = example->loadShader(example->getShadersPath() + "final/spirv/particle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else if (topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) {
		shaderStages[0] = example->loadShader(example->getShadersPath() + "final/spirv/particlePoint.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = example->loadShader(example->getShadersPath() + "final/spirv/particlePoint.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	else {
		assert(false);
	}	

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, example->pipelineCache, 1, &pipelineCI, nullptr, &m_pipeline));
}

void ParticleEffect::ParticalDrawable::draw(VkCommandBuffer cb, VkPipelineLayout pLayout)
{
	typedef std::vector<const CellMatrixMap::value_type*> DepthMatrixStartTimeVector;
	DepthMatrixStartTimeVector orderedEntries;
	orderedEntries.reserve(m_curCellMatrixMap.size());

	for (CellMatrixMap::const_iterator citr = m_curCellMatrixMap.begin();
		citr != m_curCellMatrixMap.end();
		++citr)
	{
		orderedEntries.push_back(&(*citr));
	}

	std::sort(orderedEntries.begin(), orderedEntries.end(), LessFunctor());


	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	for (DepthMatrixStartTimeVector::reverse_iterator itr = orderedEntries.rbegin();
		itr != orderedEntries.rend();
		++itr)
	{
		PushConstCell pushConstantPerCell{};
		pushConstantPerCell.modelView = (*itr)->second.modelview;
		pushConstantPerCell.startTime = (*itr)->second.startTime;
		vkCmdPushConstants(cb, pLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstCell), &pushConstantPerCell);

		// Render instances
		vkCmdDrawIndexed(cb, m_indexCount, m_instanceCount, m_firstIndex, 0, m_firstInstance);
	}
}

void ParticleEffect::ParticalDrawable::destroy(VkDevice device)
{
	if (m_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(device, m_pipeline, nullptr);
	}

	m_curCellMatrixMap.clear();
}
