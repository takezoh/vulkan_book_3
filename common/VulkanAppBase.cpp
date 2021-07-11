#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#include <vector>
#include <sstream>


static VkBool32 VKAPI_CALL DebugReportCallback(
  VkDebugReportFlagsEXT flags,
  VkDebugReportObjectTypeEXT objactTypes,
  uint64_t object,
  size_t	location,
  int32_t messageCode,
  const char* pLayerPrefix,
  const char* pMessage,
  void* pUserData)
{
  VkBool32 ret = VK_FALSE;
  if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ||
    flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
  {
    ret = VK_TRUE;
  }
  std::stringstream ss;
  if (pLayerPrefix)
  {
    ss << "[" << pLayerPrefix << "] ";
  }
  ss << pMessage << std::endl;

  OutputDebugStringA(ss.str().c_str());

  return ret;
}


bool VulkanAppBase::OnSizeChanged(uint32_t width, uint32_t height)
{
  m_isMinimizedWindow = (width == 0 || height == 0);
  if (m_isMinimizedWindow)
  {
    return false;
  }
  vkDeviceWaitIdle(m_device);

  auto format = m_swapchain->GetSurfaceFormat().format;
  // �X���b�v�`�F�C������蒼��.
  m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, width, height, format);
  return true;
}

bool VulkanAppBase::OnMouseButtonDown(int button)
{
  return ImGui::GetIO().WantCaptureMouse;
}

bool VulkanAppBase::OnMouseButtonUp(int button)
{
  return ImGui::GetIO().WantCaptureMouse;
}

bool VulkanAppBase::OnMouseMove(int dx, int dy)
{
  return ImGui::GetIO().WantCaptureMouse;
}


uint32_t VulkanAppBase::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
  uint32_t result = ~0u;
  for (uint32_t i = 0; i < m_physicalMemProps.memoryTypeCount; ++i)
  {
    if (requestBits & 1)
    {
      const auto& types = m_physicalMemProps.memoryTypes[i];
      if ((types.propertyFlags & requestProps) == requestProps)
      {
        result = i;
        break;
      }
    }
    requestBits >>= 1;
  }
  return result;
}

void VulkanAppBase::SwitchFullscreen(GLFWwindow* window)
{
  static int lastWindowPosX, lastWindowPosY;
  static int lastWindowSizeW, lastWindowSizeH;

  auto monitor = glfwGetPrimaryMonitor();
  const auto mode = glfwGetVideoMode(monitor);

#if 1 // ���݂̃��j�^�[�ɍ��킹���T�C�Y�֕ύX.
  if (!m_isFullscreen)
  {
    // to fullscreen
    glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
    glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  }
  else
  {
    // to windowmode
    glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
#else
  // �w�肳�ꂽ�𑜓x�փ��j�^�[��ύX.
  if (!m_isFullscreen)
  {
    // to fullscreen
    glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
    glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
    glfwSetWindowMonitor(window, monitor, 0, 0, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
  else
  {
    // to windowmode
    glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
  }
#endif
  m_isFullscreen = !m_isFullscreen;
}

void VulkanAppBase::Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen)
{
  m_window = window;
  CreateInstance();

  // �����f�o�C�X�̑I��.
  uint32_t count;
  vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
  std::vector<VkPhysicalDevice> physicalDevices(count);
  vkEnumeratePhysicalDevices(m_vkInstance, &count, physicalDevices.data());
  // �ŏ��̃f�o�C�X���g�p����.
  m_physicalDevice = physicalDevices[0];
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalMemProps);

  // �O���t�B�b�N�X�̃L���[�C���f�b�N�X�擾.
  SelectGraphicsQueue();

#ifdef _DEBUG
  EnableDebugReport();
#endif
  // �_���f�o�C�X�̐���.
  CreateDevice();

  // �R�}���h�v�[���̐���.
  CreateCommandPool();

  VkSurfaceKHR surface;
  auto result = glfwCreateWindowSurface(m_vkInstance, window, nullptr, &surface);
  ThrowIfFailed(result, "glfwCreateWindowSurface Failed.");

  // �X���b�v�`�F�C���̐���.
  m_swapchain = std::make_unique<Swapchain>(m_vkInstance, m_device, surface);

  int width, height;
  glfwGetWindowSize(window, &width, &height);
  m_swapchain->Prepare(
    m_physicalDevice, m_gfxQueueIndex,
    uint32_t(width), uint32_t(height),
    format
  );
  auto imageCount = m_swapchain->GetImageCount();
  auto extent = m_swapchain->GetSurfaceExtent();

  VkSemaphoreCreateInfo semCI{
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    nullptr, 0,
  };

  vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderCompletedSem);
  vkCreateSemaphore(m_device, &semCI, nullptr, &m_presentCompletedSem);

  // �f�B�X�N���v�^�v�[���̐���.
  CreateDescriptorPool();

  m_renderPassStore = std::make_unique<RenderPassRegistry>([&](VkRenderPass renderPass) { vkDestroyRenderPass(m_device, renderPass, nullptr); });
  m_descriptorSetLayoutStore = std::make_unique<DescriptorSetLayoutManager>([&](VkDescriptorSetLayout layout) { vkDestroyDescriptorSetLayout(m_device, layout, nullptr); });
  m_pipelineLayoutStore = std::make_unique<PipelineLayoutManager>([&](VkPipelineLayout layout) { vkDestroyPipelineLayout(m_device, layout, nullptr); });

  Prepare();

  PrepareImGui();
}

