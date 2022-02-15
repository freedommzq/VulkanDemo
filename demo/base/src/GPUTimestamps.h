#pragma once

#include <vulkan/vulkan.h>

#include "VulkanDevice.h"

#include <unordered_map>

struct TimeStamp
{
	std::string m_label;
	float       m_microseconds;
};

class GPUTimestamps
{
public:
	void OnCreate(vks::VulkanDevice *pDevice, std::vector<std::string>&& labels);
	void OnDestroy();

	void NextTimeStamp(VkCommandBuffer cmd_buf);
	void OnBeginFrame(VkCommandBuffer cmd_buf);
	void GetQueryResult(std::vector<TimeStamp> *pTimestamps);

private:
	vks::VulkanDevice* m_pDevice;

	const uint32_t MaxValuesPerFrame = 128;

	VkQueryPool m_QueryPool;

	std::vector<std::string> m_labels;
	std::unordered_map<std::string, uint32_t> m_labelToOffset;

	uint32_t m_curOffset;
};