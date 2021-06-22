/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2020 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Render a complete scene loaded from an glTF file. The sample is based on the glTF model loading sample, 
* and adds data structures, functions and shaders required to render a more complex scene using Crytek's Sponza model.
*
* This sample comes with a tutorial, see the README.md in this folder
*/

#include "gpuCluster.h"

/*
	Vulkan glTF scene class
*/

VulkanglTFScene::~VulkanglTFScene()
{
	// Release all Vulkan resources allocated for the model
	vkDestroyBuffer(vulkanDevice->logicalDevice, vertices.buffer, nullptr);
	vkFreeMemory(vulkanDevice->logicalDevice, vertices.memory, nullptr);
	vkDestroyBuffer(vulkanDevice->logicalDevice, indices.buffer, nullptr);
	vkFreeMemory(vulkanDevice->logicalDevice, indices.memory, nullptr);
	for (Image image : images) {
		vkDestroyImageView(vulkanDevice->logicalDevice, image.texture.view, nullptr);
		vkDestroyImage(vulkanDevice->logicalDevice, image.texture.image, nullptr);
		vkDestroySampler(vulkanDevice->logicalDevice, image.texture.sampler, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, image.texture.deviceMemory, nullptr);
	}
	for (Material material : materials) {
		vkDestroyPipeline(vulkanDevice->logicalDevice, material.pipeline, nullptr);
	}
}

/*
	glTF loading functions

	The following functions take a glTF input model loaded via tinyglTF and convert all required data into our own structure
*/

void VulkanglTFScene::loadImages(tinygltf::Model& input)
{
	// POI: The textures for the glTF file used in this sample are stored as external ktx files, so we can directly load them from disk without the need for conversion
	images.resize(input.images.size());
	for (size_t i = 0; i < input.images.size(); i++) {
		tinygltf::Image& glTFImage = input.images[i];
		images[i].texture.loadFromFile(path + "/" + glTFImage.uri, VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, copyQueue);
	}
}

void VulkanglTFScene::loadTextures(tinygltf::Model& input)
{
	textures.resize(input.textures.size());
	for (size_t i = 0; i < input.textures.size(); i++) {
		textures[i].imageIndex = input.textures[i].source;
	}
}

void VulkanglTFScene::loadMaterials(tinygltf::Model& input)
{
	materials.resize(input.materials.size());
	for (size_t i = 0; i < input.materials.size(); i++) {
		// We only read the most basic properties required for our sample
		tinygltf::Material glTFMaterial = input.materials[i];
		// Get the base color factor
		if (glTFMaterial.values.find("baseColorFactor") != glTFMaterial.values.end()) {
			materials[i].baseColorFactor = glm::make_vec4(glTFMaterial.values["baseColorFactor"].ColorFactor().data());
		}
		// Get base color texture index
		if (glTFMaterial.values.find("baseColorTexture") != glTFMaterial.values.end()) {
			materials[i].baseColorTextureIndex = glTFMaterial.values["baseColorTexture"].TextureIndex();
		}
		// Get the normal map texture index
		if (glTFMaterial.additionalValues.find("normalTexture") != glTFMaterial.additionalValues.end()) {
			materials[i].normalTextureIndex = glTFMaterial.additionalValues["normalTexture"].TextureIndex();
		}
		// Get some additional material parameters that are used in this sample
		materials[i].alphaMode = glTFMaterial.alphaMode;
		materials[i].alphaCutOff = (float)glTFMaterial.alphaCutoff;
		materials[i].doubleSided = glTFMaterial.doubleSided;
	}
}