void VulkanAppBase::Terminate()
{
  if (m_device != VK_NULL_HANDLE)
  {
    vkDeviceWaitIdle(m_device);
  }
  Cleanup();

  CleanupImGui();

  if (m_swapchain)
  {
    m_swapchain->Cleanup();
  }
#ifdef _DEBUG
  DisableDebugReport();
#endif

  m_renderPassStore->Cleanup();
  m_descriptorSetLayoutStore->Cleanup();
  m_pipelineLayoutStore->Cleanup();

  vkDestroySemaphore(m_device, m_renderCompletedSem, nullptr);
  vkDestroySemaphore(m_device, m_presentCompletedSem, nullptr);

  vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
  vkDestroyCommandPool(m_device, m_commandPool, nullptr);
  vkDestroyDevice(m_device, nullptr);
  vkDestroyInstance(m_vkInstance, nullptr);
  m_commandPool = VK_NULL_HANDLE;
  m_device = VK_NULL_HANDLE;
  m_vkInstance = VK_NULL_HANDLE;
}

VulkanAppBase::BufferObject VulkanAppBase::CreateBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
  BufferObject obj;
  VkBufferCreateInfo bufferCI{
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    nullptr, 0,
    size, usage,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr
  };
  auto result = vkCreateBuffer(m_device, &bufferCI, nullptr, &obj.buffer);
  ThrowIfFailed(result, "vkCreateBuffer Failed.");

  // �������ʂ̎Z�o.
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(m_device, obj.buffer, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, props)
  };
  vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
  vkBindBufferMemory(m_device, obj.buffer, obj.memory, 0);
  return obj;
}

