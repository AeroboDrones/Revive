#include "OVR_CAPI_Vk.h"
#include "Common.h"
#include "Session.h"
#include "Runtime.h"
#include "SwapchainVk.h"

#include <vector>

#define XR_USE_GRAPHICS_API_VULKAN
#include "vulkan.h"
#include <openxr/openxr_platform.h>

extern XrInstance g_Instance;

HMODULE VulkanLibrary;
XrGraphicsBindingVulkanKHR g_Binding = XR_TYPE(GRAPHICS_BINDING_VULKAN_KHR);
VkQueue g_Queue = VK_NULL_HANDLE;

// Global functions
VK_DEFINE_FUNCTION(vkGetInstanceProcAddr)
VK_DEFINE_FUNCTION(vkGetDeviceProcAddr)

// Local functions
VK_DEFINE_FUNCTION(vkGetPhysicalDeviceProperties)
VK_DEFINE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties)
VK_DEFINE_FUNCTION(vkGetDeviceQueue)

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetInstanceExtensionsVk(
	ovrGraphicsLuid luid,
	char* extensionNames,
	uint32_t* inoutExtensionNamesSize)
{
	if (!inoutExtensionNamesSize)
		ovrError_InvalidParameter;

	if (Runtime::Get().Supports(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		XR_FUNCTION(g_Instance, GetVulkanInstanceExtensionsKHR);

		XrSystemId system;
		XrSystemGetInfo info = XR_TYPE(SYSTEM_GET_INFO);
		info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		CHK_XR(xrGetSystem(g_Instance, &info, &system));
		CHK_XR(GetVulkanInstanceExtensionsKHR(g_Instance, system, *inoutExtensionNamesSize, inoutExtensionNamesSize, extensionNames));
	}
	else
	{
		uint32_t size = *inoutExtensionNamesSize;
		*inoutExtensionNamesSize = 1;

		if (extensionNames && size >= 1)
			*extensionNames = '\0';
		if (size < 1)
			return ovrError_InsufficientArraySize;
	}
	return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetDeviceExtensionsVk(
	ovrGraphicsLuid luid,
	char* extensionNames,
	uint32_t* inoutExtensionNamesSize)
{
	if (!inoutExtensionNamesSize)
		ovrError_InvalidParameter;

	if (Runtime::Get().Supports(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
	{
		XR_FUNCTION(g_Instance, GetVulkanDeviceExtensionsKHR);

		XrSystemId system;
		XrSystemGetInfo info = XR_TYPE(SYSTEM_GET_INFO);
		info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		CHK_XR(xrGetSystem(g_Instance, &info, &system));
		CHK_XR(GetVulkanDeviceExtensionsKHR(g_Instance, system, *inoutExtensionNamesSize, inoutExtensionNamesSize, extensionNames));
	}
	else
	{
		uint32_t size = *inoutExtensionNamesSize;
		*inoutExtensionNamesSize = 1;

		if (extensionNames && size >= 1)
			*extensionNames = '\0';
		if (size < 1)
			return ovrError_InsufficientArraySize;
	}
	return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetSessionPhysicalDeviceVk(
	ovrSession session,
	ovrGraphicsLuid luid,
	VkInstance instance,
	VkPhysicalDevice* out_physicalDevice)
{
	XR_FUNCTION(session->Instance, GetVulkanGraphicsDeviceKHR);

	if (!out_physicalDevice)
		ovrError_InvalidParameter;

	VulkanLibrary = LoadLibraryW(L"vulkan-1.dll");
	if (!VulkanLibrary)
		return ovrError_InitializeVulkan;

	VK_LIBRARY_FUNCTION(VulkanLibrary, vkGetInstanceProcAddr);
	VK_INSTANCE_FUNCTION(instance, vkGetDeviceProcAddr);
	VK_INSTANCE_FUNCTION(instance, vkGetPhysicalDeviceProperties);
	VK_INSTANCE_FUNCTION(instance, vkGetPhysicalDeviceQueueFamilyProperties);

	CHK_XR(GetVulkanGraphicsDeviceKHR(session->Instance, session->System, instance, &g_Binding.physicalDevice));
	g_Binding.instance = instance;

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(g_Binding.physicalDevice, &props);

	// The extension uses the OpenXR version format instead of the Vulkan one
	XrVersion version = XR_MAKE_VERSION(
		VK_VERSION_MAJOR(props.apiVersion),
		VK_VERSION_MINOR(props.apiVersion),
		VK_VERSION_PATCH(props.apiVersion)
	);

	// This should always succeed, it's more of a sanity check
	XR_FUNCTION(session->Instance, GetVulkanGraphicsRequirementsKHR);
	XrGraphicsRequirementsVulkanKHR graphicsReq = XR_TYPE(GRAPHICS_REQUIREMENTS_VULKAN_KHR);
	CHK_XR(GetVulkanGraphicsRequirementsKHR(session->Instance, session->System, &graphicsReq));
	if (version < graphicsReq.minApiVersionSupported)
		return ovrError_IncompatibleGPU;

	*out_physicalDevice = g_Binding.physicalDevice;
	return ovrSuccess;
}

#undef ovr_SetSynchonizationQueueVk
OVR_PUBLIC_FUNCTION(ovrResult) ovr_SetSynchonizationQueueVk(ovrSession session, VkQueue queue)
{
	return ovr_SetSynchronizationQueueVk(session, queue);
}

OVR_PUBLIC_FUNCTION(ovrResult) ovr_SetSynchronizationQueueVk(ovrSession session, VkQueue queue)
{
	g_Queue = queue;
	return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_CreateTextureSwapChainVk(
	ovrSession session,
	VkDevice device,
	const ovrTextureSwapChainDesc* desc,
	ovrTextureSwapChain* out_TextureSwapChain)
{
	REV_TRACE(ovr_CreateTextureSwapChainVk);

	if (!session)
		return ovrError_InvalidSession;

	if (!device || !desc || !out_TextureSwapChain || desc->Type == ovrTexture_2D_External)
		return ovrError_InvalidParameter;

	if (desc->Type == ovrTexture_Cube && !Runtime::Get().CompositionCube)
		return ovrError_Unsupported;

	if (!session->Session)
	{
		if (!Runtime::Get().Supports(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))
			return ovrError_Unsupported;

		VK_DEVICE_FUNCTION(device, vkGetDeviceQueue);

		auto findQueue = [device](VkQueue queue, uint32_t& familyIndex, uint32_t& queueIndex) {
			if (queue == VK_NULL_HANDLE)
				return false;

			uint32_t familyCount = 0;
			std::vector<VkQueueFamilyProperties> familyProps;
			vkGetPhysicalDeviceQueueFamilyProperties(g_Binding.physicalDevice, &familyCount, nullptr);
			familyProps.resize(familyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(g_Binding.physicalDevice, &familyCount, familyProps.data());

			for (familyIndex = 0; familyIndex < familyCount; familyIndex++)
			{
				for (queueIndex = 0; queueIndex < familyProps[familyIndex].queueCount; queueIndex++)
				{
					VkQueue q;
					vkGetDeviceQueue(device, familyIndex, queueIndex, &q);
					if (q == queue)
						return true;
				}
			}

			return false;
		};

		g_Binding.device = device;

		if (!findQueue(g_Queue, g_Binding.queueFamilyIndex, g_Binding.queueIndex))
		{
			g_Binding.queueFamilyIndex = 0;
			g_Binding.queueIndex = 0;
		}

		session->StartSession(&g_Binding);
	}

	return ovrTextureSwapChainVk::Create(session, desc, out_TextureSwapChain);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetTextureSwapChainBufferVk(
	ovrSession session,
	ovrTextureSwapChain chain,
	int index,
	VkImage* out_Image)
{
	REV_TRACE(ovr_GetTextureSwapChainBufferVk);

	if (!session)
		return ovrError_InvalidSession;

	if (!chain || !out_Image)
		return ovrError_InvalidParameter;

	if (index < 0)
		index = chain->CurrentIndex;

	*out_Image = ((XrSwapchainImageVulkanKHR*)chain->Images)[index].image;
	return ovrSuccess;
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_CreateMirrorTextureWithOptionsVk(
	ovrSession session,
	VkDevice device,
	const ovrMirrorTextureDesc* desc,
	ovrMirrorTexture* out_MirrorTexture)
{
	REV_TRACE(ovr_CreateMirrorTextureWithOptionsVk);

	if (!session)
		return ovrError_InvalidSession;

	if (!device || !desc || !out_MirrorTexture)
		return ovrError_InvalidParameter;

	ovrMirrorTexture mirrorTexture = new ovrMirrorTextureData();
	mirrorTexture->Desc = *desc;
	*out_MirrorTexture = mirrorTexture;

	ovrTextureSwapChainDesc swapDesc;
	swapDesc.Type = ovrTexture_2D;
	swapDesc.Format = desc->Format;
	swapDesc.ArraySize = 1;
	swapDesc.Width = desc->Width;
	swapDesc.Height = desc->Height;
	swapDesc.MipLevels = 1;
	swapDesc.SampleCount = 1;
	swapDesc.StaticImage = ovrTrue;
	swapDesc.MiscFlags = desc->MiscFlags;
	swapDesc.BindFlags = 0;
	return ovr_CreateTextureSwapChainVk(session, device, &swapDesc, &mirrorTexture->Dummy);
}

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_GetMirrorTextureBufferVk(
	ovrSession session,
	ovrMirrorTexture mirrorTexture,
	VkImage* out_Image)
{
	REV_TRACE(ovr_GetMirrorTextureBufferVk);

	if (!session)
		return ovrError_InvalidSession;

	if (!mirrorTexture || !out_Image)
		return ovrError_InvalidParameter;

	return ovr_GetTextureSwapChainBufferVk(session, mirrorTexture->Dummy, 0, out_Image);
}