void VulkanglTFScene::loadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, VulkanglTFScene::Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<VulkanglTFScene::Vertex>& vertexBuffer)
{
	VulkanglTFScene::Node node{};
	node.name = inputNode.name;
	
	// Get the local node matrix
	// It's either made up from translation, rotation, scale or a 4x4 matrix
	node.matrix = glm::mat4(1.0f);
	if (inputNode.translation.size() == 3) {
		node.matrix = glm::translate(node.matrix, glm::vec3(glm::make_vec3(inputNode.translation.data())));
	}
	if (inputNode.rotation.size() == 4) {
		glm::quat q = glm::make_quat(inputNode.rotation.data());
		node.matrix *= glm::mat4(q);
	}
	if (inputNode.scale.size() == 3) {
		node.matrix = glm::scale(node.matrix, glm::vec3(glm::make_vec3(inputNode.scale.data())));
	}
	if (inputNode.matrix.size() == 16) {
		node.matrix = glm::make_mat4x4(inputNode.matrix.data());
	};

	// Load node's children
	if (inputNode.children.size() > 0) {
		for (size_t i = 0; i < inputNode.children.size(); i++) {
			loadNode(input.nodes[inputNode.children[i]], input, &node, indexBuffer, vertexBuffer);
		}
	}

	// If the node contains mesh data, we load vertices and indices from the buffers
	// In glTF this is done via accessors and buffer views
	if (inputNode.mesh > -1) {
		const tinygltf::Mesh mesh = input.meshes[inputNode.mesh];
		// Iterate through all primitives of this node's mesh
		for (size_t i = 0; i < mesh.primitives.size(); i++) {
			const tinygltf::Primitive& glTFPrimitive = mesh.primitives[i];
			uint32_t firstIndex = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
			uint32_t indexCount = 0;
			// Vertices
			{
				const float* positionBuffer = nullptr;
				const float* normalsBuffer = nullptr;
				const float* texCoordsBuffer = nullptr;
				const float* tangentsBuffer = nullptr;
				size_t vertexCount = 0;

				// Get buffer data for vertex normals
				if (glTFPrimitive.attributes.find("POSITION") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("POSITION")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					positionBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
					vertexCount = accessor.count;
				}
				// Get buffer data for vertex normals
				if (glTFPrimitive.attributes.find("NORMAL") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					normalsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// Get buffer data for vertex texture coordinates
				// glTF supports multiple sets, we only load the first one
				if (glTFPrimitive.attributes.find("TEXCOORD_0") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					texCoordsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}
				// POI: This sample uses normal mapping, so we also need to load the tangents from the glTF file
				if (glTFPrimitive.attributes.find("TANGENT") != glTFPrimitive.attributes.end()) {
					const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView& view = input.bufferViews[accessor.bufferView];
					tangentsBuffer = reinterpret_cast<const float*>(&(input.buffers[view.buffer].data[accessor.byteOffset + view.byteOffset]));
				}

				// Append data to model's vertex buffer
				for (size_t v = 0; v < vertexCount; v++) {
					Vertex vert{};
					vert.pos = glm::vec4(glm::make_vec3(&positionBuffer[v * 3]), 1.0f);
					vert.normal = glm::normalize(glm::vec3(normalsBuffer ? glm::make_vec3(&normalsBuffer[v * 3]) : glm::vec3(0.0f)));
					vert.uv = texCoordsBuffer ? glm::make_vec2(&texCoordsBuffer[v * 2]) : glm::vec3(0.0f);
					vert.color = glm::vec3(1.0f);
					vert.tangent = tangentsBuffer ? glm::make_vec4(&tangentsBuffer[v * 4]) : glm::vec4(0.0f);
 					vertexBuffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor& accessor = input.accessors[glTFPrimitive.indices];
				const tinygltf::BufferView& bufferView = input.bufferViews[accessor.bufferView];
				const tinygltf::Buffer& buffer = input.buffers[bufferView.buffer];

				indexCount += static_cast<uint32_t>(accessor.count);

				// glTF supports different component types of indices
				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t* buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t* buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t* buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					break;
				}
				default:
					std::cerr << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					return;
				}
			}
			Primitive primitive{};
			primitive.firstIndex = firstIndex;
			primitive.indexCount = indexCount;
			primitive.materialIndex = glTFPrimitive.material;
			node.mesh.primitives.push_back(primitive);
		}
	}

	if (parent) {
		parent->children.push_back(node);
	}
	else {
		nodes.push_back(node);
	}
}