VulkanAppBase::ImageObject VulkanAppBase::CreateTexture(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage)
{
  ImageObject obj;
  VkImageCreateInfo imageCI{
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    nullptr, 0,
    VK_IMAGE_TYPE_2D,
    format, { width, height, 1 },
    1, 1, VK_SAMPLE_COUNT_1_BIT,
    VK_IMAGE_TILING_OPTIMAL,
    usage,
    VK_SHARING_MODE_EXCLUSIVE,
    0, nullptr,
    VK_IMAGE_LAYOUT_UNDEFINED
  };
  auto result = vkCreateImage(m_device, &imageCI, nullptr, &obj.image);
  ThrowIfFailed(result, "vkCreateImage Failed.");

  // �������ʂ̎Z�o.
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, obj.image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  result = vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  vkBindImageMemory(m_device, obj.image, obj.memory, 0);

  VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
  {
    imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  VkImageViewCreateInfo viewCI{
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    nullptr, 0,
    obj.image,
    VK_IMAGE_VIEW_TYPE_2D,
    imageCI.format,
    book_util::DefaultComponentMapping(),
    { imageAspect, 0, 1, 0, 1}
  };
  result = vkCreateImageView(m_device, &viewCI, nullptr, &obj.view);
  ThrowIfFailed(result, "vkCreateImageView Failed.");
  return obj;
}

void VulkanAppBase::DestroyBuffer(BufferObject bufferObj)
{
  vkDestroyBuffer(m_device, bufferObj.buffer, nullptr);
  vkFreeMemory(m_device, bufferObj.memory, nullptr);
}

void VulkanAppBase::DestroyImage(ImageObject imageObj)
{
  vkDestroyImage(m_device, imageObj.image, nullptr);
  vkFreeMemory(m_device, imageObj.memory, nullptr);
  if (imageObj.view != VK_NULL_HANDLE)
  {
    vkDestroyImageView(m_device, imageObj.view, nullptr);
  }
}

VkFramebuffer VulkanAppBase::CreateFramebuffer(
  VkRenderPass renderPass, uint32_t width, uint32_t height, uint32_t viewCount, VkImageView* views)
{
  VkFramebufferCreateInfo fbCI{
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    nullptr, 0,
    renderPass,
    viewCount, views,
    width, height,
    1,
  };
  VkFramebuffer framebuffer;
  auto result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &framebuffer);
  ThrowIfFailed(result, "vkCreateFramebuffer Failed.");
  return framebuffer;
}
void VulkanAppBase::DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers)
{
  for (uint32_t i = 0; i < count; ++i)
  {
    vkDestroyFramebuffer(m_device, framebuffers[i], nullptr);
  }
}
VkFence VulkanAppBase::CreateFence()
{
  VkFenceCreateInfo fenceCI{
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    nullptr,
    VK_FENCE_CREATE_SIGNALED_BIT
  };
  VkFence fence;
  auto result = vkCreateFence(m_device, &fenceCI, nullptr, &fence);
  ThrowIfFailed(result, "vkCreateFence Failed.");
  return fence;
}
void VulkanAppBase::DestroyFence(VkFence fence)
{
  vkDestroyFence(m_device, fence, nullptr);
}

VkDescriptorSet VulkanAppBase::AllocateDescriptorSet(VkDescriptorSetLayout dsLayout)
{
  VkDescriptorSetAllocateInfo descriptorSetAI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    nullptr, m_descriptorPool,
    1, &dsLayout
  };

  VkDescriptorSet ds;
  auto result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &ds);
  ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");
  return ds;
}
void VulkanAppBase::DeallocateDescriptorSet(VkDescriptorSet descriptorSet)
{
  vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &descriptorSet);
}


VkCommandBuffer VulkanAppBase::CreateCommandBuffer(bool bBegin)
{
  VkCommandBufferAllocateInfo commandAI{
     VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    1
  };
  VkCommandBufferBeginInfo beginInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  VkCommandBuffer command;
  vkAllocateCommandBuffers(m_device, &commandAI, &command);
  if (bBegin)
  {
    vkBeginCommandBuffer(command, &beginInfo);
  }
  return command;
}

void VulkanAppBase::FinishCommandBuffer(VkCommandBuffer command)
{
  auto result = vkEndCommandBuffer(command);
  ThrowIfFailed(result, "vkEndCommandBuffer Failed.");
  VkFence fence;
  VkFenceCreateInfo fenceCI{
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    nullptr, 0
  };
  result = vkCreateFence(m_device, &fenceCI, nullptr, &fence);

  VkSubmitInfo submitInfo{
    VK_STRUCTURE_TYPE_SUBMIT_INFO,
    nullptr,
    0, nullptr,
    nullptr,
    1, &command,
    0, nullptr,
  };
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
  vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(m_device, fence, nullptr);
}

