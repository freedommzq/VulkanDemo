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

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"

#include "vulkanexamplebase.h"

#define ENABLE_VALIDATION false

 // Contains everything required to render a basic glTF scene in Vulkan
 // This class is heavily simplified (compared to glTF's feature set) but retains the basic glTF structure
class VulkanglTFScene
{
public:
	// The class requires some Vulkan objects so it can create it's own resources
	vks::VulkanDevice* vulkanDevice;
	VkQueue copyQueue;

	// The vertex layout for the samples' model
	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec3 color;
		glm::vec4 tangent;
	};

	// Single vertex buffer for all primitives
	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertices;

	// Single index buffer for all primitives
	struct {
		int count;
		VkBuffer buffer;
		VkDeviceMemory memory;
	} indices;

	// The following structures roughly represent the glTF scene structure
	// To keep things simple, they only contain those properties that are required for this sample
	struct Node;

	// A primitive contains the data for a single draw call
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		int32_t materialIndex;
	};

	// Contains the node's (optional) geometry and can be made up of an arbitrary number of primitives
	struct Mesh {
		std::vector<Primitive> primitives;
	};

	// A node represents an object in the glTF scene graph
	struct Node {
		Node* parent;
		std::vector<Node> children;
		Mesh mesh;
		glm::mat4 matrix;
		std::string name;
		bool visible = true;
	};

	// A glTF material stores information in e.g. the texture that is attached to it and colors
	struct Material {
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		uint32_t baseColorTextureIndex;
		uint32_t normalTextureIndex;
		std::string alphaMode = "OPAQUE";
		float alphaCutOff;
		bool doubleSided = false;
		VkDescriptorSet descriptorSet;
		VkPipeline pipeline;
	};

	// Contains the texture for a single glTF image
	// Images may be reused by texture objects and are as such separated
	struct Image {
		vks::Texture2D texture;
	};

	// A glTF texture stores a reference to the image and a sampler
	// In this sample, we are only interested in the image
	struct Texture {
		int32_t imageIndex;
	};

	/*
		Model data
	*/
	std::vector<Image> images;
	std::vector<Texture> textures;
	std::vector<Material> materials;
	std::vector<Node> nodes;

	std::string path;

	~VulkanglTFScene();
	VkDescriptorImageInfo getTextureDescriptor(const size_t index);
	void loadImages(tinygltf::Model& input);
	void loadTextures(tinygltf::Model& input);
	void loadMaterials(tinygltf::Model& input);
	void loadNode(const tinygltf::Node& inputNode, const tinygltf::Model& input, VulkanglTFScene::Node* parent, std::vector<uint32_t>& indexBuffer, std::vector<VulkanglTFScene::Vertex>& vertexBuffer);
	void drawNode(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VulkanglTFScene::Node node);
	void draw(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);
};

class VulkanExample : public VulkanExampleBase
{
public:
	VulkanglTFScene glTFScene;

	struct ShaderData {
		vks::Buffer buffer;
		struct Values {
			glm::mat4 projection;
			glm::mat4 view;
			glm::vec4 viewPos;
		} values;
	} shaderData;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;

	struct DescriptorSetLayouts {
		VkDescriptorSetLayout matrices;
		VkDescriptorSetLayout textures;
	} descriptorSetLayouts;

	VulkanExample();
	~VulkanExample();
	virtual void getEnabledFeatures();
	void buildClusterCommandBuffer();
	void buildCommandBuffers();
	void loadglTFFile(std::string filename);
	void loadAssets();
	void setupDescriptors();
	void preparePipelines();
	void prepareCluster();
	void prepareUniformBuffers();
	void updateUniformBuffers();
	void prepareBuffer();
	void prepare();
	virtual void render();
	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay);

// todo: dynamic light & add/delete light
#define CLUSTER_X 4
#define CLUSTER_Y 4
#define CLUSTER_Z 4
#define CLUSTER_SIZE CLUSTER_X * CLUSTER_Y * CLUSTER_Z
#define MAX_LIST_SIZE 20
#define MAX_LIGHT_SIZE 1000

	struct Cluster {
		uint32_t size;
		uint32_t lights[MAX_LIST_SIZE];
	};
	Cluster clusters[CLUSTER_SIZE];

	// todo: should add support for spot light
	struct Light {
		glm::vec4 sphere; // pos + radius
		alignas(16) glm::vec3 color;
	};
	struct GlobalLights {
		std::array<Light, MAX_LIGHT_SIZE> lights;
	};
	uint32_t globalLightCount = 0;
	GlobalLights globalLights;

	struct Frustum {
		glm::vec4 planes[6];
	};
	std::array<Frustum, CLUSTER_SIZE> frustums;

	vks::Buffer clusterBuffer;
	vks::Buffer lightBuffer;
	vks::Buffer frustumBuffer;

	VkCommandBuffer clusterCmdBuffer;
	VkFence clusterFence;
	VkSemaphore clusterSemaphore;

	VkDescriptorSetLayout clusterDSLayout;
	VkDescriptorSet clusterDS;
	VkPipelineLayout clusterPipelineLayout;
	VkPipeline clusterPipeline;

	VkQueue computeQueue;
	VkCommandPool computeCmdPool;

private:
	void updateClusterFrustum();
	inline glm::vec4 calPlane(glm::vec3 v1, glm::vec3 v2, glm::vec3 v3) {
		glm::vec3 normal = normalize(cross(v3 - v1, v2 - v1));
		return glm::vec4(normal, -dot(normal, v1));
	}
	inline glm::vec3 mul(glm::mat4 m, glm::vec4 v) {
		glm::vec4 ret = m * v;
		ret /= ret.w;
		return ret;
	}
};