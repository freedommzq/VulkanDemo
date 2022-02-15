#include "PipelineStatistics.h"

void PipelineStatistics::init(vks::VulkanDevice* vkDevice)
{
	m_vkDevice = vkDevice;

	pipelineStatNames = {
		"Input assembly vertex count        ",
		"Input assembly primitives count    ",
		"Vertex shader invocations          ",
		"Clipping primitives processed",
		"Clipping primitives output    ",
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
	VK_CHECK_RESULT(vkCreateQueryPool(m_vkDevice->logicalDevice, &queryPoolInfo, NULL, &m_queryPool));
}

void PipelineStatistics::destroy()
{
	vkDestroyQueryPool(m_vkDevice->logicalDevice, m_queryPool, nullptr);
}

void PipelineStatistics::begin(VkCommandBuffer cb)
{
	vkCmdBeginQuery(cb, m_queryPool, 0, 0);
}

void PipelineStatistics::end(VkCommandBuffer cb)
{
	vkCmdEndQuery(cb, m_queryPool, 0);
}

void PipelineStatistics::reset(VkCommandBuffer cb)
{
	vkCmdResetQueryPool(cb, m_queryPool, 0, static_cast<uint32_t>(pipelineStats.size()));
}

void PipelineStatistics::getResult()
{
	uint32_t count = static_cast<uint32_t>(pipelineStats.size());
	vkGetQueryPoolResults(
		m_vkDevice->logicalDevice,
		m_queryPool,
		0,
		1,
		count * sizeof(uint64_t),
		pipelineStats.data(),
		sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT);
}
