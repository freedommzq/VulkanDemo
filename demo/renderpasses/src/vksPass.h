#pragma once

#include "vulkanexamplebase.h"

#include <vector>

class vksPass {
public:
	vksPass(VkDevice device, uint32_t width = 0, uint32_t height = 0)
		: m_device(device), m_width(width), m_height(height) {}

	void destroy();

	const VkDevice			m_device;
	const uint32_t			m_width, m_height;

	VkRenderPass			m_renderPass		= VK_NULL_HANDLE;
	VkFramebuffer			m_framebuffer		= VK_NULL_HANDLE;

	VkDescriptorSetLayout	m_dsLayout			= VK_NULL_HANDLE;
	VkPipelineLayout		m_pipelineLayout	= VK_NULL_HANDLE;
	VkPipeline				m_pipeline			= VK_NULL_HANDLE;
};