void VulkanAppBase::DestroyCommandBuffer(VkCommandBuffer command)
{
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

VkRect2D VulkanAppBase::GetSwapchainRenderArea() const
{
  return VkRect2D{
    VkOffset2D{0,0},
    m_swapchain->GetSurfaceExtent()
  };
}

std::vector<VulkanAppBase::BufferObject> VulkanAppBase::CreateUniformBuffers(uint32_t bufferSize, uint32_t imageCount)
{
  std::vector<BufferObject> buffers(imageCount);
  for (auto& b : buffers)
  {
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    b = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, props);
  }
  return buffers;
}

void VulkanAppBase::WriteToHostVisibleMemory(VkDeviceMemory memory, uint32_t size, const void* pData)
{
  void* p;
  vkMapMemory(m_device, memory, 0, VK_WHOLE_SIZE, 0, &p);
  memcpy(p, pData, size);
  vkUnmapMemory(m_device, memory);
}

void VulkanAppBase::AllocateCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands)
{
  VkCommandBufferAllocateInfo commandAI{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool,
    VK_COMMAND_BUFFER_LEVEL_SECONDARY, count
  };
  auto result = vkAllocateCommandBuffers(m_device, &commandAI, pCommands);
  ThrowIfFailed(result, "vkAllocateCommandBuffers Faield.");
}

void VulkanAppBase::FreeCommandBufferSecondary(uint32_t count, VkCommandBuffer* pCommands)
{
  vkFreeCommandBuffers(m_device, m_commandPool, count, pCommands);
}

void VulkanAppBase::TransferStageBufferToImage(
  const BufferObject& srcBuffer, const ImageObject& dstImage, const VkBufferImageCopy* region)
{ 
  VkImageMemoryBarrier imb{
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr,
    0, VK_ACCESS_TRANSFER_WRITE_BIT,
    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
    dstImage.image,
    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
  };

  // Staging ����]��.
  auto command = CreateCommandBuffer();
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr,
    0, nullptr, 1, &imb);

  vkCmdCopyBufferToImage(
    command, 
    srcBuffer.buffer, dstImage.image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, region);
  imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  vkCmdPipelineBarrier(command,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    0, 0, nullptr,
    0, nullptr,
    1, &imb);
  FinishCommandBuffer(command);
  DestroyCommandBuffer(command);
}


