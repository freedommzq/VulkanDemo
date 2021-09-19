#include "vksPass.h"

void vksPass::destroy()
{
	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_dsLayout, nullptr);

	if(m_framebuffer == VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
	if (m_framebuffer == VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
}