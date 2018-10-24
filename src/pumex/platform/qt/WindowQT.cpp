//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <pumex/platform/qt/WindowQT.h>
#include <cstring>
#include <pumex/Viewer.h>
#include <pumex/Surface.h>
#include <pumex/utils/Log.h>
#include <QtGui/QVulkanInstance.h>

using namespace pumex;

WindowQT::WindowQT(QWindow *parent)
  : QWindow(parent)
{
  setSurfaceType(QSurface::VulkanSurface);
}

WindowQT::WindowQT(const WindowTraits& windowTraits)
  : QWindow()
{
  setSurfaceType(QSurface::VulkanSurface);
}

WindowQT::~WindowQT()
{
}

std::shared_ptr<Surface> WindowQT::createSurface(std::shared_ptr<Viewer> v, std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits)
{
  QVulkanInstance* qtInstance = new QVulkanInstance;
  qtInstance->setVkInstance(v->getInstance());
  setVulkanInstance(qtInstance);

  VkSurfaceKHR vkSurface;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{};
  surfaceCreateInfo.sType      = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  surfaceCreateInfo.hinstance  = ::GetModuleHandle(NULL);
  surfaceCreateInfo.hwnd       = _hwnd;
  VK_CHECK_LOG_THROW(vkCreateWin32SurfaceKHR(v->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  VkXcbSurfaceCreateInfoKHR surfaceCreateInfo{};
  surfaceCreateInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  surfaceCreateInfo.connection = connection;
  surfaceCreateInfo.window     = window;
  VK_CHECK_LOG_THROW(vkCreateXcbSurfaceKHR(v->getInstance(), &surfaceCreateInfo, nullptr, &vkSurface), "Could not create surface");
#endif
  std::shared_ptr<Surface> result = std::make_shared<Surface>(v, shared_from_this(), device, vkSurface, surfaceTraits);

  viewer             = v;
  surface            = result;
  swapChainResizable = true;
  return result;
}

void WindowQT::normalizeMouseCoordinates(float& x, float& y) const
{
  // x and y are defined in windows coordinates as oposed to Windows OS
  x = x / width();
  y = y / height();
}