VkDescriptorImageInfo VulkanglTFScene::getTextureDescriptor(const size_t index)
{
	return images[index].texture.descriptor;
}

/*
	glTF rendering functions
*/

// Draw a single node including child nodes (if present)
void VulkanglTFScene::drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VulkanglTFScene::Node node)
{
	if (!node.visible) {
		return;
	}
	if (node.mesh.primitives.size() > 0) {
		// Pass the node's matrix via push constants
		// Traverse the node hierarchy to the top-most parent to get the final matrix of the current node
		glm::mat4 nodeMatrix = node.matrix;
		VulkanglTFScene::Node* currentParent = node.parent;
		while (currentParent) {
			nodeMatrix = currentParent->matrix * nodeMatrix;
			currentParent = currentParent->parent;
		}
		// Pass the final matrix to the vertex shader using push constants
		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &nodeMatrix);
		for (VulkanglTFScene::Primitive& primitive : node.mesh.primitives) {
			if (primitive.indexCount > 0) {
				VulkanglTFScene::Material& material = materials[primitive.materialIndex];
				// POI: Bind the pipeline for the node's material
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &material.descriptorSet, 0, nullptr);
				vkCmdDrawIndexed(commandBuffer, primitive.indexCount, 1, primitive.firstIndex, 0, 0);
			}
		}
	}
	for (auto& child : node.children) {
		drawNode(commandBuffer, pipelineLayout, child);
	}
}

// Draw the glTF scene starting at the top-level-nodes
void VulkanglTFScene::draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout)
{
	// All vertices and indices are stored in single buffers, so we only need to bind once
	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertices.buffer, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
	// Render all nodes at top-level
	for (auto& node : nodes) {
		drawNode(commandBuffer, pipelineLayout, node);
	}
}

/*
	Vulkan Example class
*/

VulkanExample::VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
{
	title = "glTF scene rendering";
	camera.type = Camera::CameraType::firstperson;
	camera.flipY = true;
	camera.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	camera.setRotation(glm::vec3(0.0f, -90.0f, 0.0f));
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	settings.overlay = true;
}

VulkanExample::~VulkanExample()
{
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.matrices, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.textures, nullptr);
	shaderData.buffer.destroy();
}

void VulkanExample::getEnabledFeatures()
{
	enabledFeatures.samplerAnisotropy = deviceFeatures.samplerAnisotropy;
}

