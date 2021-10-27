#include "vksPass.h"

void vksPass::destroy()
{
	if (m_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(m_device, m_pipeline, nullptr);
		m_pipeline = VK_NULL_HANDLE;
	}
	if (m_pipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
		m_pipelineLayout = VK_NULL_HANDLE;
	}
	if (m_dsLayout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(m_device, m_dsLayout, nullptr);
		m_dsLayout = VK_NULL_HANDLE;
	}

	if (m_framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
		m_framebuffer = VK_NULL_HANDLE;
	}
	if (m_framebuffer != VK_NULL_HANDLE) {
		vkDestroyRenderPass(m_device, m_renderPass, nullptr);
		m_framebuffer = VK_NULL_HANDLE;
	}
}