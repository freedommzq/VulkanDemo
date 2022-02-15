#pragma once

#include "VulkanDevice.h"

class PipelineStatistics {
public:
	void init(vks::VulkanDevice* vkDevice);
	void destroy();

	void begin(VkCommandBuffer cb);
	void end(VkCommandBuffer cb);
	void reset(VkCommandBuffer cb);

	void getResult();
public:
	std::vector<uint64_t> pipelineStats;
	std::vector<std::string> pipelineStatNames;
private:
	vks::VulkanDevice* m_vkDevice;

	VkQueryPool m_queryPool;
};