void VulkanExample::buildClusterCommandBuffer()
{
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VK_CHECK_RESULT(vkBeginCommandBuffer(clusterCmdBuffer, &cmdBufInfo));

	// Add memory barrier to ensure that the indirect commands have been consumed before the compute shader updates them
	std::array<VkBufferMemoryBarrier, 1> barriersBefore{};
	barriersBefore[0] = vks::initializers::bufferMemoryBarrier();
	barriersBefore[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barriersBefore[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barriersBefore[0].srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
	barriersBefore[0].dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
	barriersBefore[0].buffer = clusterBuffer.buffer;
	barriersBefore[0].size = clusterBuffer.size;

	// since light is static, no barrier is needed for light buffer

	// the frustum calculation is doing on CPU side, we'll use fence to make sure it accessible before execute compute shader

	vkCmdPipelineBarrier(
		clusterCmdBuffer,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_FLAGS_NONE,
		0, nullptr,
		barriersBefore.size(), barriersBefore.data(),
		0, nullptr);

	vkCmdBindPipeline(clusterCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clusterPipeline);
	vkCmdBindDescriptorSets(clusterCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, clusterPipelineLayout, 0, 1, &clusterDS, 0, 0);

	vkCmdDispatch(clusterCmdBuffer, CLUSTER_SIZE / 16, 1, 1);

	// Add memory barrier to ensure that the compute shader has finished writing the indirect command buffer before it's consumed
	std::array<VkBufferMemoryBarrier, 1> barriersAfter{};
	barriersAfter[0] = vks::initializers::bufferMemoryBarrier();
	barriersAfter[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barriersAfter[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	barriersAfter[0].srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
	barriersAfter[0].dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphics;
	barriersAfter[0].buffer = clusterBuffer.buffer;
	barriersAfter[0].size = clusterBuffer.size;

	vkCmdPipelineBarrier(
		clusterCmdBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_FLAGS_NONE,
		0, nullptr,
		barriersAfter.size(), barriersAfter.data(),
		0, nullptr);

	vkEndCommandBuffer(clusterCmdBuffer);
}

void VulkanExample::buildCommandBuffers()
{
	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkClearValue clearValues[2];
	clearValues[0].color = defaultClearColor;
	clearValues[0].color = { { 0.25f, 0.25f, 0.25f, 1.0f } };;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
	const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

	for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	{
		renderPassBeginInfo.framebuffer = frameBuffers[i];
		VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
		vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
		vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
		// Bind scene matrices descriptor to set 0
		vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		// POI: Draw the glTF scene
		glTFScene.draw(drawCmdBuffers[i], pipelineLayout);

		drawUI(drawCmdBuffers[i]);
		vkCmdEndRenderPass(drawCmdBuffers[i]);
		VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
	}
}

void VulkanExample::loadglTFFile(std::string filename)
{
	tinygltf::Model glTFInput;
	tinygltf::TinyGLTF gltfContext;
	std::string error, warning;

	this->device = device;

#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&glTFInput, &error, &warning, filename);

	// Pass some Vulkan resources required for setup and rendering to the glTF model loading class
	glTFScene.vulkanDevice = vulkanDevice;
	glTFScene.copyQueue    = queue;

	size_t pos = filename.find_last_of('/');
	glTFScene.path = filename.substr(0, pos);

	std::vector<uint32_t> indexBuffer;
	std::vector<VulkanglTFScene::Vertex> vertexBuffer;

	if (fileLoaded) {
		glTFScene.loadImages(glTFInput);
		glTFScene.loadMaterials(glTFInput);
		glTFScene.loadTextures(glTFInput);
		const tinygltf::Scene& scene = glTFInput.scenes[0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = glTFInput.nodes[scene.nodes[i]];
			glTFScene.loadNode(node, glTFInput, nullptr, indexBuffer, vertexBuffer);
		}
	}
	else {
		vks::tools::exitFatal("Could not open the glTF file.\n\nThe file is part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		return;
	}

	// Create and upload vertex and index buffer
	// We will be using one single vertex buffer and one single index buffer for the whole glTF scene
	// Primitives (of the glTF model) will then index into these using index offsets

	size_t vertexBufferSize = vertexBuffer.size() * sizeof(VulkanglTFScene::Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
	glTFScene.indices.count = static_cast<uint32_t>(indexBuffer.size());

	struct StagingBuffer {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertexStaging, indexStaging;

	// Create host visible staging buffers (source)
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertexBufferSize,
		&vertexStaging.buffer,
		&vertexStaging.memory,
		vertexBuffer.data()));
	// Index data
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indexBufferSize,
		&indexStaging.buffer,
		&indexStaging.memory,
		indexBuffer.data()));

	// Create device local buffers (target)
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		vertexBufferSize,
		&glTFScene.vertices.buffer,
		&glTFScene.vertices.memory));
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		indexBufferSize,
		&glTFScene.indices.buffer,
		&glTFScene.indices.memory));

	// Copy data from staging buffers (host) do device local buffer (gpu)
	VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	VkBufferCopy copyRegion = {};

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(
		copyCmd,
		vertexStaging.buffer,
		glTFScene.vertices.buffer,
		1,
		&copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(
		copyCmd,
		indexStaging.buffer,
		glTFScene.indices.buffer,
		1,
		&copyRegion);

	vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

	// Free staging resources
	vkDestroyBuffer(device, vertexStaging.buffer, nullptr);
	vkFreeMemory(device, vertexStaging.memory, nullptr);
	vkDestroyBuffer(device, indexStaging.buffer, nullptr);
	vkFreeMemory(device, indexStaging.memory, nullptr);
}

void VulkanExample::loadAssets()
{
	loadglTFFile(getAssetPath() + "models/sponza/sponza.gltf");
}