VkRenderPass VulkanAppBase::CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat, VkImageLayout layoutColor)
{
  VkRenderPass renderPass;

  std::vector<VkAttachmentDescription> attachments;

  if (colorFormat == VK_FORMAT_UNDEFINED)
  {
    colorFormat = m_swapchain->GetSurfaceFormat().format;
  }

  VkAttachmentDescription colorTarget, depthTarget;
  colorTarget = VkAttachmentDescription{
    0,  // Flags
    colorFormat,
    VK_SAMPLE_COUNT_1_BIT,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    layoutColor
  };
  depthTarget = VkAttachmentDescription{
    0,
    depthFormat,
    VK_SAMPLE_COUNT_1_BIT,
    VK_ATTACHMENT_LOAD_OP_CLEAR,
    VK_ATTACHMENT_STORE_OP_STORE,
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_STORE_OP_DONT_CARE,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };

  VkAttachmentReference colorRef{
    0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  VkAttachmentReference depthRef{
    1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  };

  VkSubpassDescription subpassDesc{
    0, // Flags
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    0, nullptr, // InputAttachments
    1, &colorRef, // ColorAttachments
    nullptr,    // ResolveAttachments
    nullptr,    // DepthStencilAttachments
    0, nullptr, // PreserveAttachments
  };

  attachments.push_back(colorTarget);
  if (depthFormat != VK_FORMAT_UNDEFINED)
  {
    attachments.push_back(depthTarget);
    subpassDesc.pDepthStencilAttachment = &depthRef;
  }

  VkRenderPassCreateInfo rpCI{
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    nullptr, 0,
    uint32_t(attachments.size()), attachments.data(),
    1, &subpassDesc,
    0, nullptr, // Dependency
  };
  auto result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
  ThrowIfFailed(result, "vkCreateRenderPass Failed.");
  return renderPass;
}


void VulkanAppBase::PrepareImGui()
{
  // ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplGlfw_InitForVulkan(m_window, true);

  ImGui_ImplVulkan_InitInfo info{};
  info.Instance = m_vkInstance;
  info.PhysicalDevice = m_physicalDevice;
  info.Device = m_device;
  info.QueueFamily = m_gfxQueueIndex;
  info.Queue = m_deviceQueue;
  info.DescriptorPool = m_descriptorPool;
  info.MinImageCount = m_swapchain->GetImageCount();
  info.ImageCount = m_swapchain->GetImageCount();
  ImGui_ImplVulkan_Init(&info, GetRenderPass("default"));

  // �t�H���g�e�N�X�`����]������.
  VkCommandBufferAllocateInfo commandAI{
   VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    nullptr, m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1
  };
  VkCommandBufferBeginInfo beginInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
  };

  VkCommandBuffer command;
  vkAllocateCommandBuffers(m_device, &commandAI, &command);
  vkBeginCommandBuffer(command, &beginInfo);
  ImGui_ImplVulkan_CreateFontsTexture(command);
  vkEndCommandBuffer(command);

  VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
  submitInfo.pCommandBuffers = &command;
  submitInfo.commandBufferCount = 1;
  vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);

  // �t�H���g�e�N�X�`���]���̊�����҂�.
  vkDeviceWaitIdle(m_device);
  vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

void VulkanAppBase::CleanupImGui()
{
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}


void VulkanAppBase::CreateInstance()
{
  VkApplicationInfo appinfo{};
  appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appinfo.pApplicationName = "VulkanBook2";
  appinfo.pEngineName = "VulkanBook2";
  appinfo.apiVersion = VK_API_VERSION_1_1;
  appinfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  // �g�����̎擾.
  uint32_t count;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> props(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
  std::vector<const char*> extensions;
  extensions.reserve(count);
  for (const auto& v : props)
  {
    extensions.push_back(v.extensionName);
  }

  VkInstanceCreateInfo instanceCI{};
  instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCI.enabledExtensionCount = count;
  instanceCI.ppEnabledExtensionNames = extensions.data();
  instanceCI.pApplicationInfo = &appinfo;
#ifdef _DEBUG
  // �f�o�b�O�r���h���ɂ͌��؃��C���[��L����
  const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
  if (VK_HEADER_VERSION_COMPLETE < VK_MAKE_VERSION(1, 1, 106)) {
	  // "VK_LAYER_LUNARG_standard_validation" �͔p�~�ɂȂ��Ă��邪�̂� Vulkan SDK �ł͓����̂őΏ����Ă���.
	  layers[0] = "VK_LAYER_LUNARG_standard_validation";
  }
  instanceCI.enabledLayerCount = 1;
  instanceCI.ppEnabledLayerNames = layers;
#endif
  auto result = vkCreateInstance(&instanceCI, nullptr, &m_vkInstance);
  ThrowIfFailed(result, "vkCreateInstance Failed.");
}

void VulkanAppBase::SelectGraphicsQueue()
{
  // �O���t�B�b�N�X�L���[�̃C���f�b�N�X�l���擾.
  uint32_t queuePropCount;
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilyProps(queuePropCount);
  vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, queueFamilyProps.data());
  uint32_t graphicsQueue = ~0u;
  for (uint32_t i = 0; i < queuePropCount; ++i)
  {
    if (queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphicsQueue = i; break;
    }
  }
  m_gfxQueueIndex = graphicsQueue;
}

