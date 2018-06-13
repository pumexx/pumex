//
// Copyright(c) 2017-2018 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <pumex/Window.h>
#if defined(_WIN32)
  #include <pumex/platform/win32/WindowWin32.h>
#elif defined(__linux__)
  #include <pumex/platform/linux/WindowXcb.h>
//#elif defined(__ANDROID__)
//  #include <pumex/platform/win32/WindowWin32.h>
#endif
#include <pumex/Viewer.h>
#include <pumex/Surface.h>

using namespace pumex;

WindowTraits::WindowTraits(uint32_t sn, uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah, Type wt, const std::string& aWindowName)
  : screenNum{ sn }, x{ ax }, y{ ay }, w{ aw }, h{ ah }, type{wt}, windowName(aWindowName)
{
}

Window::~Window()
{
}

std::shared_ptr<Window> Window::createWindow(const WindowTraits& windowTraits)
{
#if defined(_WIN32)
  return std::make_shared<WindowWin32>(windowTraits);
#elif defined(__linux__)
  return std::make_shared<WindowXcb>(windowTraits);
//#elif defined(__ANDROID__)
  //  #include <pumex/platform/win32/WindowWin32.h>
#endif

}

void Window::pushInputEvent(const InputEvent& event)
{
  std::lock_guard<std::mutex> lock(inputMutex);
  inputEvents.push_back(event);
}

std::vector<InputEvent> Window::getInputEvents()
{
  std::lock_guard<std::mutex> lock(inputMutex);
  auto result = inputEvents;
  inputEvents.clear();
  return result;
}