void VulkanExample::setupDescriptors()
{
	/*
		This sample uses separate descriptor sets (and layouts) for the matrices and materials (textures)
	*/

	// One ubo to pass dynamic data to the shader
	// Two combined image samplers per material as each material uses color and normal maps
	std::vector<VkDescriptorPoolSize> poolSizes = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(glTFScene.materials.size()) * 2),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3)
	};
	// One set for matrices and one per model image/texture
	const uint32_t maxSetCount = static_cast<uint32_t>(glTFScene.images.size()) + 2;
	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxSetCount);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
	vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
	vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
	vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
	};
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &clusterDSLayout));

	// Descriptor set layout for passing matrices
	setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
	};
	descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.matrices));

	// Descriptor set layout for passing material textures
	setLayoutBindings = {		
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0), // Color map	
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1), // Normal map
	};
	descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
	descriptorSetLayoutCI.bindingCount = 2;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.textures));

	// Pipeline layout using both descriptor sets (set 0 = matrices, set 1 = material)
	std::array<VkDescriptorSetLayout, 2> setLayouts = { descriptorSetLayouts.matrices, descriptorSetLayouts.textures };
	VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), static_cast<uint32_t>(setLayouts.size()));
	// We will use push constants to push the local matrices of a primitive to the vertex shader
	VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4), 0);
	// Push constant ranges are part of the pipeline layout
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayout));

	pipelineLayoutCI = vks::initializers::pipelineLayoutCreateInfo(&clusterDSLayout, 1);
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &clusterPipelineLayout));

	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &clusterDSLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &clusterDS));
	std::array<VkWriteDescriptorSet, 3> writeClusterSets = {
		vks::initializers::writeDescriptorSet(clusterDS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &clusterBuffer.descriptor),
		vks::initializers::writeDescriptorSet(clusterDS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &lightBuffer.descriptor),
		vks::initializers::writeDescriptorSet(clusterDS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &frustumBuffer.descriptor),
	};
	vkUpdateDescriptorSets(device, writeClusterSets.size(), writeClusterSets.data(), 0, nullptr);

	// Descriptor set for scene matrices
	allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.matrices, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
	std::array<VkWriteDescriptorSet, 4> writeSets = {
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shaderData.buffer.descriptor),
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &clusterBuffer.descriptor),
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &lightBuffer.descriptor),
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &clusterCameraBuffer.descriptor),
	};
	vkUpdateDescriptorSets(device, writeSets.size(), writeSets.data(), 0, nullptr);

	// Descriptor sets for materials
	for (auto& material : glTFScene.materials) {
		const VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.textures, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &material.descriptorSet));
		VkDescriptorImageInfo colorMap = glTFScene.getTextureDescriptor(material.baseColorTextureIndex);
		VkDescriptorImageInfo normalMap = glTFScene.getTextureDescriptor(material.normalTextureIndex);
		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(material.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &colorMap),
			vks::initializers::writeDescriptorSet(material.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &normalMap),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}