void VulkanAppBase::CreateDevice()
{
  const float defaultQueuePriority(1.0f);
  VkDeviceQueueCreateInfo devQueueCI{
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
    nullptr, 0,
    m_gfxQueueIndex,
    1, &defaultQueuePriority
  };
  uint32_t count;
  vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> deviceExtensions(count);
  vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, deviceExtensions.data());

  std::vector<const char*> extensions;
  extensions.reserve(count);
  for (const auto& v : deviceExtensions)
  {
    extensions.push_back(v.extensionName);
  }

  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceFeatures(m_physicalDevice, &features);

  VkDeviceCreateInfo deviceCI{
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    nullptr, 0,
    1, &devQueueCI,
    0, nullptr,
    count, extensions.data(),
    &features
  };
  auto result = vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device);
  ThrowIfFailed(result, "vkCreateDevice Failed.");

  vkGetDeviceQueue(m_device, m_gfxQueueIndex, 0, &m_deviceQueue);
}

void VulkanAppBase::CreateCommandPool()
{
  VkCommandPoolCreateInfo cmdPoolCI{
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    nullptr,
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    m_gfxQueueIndex
  };
  auto result = vkCreateCommandPool(m_device, &cmdPoolCI, nullptr, &m_commandPool);
  ThrowIfFailed(result, "vkCreateCommandPool Failed.");
}

void VulkanAppBase::CreateDescriptorPool()
{
  VkResult result;
  VkDescriptorPoolSize poolSize[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
  };
  VkDescriptorPoolCreateInfo descPoolCI{
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    nullptr,  VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    1000 * _countof(poolSize), // maxSets
    _countof(poolSize), poolSize,
  };
  result = vkCreateDescriptorPool(m_device, &descPoolCI, nullptr, &m_descriptorPool);
  ThrowIfFailed(result, "vkCreateDescriptorPool Failed.");
}

VkDeviceMemory VulkanAppBase::AllocateMemory(VkBuffer buffer, VkMemoryPropertyFlags memProps)
{
  VkDeviceMemory memory;
  VkMemoryRequirements reqs;
  vkGetBufferMemoryRequirements(m_device, buffer, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  auto result = vkAllocateMemory(m_device, &info, nullptr, &memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  return memory;
}

VkDeviceMemory VulkanAppBase::AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps)
{
  VkDeviceMemory memory;
  VkMemoryRequirements reqs;
  vkGetImageMemoryRequirements(m_device, image, &reqs);
  VkMemoryAllocateInfo info{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    nullptr,
    reqs.size,
    GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };
  auto result = vkAllocateMemory(m_device, &info, nullptr, &memory);
  ThrowIfFailed(result, "vkAllocateMemory Failed.");
  return memory;
}


#define GetInstanceProcAddr(FuncName) \
  m_##FuncName = reinterpret_cast<PFN_##FuncName>(vkGetInstanceProcAddr(m_vkInstance, #FuncName))

void VulkanAppBase::EnableDebugReport()
{
  GetInstanceProcAddr(vkCreateDebugReportCallbackEXT);
  GetInstanceProcAddr(vkDebugReportMessageEXT);
  GetInstanceProcAddr(vkDestroyDebugReportCallbackEXT);

  VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

  VkDebugReportCallbackCreateInfoEXT drcCI{};
  drcCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  drcCI.flags = flags;
  drcCI.pfnCallback = &DebugReportCallback;
  auto result = m_vkCreateDebugReportCallbackEXT(m_vkInstance, &drcCI, nullptr, &m_debugReport);
  ThrowIfFailed(result, "vkCreateDebugReportCallback Failed.");
}

void VulkanAppBase::DisableDebugReport()
{
  if (m_vkDestroyDebugReportCallbackEXT)
  {
    m_vkDestroyDebugReportCallbackEXT(m_vkInstance, m_debugReport, nullptr);
  }
}

void VulkanAppBase::MsgLoopMinimizedWindow()
{
  int width, height;
  do
  {
    glfwGetWindowSize(m_window, &width, &height);
    glfwWaitEvents();
  } while (width == 0 || height == 0);
}
