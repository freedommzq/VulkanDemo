#include "GPUTimestamps.h"

void GPUTimestamps::OnCreate(vks::VulkanDevice *pDevice, std::vector<std::string>&& labels)
{
	m_pDevice = pDevice;
	m_labels = labels;

	for (int i = 0; i < m_labels.size(); ++i) {
		m_labelToOffset.emplace(m_labels[i], i);
	}

	const VkQueryPoolCreateInfo queryPoolCreateInfo =
	{
		VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,     // VkStructureType                  sType
		NULL,                                         // const void*                      pNext
		(VkQueryPoolCreateFlags)0,                    // VkQueryPoolCreateFlags           flags
		VK_QUERY_TYPE_TIMESTAMP ,                     // VkQueryType                      queryType
		MaxValuesPerFrame,                            // deUint32                         entryCount
		0,                                            // VkQueryPipelineStatisticFlags    pipelineStatistics
	};

	VkResult res = vkCreateQueryPool(pDevice->logicalDevice, &queryPoolCreateInfo, NULL, &m_QueryPool);
}

void GPUTimestamps::OnDestroy()
{
	vkDestroyQueryPool(m_pDevice->logicalDevice, m_QueryPool, nullptr);

	m_labels.clear();
	m_labelToOffset.clear();
}

void GPUTimestamps::NextTimeStamp(VkCommandBuffer cmd_buf)
{
	vkCmdWriteTimestamp(cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, ++m_curOffset);
}

void GPUTimestamps::OnBeginFrame(VkCommandBuffer cmd_buf)
{
	m_curOffset = 0;
	vkCmdResetQueryPool(cmd_buf, m_QueryPool, 0, MaxValuesPerFrame);
	vkCmdWriteTimestamp(cmd_buf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_QueryPool, m_curOffset);
}

void GPUTimestamps::GetQueryResult(std::vector<TimeStamp> *pTimestamps)
{
	pTimestamps->clear();
	pTimestamps->reserve(m_labels.size());

	uint32_t measurements = (uint32_t)m_labels.size();
	if (measurements > 0)
	{
		// timestampPeriod is the number of nanoseconds per timestamp value increment
		double microsecondsPerTick = (1e-3f * m_pDevice->properties.limits.timestampPeriod);
		{
			UINT64 TimingsInTicks[256] = {};
			VkResult res = vkGetQueryPoolResults(m_pDevice->logicalDevice, m_QueryPool, 0, measurements, measurements * sizeof(UINT64), &TimingsInTicks, sizeof(UINT64), VK_QUERY_RESULT_64_BIT);
			if (res == VK_SUCCESS)
			{
				for (uint32_t i = 1; i < measurements; i++)
				{
					TimeStamp ts = { m_labels[i], float(microsecondsPerTick * (double)(TimingsInTicks[i] - TimingsInTicks[i - 1])) };
					pTimestamps->push_back(ts);
				}

				// compute total
				TimeStamp ts = { "Total GPU Time (us)", float(microsecondsPerTick * (double)(TimingsInTicks[measurements - 1] - TimingsInTicks[0])) };
				pTimestamps->push_back(ts);
			}
			else
			{
				pTimestamps->push_back({ "GPU counters are invalid", 0.0f });
			}
		}
	}
}