void VulkanExample::preparePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
	const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
		vks::initializers::vertexInputBindingDescription(0, sizeof(VulkanglTFScene::Vertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFScene::Vertex, pos)),
		vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFScene::Vertex, normal)),
		vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFScene::Vertex, uv)),
		vks::initializers::vertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFScene::Vertex, color)),
		vks::initializers::vertexInputAttributeDescription(0, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFScene::Vertex, tangent)),
	};
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::pipelineVertexInputStateCreateInfo(vertexInputBindings, vertexInputAttributes);

	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();

	shaderStages[0] = loadShader(getShadersPath() + "gpuCluster/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = loadShader(getShadersPath() + "gpuCluster/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	// POI: Instead if using a few fixed pipelines, we create one pipeline for each material using the properties of that material
	for (auto &material : glTFScene.materials) {

		struct MaterialSpecializationData {
			bool alphaMask;
			float alphaMaskCutoff;
			float zNear;
			float zFar;
			float width;
			float height;
			uint32_t clusterX;
			uint32_t clusterY;
			uint32_t clusterZ;
		} materialSpecializationData;

		materialSpecializationData.alphaMask = material.alphaMode == "MASK";
		materialSpecializationData.alphaMaskCutoff = material.alphaCutOff;
		materialSpecializationData.zNear = camera.getNearClip();
		materialSpecializationData.zFar = camera.getFarClip();
		materialSpecializationData.width = width;
		materialSpecializationData.height = height;
		materialSpecializationData.clusterX = CLUSTER_X;
		materialSpecializationData.clusterY = CLUSTER_Y;
		materialSpecializationData.clusterZ = CLUSTER_Z;

		// POI: Constant fragment shader material parameters will be set using specialization constants
		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, offsetof(MaterialSpecializationData, alphaMask), sizeof(MaterialSpecializationData::alphaMask)),
			vks::initializers::specializationMapEntry(1, offsetof(MaterialSpecializationData, alphaMaskCutoff), sizeof(MaterialSpecializationData::alphaMaskCutoff)),
			vks::initializers::specializationMapEntry(2, offsetof(MaterialSpecializationData, zNear), sizeof(MaterialSpecializationData::zNear)),
			vks::initializers::specializationMapEntry(3, offsetof(MaterialSpecializationData, zFar), sizeof(MaterialSpecializationData::zFar)),
			vks::initializers::specializationMapEntry(4, offsetof(MaterialSpecializationData, width), sizeof(MaterialSpecializationData::width)),
			vks::initializers::specializationMapEntry(5, offsetof(MaterialSpecializationData, height), sizeof(MaterialSpecializationData::height)),
			vks::initializers::specializationMapEntry(6, offsetof(MaterialSpecializationData, clusterX), sizeof(MaterialSpecializationData::clusterX)),
			vks::initializers::specializationMapEntry(7, offsetof(MaterialSpecializationData, clusterY), sizeof(MaterialSpecializationData::clusterY)),
			vks::initializers::specializationMapEntry(8, offsetof(MaterialSpecializationData, clusterZ), sizeof(MaterialSpecializationData::clusterZ)),

		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(specializationMapEntries, sizeof(materialSpecializationData), &materialSpecializationData);
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		// For double sided materials, culling will be disabled
		rasterizationStateCI.cullMode = material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &material.pipeline));
	}

	//================================================================
	// Compute Pipeline
	VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(clusterPipelineLayout);
	computePipelineCreateInfo.stage = loadShader(getShadersPath() + "gpuCluster/cluster.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

	struct ClusterSpecializationData {
		uint32_t maxListSize;
		uint32_t globalLightCount;
	} clusterSpecializationData;

	clusterSpecializationData.maxListSize = MAX_LIST_SIZE;
	clusterSpecializationData.globalLightCount = globalLightCount;

	std::vector<VkSpecializationMapEntry> specializationMapEntries = {
		vks::initializers::specializationMapEntry(0, offsetof(ClusterSpecializationData, maxListSize), sizeof(ClusterSpecializationData::maxListSize)),
		vks::initializers::specializationMapEntry(1, offsetof(ClusterSpecializationData, globalLightCount), sizeof(ClusterSpecializationData::globalLightCount)),
	};
	VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(specializationMapEntries, sizeof(clusterSpecializationData), &clusterSpecializationData);

	computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

	VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &clusterPipeline));
}

void VulkanExample::prepareCluster()
{
	// Get a compute capable device queue
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &computeQueue);

	// Separate command pool as queue family for compute may be different than graphics
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &computeCmdPool));

	// Create a command buffer for compute operations
	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
		vks::initializers::commandBufferAllocateInfo(
			computeCmdPool,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1);

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &clusterCmdBuffer));

	// Fence for compute CB sync
	VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &clusterFence));

	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &clusterSemaphore));
}

void VulkanExample::prepareUniformBuffers()
{
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&shaderData.buffer,
		sizeof(shaderData.values)));
	VK_CHECK_RESULT(shaderData.buffer.map());
	updateUniformBuffers();
}

