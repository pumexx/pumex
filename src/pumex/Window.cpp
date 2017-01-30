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

WindowTraits::WindowTraits(uint32_t sn, uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah, bool af, const std::string& aWindowName)
  : screenNum{ sn }, x{ ax }, y{ ay }, w{ aw }, h{ ah }, fullscreen{af}, windowName(aWindowName)
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

void Window::setKeyState(const std::array<uint8_t, 256>& newKeyState)
{
  std::lock_guard<std::mutex> lock(inputMutex);
  keyState = newKeyState;
}


bool Window::isKeyPressed(uint8_t key) const 
{ 
  std::lock_guard<std::mutex> lock(inputMutex);
  return (keyState[key] & 0x80) != 0x00;
}


void Window::pushMouseEvent(const MouseEvent& event)
{
  std::lock_guard<std::mutex> lock(inputMutex);
  mouseEvents.push_back(event);
}

std::vector<MouseEvent> Window::getMouseEvents()
{
  std::lock_guard<std::mutex> lock(inputMutex);
  auto result = mouseEvents;
  mouseEvents.clear();
  return result;
}
