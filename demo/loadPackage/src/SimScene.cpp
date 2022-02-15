#include "SimScene.h"

SimScene::SimScene()
{

}

void SimScene::init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, const std::string& filename)
{
	m_vkDevice = vkDevice;
	m_example = example;

	loadFromFile(filename);

	prepareDescriptorSetLayout();
	prepareDescriptor();
}

void SimScene::loadFromFile(const std::string& filename)
{
	std::ifstream is(filename, std::ios::binary);
	cereal::BinaryInputArchive archive(is);
	SDataOperation inputData;
	archive(inputData);

	textures.reserve(inputData.m_images.size());
	for (const SImage& img : inputData.m_images) {
		vks::Texture2D tex;
		VkFormat format;
		if (img.pixelFormat == 0x83F0/* GL_COMPRESSED_RGB_S3TC_DXT1_EXT */ && img.type == 0x1401/* GL_UNSIGNED_BYTE */) {
			format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
			tex.fromBuffer((void*)img.imageData.data(), img.imageData.size(), format, img.s, img.t, m_vkDevice, m_example->queue);
		}
		else if (img.pixelFormat == 0x83F3/* GL_COMPRESSED_RGBA_S3TC_DXT5_EXT */ && img.type == 0x1401/* GL_UNSIGNED_BYTE */) {
			format = VK_FORMAT_BC3_UNORM_BLOCK;
			tex.fromBuffer((void*)img.imageData.data(), img.imageData.size(), format, img.s, img.t, m_vkDevice, m_example->queue);
		}
		else if (img.pixelFormat == 0x1907/* GL_RGB */ && img.type == 0x1401/* GL_UNSIGNED_BYTE */) {
			format = VK_FORMAT_R8G8B8A8_UNORM;

			std::vector<uint8_t> temp(img.s * img.t * 4);
			for (int i = 0; i < img.t; ++i) {
				memcpy(temp.data() + i * img.s * 4, img.imageData.data() + i * img.s * 3, img.s * 3);
				memset(temp.data() + i * img.s * 4 + img.s * 3, 255u, img.s);
			}
			tex.fromBuffer((void*)temp.data(), temp.size(), format, img.s, img.t, m_vkDevice, m_example->queue);

			/*		
			VkImageFormatProperties prop;
			vkGetPhysicalDeviceImageFormatProperties(m_vkDevice->physicalDevice, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, &prop);
			*/
		}
		else if (img.pixelFormat == 0x1908/* GL_RGBA */ && img.type == 0x1401/* GL_UNSIGNED_BYTE */) {
			format = VK_FORMAT_R8G8B8A8_UNORM;
			tex.fromBuffer((void*)img.imageData.data(), img.imageData.size(), format, img.s, img.t, m_vkDevice, m_example->queue);
		}
		else {
			format = VK_FORMAT_R8G8B8A8_UNORM;
		}
		
		textures.push_back(tex);
	}

	geometries.reserve(inputData.geo.size());
	for (SGeodata& g : inputData.geo)
	{
		if (g.vt.empty()) continue;

		Geometry geometry;

		std::vector<Geometry::Vertex> vertices;
		vertices.reserve(g.vt.size());
		for (uint32_t i = 0; i < g.vt.size(); ++i) {
			Geometry::Vertex vertex;
			vertex.pos = glm::vec3(g.vt[i].x, g.vt[i].y, g.vt[i].z);
			vertex.normal = glm::vec3(g.nm[i].x, g.nm[i].y, g.nm[i].z);
			vertex.uv0 = glm::vec3(g.tx[0][i].x, g.tx[0][i].y, g.tx[0][i].z);
			vertex.uv1 = glm::vec3(g.tx[1][i].x, g.tx[1][i].y, g.tx[1][i].z);
			vertex.uv2 = glm::vec3(g.tx[2][i].x, g.tx[2][i].y, g.tx[2][i].z);
			vertex.uv3 = glm::vec3(g.tx[3][i].x, g.tx[3][i].y, g.tx[3][i].z);
			vertices.push_back(vertex);
		}

		geometry.indexCount = g.indices.size();

		VK_CHECK_RESULT(m_vkDevice->createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&geometry.vertexBuffer,
			vertices.size() * sizeof(Geometry::Vertex),
			vertices.data()));
		VK_CHECK_RESULT(m_vkDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&geometry.indexBuffer,
			g.indices.size() * sizeof(uint32_t),
			g.indices.data()));

		geometries.push_back(geometry);
	}
}

void SimScene::draw(VkCommandBuffer cb, VkPipelineLayout pLayout, uint32_t instanceCount)
{
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pLayout, 1, 1, &m_descriptorSet, 0, NULL);

	for (auto& geometry : geometries) {
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cb, 0, 1, &geometry.vertexBuffer.buffer, offsets);
		vkCmdBindIndexBuffer(cb, geometry.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cb, geometry.indexCount, instanceCount, 0, 0, 0);
	}
}

void SimScene::destroy()
{
	for (auto& texture : textures) {
		texture.destroy();
	}
	for (auto& geometry : geometries) {
		geometry.vertexBuffer.destroy();
		geometry.indexBuffer.destroy();
	}
	vkDestroyDescriptorPool(m_vkDevice->logicalDevice, m_descriptorPool, nullptr);
	vkDestroyDescriptorSetLayout(m_vkDevice->logicalDevice, m_dsLayout, nullptr);
}

void SimScene::prepareDescriptorSetLayout()
{
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, textures.size())
	};
	VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(m_vkDevice->logicalDevice, &descriptorLayout, nullptr, &m_dsLayout));
}

void SimScene::prepareDescriptor()
{
	// Descriptor Pool
	//
	std::vector<VkDescriptorPoolSize> typeCounts = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textures.size())
	};

	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(typeCounts, 1);
	VK_CHECK_RESULT(vkCreateDescriptorPool(m_vkDevice->logicalDevice, &descriptorPoolInfo, nullptr, &m_descriptorPool));

	// Descriptor Set
	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(m_descriptorPool, &m_dsLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(m_vkDevice->logicalDevice, &allocInfo, &m_descriptorSet));

	std::vector<VkDescriptorImageInfo> texDescriptors;
	texDescriptors.reserve(textures.size());
	for (auto& tex : textures) {
		texDescriptors.push_back(tex.descriptor);
	}

	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(m_descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, texDescriptors.data(), texDescriptors.size())
	};
	vkUpdateDescriptorSets(m_vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}