void VulkanExample::updateUniformBuffers()
{
	shaderData.values.projection = camera.matrices.perspective;
	shaderData.values.view = camera.matrices.view;
	shaderData.values.viewPos = camera.viewPos;
	memcpy(shaderData.buffer.mapped, &shaderData.values, sizeof(shaderData.values));
}

void VulkanExample::prepareClusterBuffers()
{
	vks::Buffer stagingBuffer;

	// Cluster Buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		CLUSTER_SIZE * sizeof(Cluster),
		clusters));

	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&clusterBuffer,
		stagingBuffer.size));

	// Light Buffer
	globalLights.lights[0].sphere = glm::vec4(-4.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[0].color = glm::vec3(1.0f, 0.0f, 0.0f);
	globalLights.lights[1].sphere = glm::vec4(0.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[1].color = glm::vec3(0.0f, 1.0f, 0.0f);
	globalLights.lights[2].sphere = glm::vec4(4.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[2].color = glm::vec3(0.0f, 0.0f, 1.0f);
	globalLights.lights[3].sphere = glm::vec4(-8.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
	globalLights.lights[4].sphere = glm::vec4(8.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[4].color = glm::vec3(0.0f, 1.0f, 1.0f);

	globalLights.lights[5].sphere = glm::vec4(8.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[5].color = glm::vec3(1.0f, 1.0f, 1.0f);
	globalLights.lights[6].sphere = glm::vec4(8.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[6].color = glm::vec3(1.0f, 1.0f, 1.0f);
	globalLights.lights[7].sphere = glm::vec4(8.0f, 2.0f, 0.0f, 4.0f);
	globalLights.lights[7].color = glm::vec3(1.0f, 1.0f, 1.0f);

	globalLightCount = 8;

	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		sizeof(GlobalLights),
		&globalLights));

	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&lightBuffer,
		stagingBuffer.size));

	vulkanDevice->copyBuffer(&stagingBuffer, &lightBuffer, queue);

	// Frustum Buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&frustumBuffer,
		frustums.size() * sizeof(Frustum)));

	frustumBuffer.map();
	updateClusterFrustum();

	// Cluster Camera Buffer
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&clusterCameraBuffer,
		sizeof(ClusterCamera)));

	clusterCameraBuffer.map();
	updateClusterCamera(showCluster, isClusterFreeze);

	stagingBuffer.destroy();
}

void VulkanExample::prepare()
{
	VulkanExampleBase::prepare();
	loadAssets();
	
	prepareUniformBuffers();
	prepareClusterBuffers();

	setupDescriptors();
	preparePipelines();
	prepareCluster();

	buildCommandBuffers();
	buildClusterCommandBuffer();
	prepared = true;
}

void VulkanExample::render()
{
	if (!prepared) return;

	VulkanExampleBase::prepareFrame();

	// update cluster list in gpu
	// Wait for fence to ensure that compute buffer writes have finished
	vkWaitForFences(device, 1, &clusterFence, VK_TRUE, UINT64_MAX);
	vkResetFences(device, 1, &clusterFence);

	VkSubmitInfo computeSubmitInfo = vks::initializers::submitInfo();
	computeSubmitInfo.commandBufferCount = 1;
	computeSubmitInfo.pCommandBuffers = &clusterCmdBuffer;
	computeSubmitInfo.signalSemaphoreCount = 1;
	computeSubmitInfo.pSignalSemaphores = &clusterSemaphore;

	VK_CHECK_RESULT(vkQueueSubmit(computeQueue, 1, &computeSubmitInfo, VK_NULL_HANDLE));

	// Submit graphics command buffer

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

	// Wait on present and compute semaphores
	std::array<VkPipelineStageFlags, 2> stageFlags = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	};
	std::array<VkSemaphore, 2> waitSemaphores = {
		semaphores.presentComplete,						// Wait for presentation to finished
		clusterSemaphore								// Wait for compute to finish
	};

	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitDstStageMask = stageFlags.data();

	// Submit to queue
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, clusterFence));
	VulkanExampleBase::submitFrame();


	if (camera.updated) {
		updateUniformBuffers();
		if(!isClusterFreeze)
			updateClusterFrustum();
	}
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay* overlay)
{
	if (overlay->header("Setting")) {
		if (overlay->checkBox("Show Cluster", &showCluster)) {
			updateClusterCamera(showCluster, isClusterFreeze);
		}
		if (overlay->checkBox("Cluster Freeze", &isClusterFreeze)) {
			updateClusterCamera(showCluster, isClusterFreeze);
		}
	}
}

