/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield
 *
 * This library is open source and may be redistributed and/or modified under
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * OpenSceneGraph Public License for more details.
*/

#include "vulkanexamplebase.h"

#include "osg/PolyTope.h"

#include <map>

class ParticleEffect {
	static constexpr uint32_t VERTEX_BUFFER_ID = 0;
	static constexpr uint32_t INSTANCE_BUFFER_ID = 1;

	static constexpr uint32_t SIMU_FRAME_RATE = 30;
	static constexpr float SIMU_DELTA_TIME = 1.0f / (float)SIMU_FRAME_RATE;

	static constexpr uint32_t MAX_PARTICLE_COUNT_PER_CELL = 1024;

	static uint32_t statisticCullFrustum;
	static uint32_t statisticCullFar;

	const glm::mat4 matToOSG = glm::mat4(
		1.0, 0.0, 0.0, 0.0,
		0.0, 0.0, -1.0, 0.0,
		0.0, -1.0, 0.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	);
public:
	ParticleEffect();

	void init(vks::VulkanDevice* vkDevice, VulkanExampleBase* example, VkRenderPass renderPass);
	void update();
	void draw(VkCommandBuffer cb);
	void destroy();

	void rain(float intensity);
	void snow(float intensity);
private:
	void cull();
	bool build(glm::vec3& eyeLocal, int i, int j, int k, float startTime, osg::Polytope& frustum);

	void createGeometry(uint32_t numParticles);
	void prepareUniforms();
	void updateUniformBuffer();
	void createSpotLightImage(glm::vec4& centerColor, glm::vec4& backgroundColor, uint32_t size, float power);
	void fillSpotLightImage(unsigned char* ptr, glm::vec4& centerColor, glm::vec4& backgroundColor, uint32_t size, float power);

	void prepareDescriptorSetLayout();
	void preparePipelineLayout();
	void prepareDescriptorSet();
private:
	vks::VulkanDevice* m_vkDevice;
	VulkanExampleBase* m_example;

	struct VertexData {
		glm::vec2 uv;
	};
	struct InstanceData {
		glm::vec3 pos;
	};
	vks::Buffer m_vertexBuffer;
	vks::Buffer m_instanceBuffer;
	vks::Buffer m_indexBuffer;

	struct {
		glm::mat4 projection;

		glm::vec4 particleColor;
		float particleSize;
		float inversePeriod;

		float time;
	}uniforms;
	vks::Buffer m_uniformBuffer;
	vks::Texture2D m_texRaindrop;

	VkRenderPass m_renderPass;

	VkDescriptorSetLayout m_dsLayout;
	VkPipelineLayout m_pLayout;

	VkDescriptorPool m_descriptorPool;
	VkDescriptorSet m_descriptorSet;


	class ParticalDrawable {
	public:
		void init(VulkanExampleBase* example, VkDevice device, VkRenderPass renderPass, VkPipelineLayout pLayout, VkPrimitiveTopology topology, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t firstInstance);
		void draw(VkCommandBuffer cb, VkPipelineLayout pLayout);
		void destroy(VkDevice device);

		inline void setDrawInstanceCountPerCell(uint32_t count) {
			m_instanceCount = std::min(MAX_PARTICLE_COUNT_PER_CELL, count);
		}

		inline void newFrame() {
			//m_preCellMatrixMap.swap(m_curCellMatrixMap);
			m_curCellMatrixMap.clear();
		}

		struct Cell {
			Cell(int in_i, int in_j, int in_k) :
				i(in_i), j(in_j), k(in_k) {}

			inline bool operator < (const Cell& rhs) const {
				if (i < rhs.i) return true;
				if (i > rhs.i) return false;
				if (j < rhs.j) return true;
				if (j > rhs.j) return false;
				if (k < rhs.k) return true;
				if (k > rhs.k) return false;
				return false;
			}

			int i;
			int j;
			int k;
		};

		struct DepthMatrixStartTime {
			inline bool operator < (const DepthMatrixStartTime& rhs) const {
				return depth < rhs.depth;
			}

			float           depth;
			float           startTime;
			glm::mat4       modelview;
		};

		struct PushConstCell {
			glm::mat4 modelView;
			float startTime;
		};

		typedef std::map<Cell, DepthMatrixStartTime> CellMatrixMap;

		struct LessFunctor {
			inline bool operator () (const CellMatrixMap::value_type* lhs, const CellMatrixMap::value_type* rhs) const {
				return (*lhs).second < (*rhs).second;
			}
		};

		CellMatrixMap m_curCellMatrixMap;
		//CellMatrixMap m_preCellMatrixMap;

		VkPipeline m_pipeline = VK_NULL_HANDLE;

		uint32_t m_indexCount;
		uint32_t m_instanceCount; // Dynamic (Particle Density * Cell Size)
		uint32_t m_firstIndex;
		uint32_t m_firstInstance;
	};

	ParticalDrawable m_quadDrawable;
	ParticalDrawable m_lineDrawable;
	ParticalDrawable m_pointDrawable;

	// Parameters
	glm::vec3 m_wind;
	float m_particleSpeed;
	float m_particleSize;
	glm::vec4 m_particleColor;
	float m_maxParticleDensity;
	glm::vec3 m_cellSize;
	float m_nearTransition;
	float m_farTransition;

	// Cache
	float m_period;
	glm::vec3 m_origin;
	glm::vec3 m_du;
	glm::vec3 m_dv;
	glm::vec3 m_dw;
	glm::vec3 m_duInv;
	glm::vec3 m_dvInv;
	glm::vec3 m_dwInv;

	float m_simuTime = 0.0;
};