VULKAN_EXAMPLE_MAIN()

void VulkanExample::updateClusterFrustum()
{
	glm::mat4 invVP = glm::inverse(camera.matrices.perspective * camera.matrices.view);
	for (int i = 0; i < CLUSTER_SIZE; ++i) {
		int clusterZ = i / (CLUSTER_X * CLUSTER_Y);
		int clusterY = i % (CLUSTER_X * CLUSTER_Y) / CLUSTER_X;
		int clusterX = i % (CLUSTER_X * CLUSTER_Y) % CLUSTER_X;

		glm::vec2 uvBottomLeft = glm::vec2((float)clusterX / CLUSTER_X, (float)clusterY / CLUSTER_Y) * 2.0f - 1.0f;
		glm::vec2 uvTopRight = glm::vec2((float)(clusterX + 1) / CLUSTER_X, (float)(clusterY + 1) / CLUSTER_Y) * 2.0f - 1.0f;

		// Top
		frustums[i].planes[0] = 
			calPlane(
				mul(invVP, glm::vec4(-1, uvTopRight.y, 0, 1)),
				mul(invVP, glm::vec4(-1, uvTopRight.y, 1, 1)),
				mul(invVP, glm::vec4(1, uvTopRight.y, 0, 1))
			);
		// Bottom
		frustums[i].planes[1] =
			calPlane(
				mul(invVP, glm::vec4(-1, uvBottomLeft.y, 0, 1)),
				mul(invVP, glm::vec4(1, uvBottomLeft.y, 0, 1)),
				mul(invVP, glm::vec4(-1, uvBottomLeft.y, 1, 1))
			);
		// Left
		frustums[i].planes[2] =
			calPlane(
				mul(invVP, glm::vec4(uvBottomLeft.x, -1, 0, 1)),
				mul(invVP, glm::vec4(uvBottomLeft.x, -1, 1, 1)),
				mul(invVP, glm::vec4(uvBottomLeft.x, 1, 0, 1))
			);
		// Right
		frustums[i].planes[3] =
			calPlane(
				mul(invVP, glm::vec4(uvTopRight.x, -1, 0, 1)),
				mul(invVP, glm::vec4(uvTopRight.x, 1, 0, 1)),
				mul(invVP, glm::vec4(uvTopRight.x, -1, 1, 1))
			);
		
		// Front
		frustums[i].planes[4] =
			calPlane(
				mul(invVP, glm::vec4(-1, -1, 0, 1)),
				mul(invVP, glm::vec4(-1, 1, 0, 1)),
				mul(invVP, glm::vec4(1, -1, 0, 1))
			);
		// Back
		frustums[i].planes[5] =
			calPlane(
				mul(invVP, glm::vec4(-1, -1, 1, 1)),
				mul(invVP, glm::vec4(1, -1, 1, 1)),
				mul(invVP, glm::vec4(-1, 1, 1, 1))
			);
	}
	frustumBuffer.copyTo(frustums.data(), frustums.size() * sizeof(Frustum));
}

void VulkanExample::updateClusterCamera(bool showCluster, bool isFreeze)
{
	if (showCluster) {
		clusterCamera.showCluster = 1;
		if (isFreeze) {
			clusterCamera.isFreeze = 1;
			clusterCamera.view = camera.matrices.view;
			clusterCamera.proj = camera.matrices.perspective;
		}
		else {
			clusterCamera.isFreeze = 0;
		}
	}
	else {
		clusterCamera.showCluster = 0;
		clusterCamera.isFreeze = 0;
	}

	clusterCameraBuffer.copyTo(&clusterCamera, sizeof(ClusterCamera));
}
