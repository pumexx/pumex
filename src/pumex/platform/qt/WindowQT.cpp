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
#include <qevent.h>

using namespace pumex;

std::unordered_map<int, InputEvent::Key> WindowQT::qtKeycodes;
std::unique_ptr<QVulkanInstance>         WindowQT::qtInstance;


WindowQT::WindowQT(QWindow *parent)
  : QWindow(parent)
{
  setSurfaceType(QSurface::VulkanSurface);
  if (qtKeycodes.empty())
    fillQTKeyCodes();
}

WindowQT::WindowQT(const WindowTraits& windowTraits)
  : QWindow()
{
  CHECK_LOG_THROW(windowTraits.type != WindowTraits::WINDOW, "QT window may use only WindowTraits::WINDOW type");
  setSurfaceType(QSurface::VulkanSurface);
  setPosition(windowTraits.x, windowTraits.y);
  setWidth(windowTraits.w);
  setHeight(windowTraits.h);
  setTitle(QString::fromStdString(windowTraits.windowName));
  show();
  Window::width  = windowTraits.w;
  Window::height = windowTraits.h;
  if (qtKeycodes.empty())
    fillQTKeyCodes();
}

WindowQT::~WindowQT()
{
}

std::shared_ptr<Surface> WindowQT::createSurface(std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits)
{
  // return existing surface when it was already created
  if (!surface.expired())
    return surface.lock();
  auto viewer = device->viewer.lock();
  if (qtInstance.get() == nullptr)
  {
    qtInstance = std::make_unique<QVulkanInstance>();
    qtInstance->setVkInstance(viewer->getInstance());
    qtInstance->create();
  }
  setVulkanInstance(qtInstance.get());

  VkSurfaceKHR vkSurface = QVulkanInstance::surfaceForWindow(this);
  // surfaces used by QT are owned by QT and therefore are also destroyed by QT. We must informs Surface class not to call vkDestroySurfaceKHR() when QT window is used
  SurfaceTraits st = surfaceTraits;
  st.destroySurfaceDuringCleanup = false;
  std::shared_ptr<Surface> result = std::make_shared<Surface>(device, shared_from_this(), vkSurface, st);
  viewer->addSurface(result);

  surface = result;
  return result;
}

void WindowQT::endFrame()
{
  qtInstance->presentQueued(this);
}

void WindowQT::normalizeMouseCoordinates(float& x, float& y) const
{
  // x and y are defined in windows coordinates as oposed to Windows OS
  x = x / Window::width;
  y = y / Window::height;
}

InputEvent::Key WindowQT::qtKeyCodeToPumex(int keycode) const
{
  auto it = qtKeycodes.find(keycode);
  if (it != end(qtKeycodes))
    return it->second;
  //  LOG_ERROR << "Unknown keycode : 0x" << std::hex << (uint32_t)keycode << std::endl;
  return InputEvent::KEY_UNDEFINED;
}


void WindowQT::fillQTKeyCodes()
{
  // important keys
  qtKeycodes.insert({ Qt::Key_Escape, InputEvent::ESCAPE });
  qtKeycodes.insert({ Qt::Key_Space,  InputEvent::SPACE });
  qtKeycodes.insert({ Qt::Key_Tab,    InputEvent::TAB });
  qtKeycodes.insert({ Qt::Key_Shift,  InputEvent::SHIFT });

  int i = 0;

  // keys F1-F10
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::F1, InputEvent::Key::F10> FunKeyIterator;
  i = Qt::Key_F1;
  for (InputEvent::Key f : FunKeyIterator())
    qtKeycodes.insert({ i++, f });

  // numbers
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::N0, InputEvent::Key::N9> NumKeyIterator;
  i = Qt::Key_0;
  for (InputEvent::Key f : NumKeyIterator())
    qtKeycodes.insert({ i++, f });

  // letters
  typedef EnumIterator<InputEvent::Key, InputEvent::Key::A, InputEvent::Key::Z> LetterKeyIterator;
  i = Qt::Key_A;
  for (InputEvent::Key f : LetterKeyIterator())
    qtKeycodes.insert({ i++, f });
}


void WindowQT::exposeEvent(QExposeEvent *)
{
  //if (isExposed() && !m_inited) 
  //{
  //  m_inited = true;
  //  init();
  //  recreateSwapChain();
  //  render();
  //}

  //if (!isExposed() && m_inited) {
  //  m_inited = false;
  //  releaseSwapChain();
  //  releaseResources();
  //}
}

bool WindowQT::event(QEvent *e)
{
  auto timeNow = HPClock::now();
  switch (e->type()) {
  case QEvent::UpdateRequest:
    break;
  case QEvent::PlatformSurface:
    if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
    { 
      surface.lock()->viewer.lock()->removeSurface(surface.lock()->getID());
      if (mainWindow)
        surface.lock()->viewer.lock()->setTerminate();
    }
    break;
  case QEvent::MouseMove:
  {
    QMouseEvent* event = static_cast<QMouseEvent*>(e);
    float mx = event->x();
    float my = event->y();
    normalizeMouseCoordinates(mx, my);
    pushInputEvent(InputEvent(timeNow, InputEvent::MOUSE_MOVE, InputEvent::BUTTON_UNDEFINED, mx, my));
    break;
  }
  case QEvent::MouseButtonPress:
  {
    QMouseEvent* event = static_cast<QMouseEvent*>(e);

    InputEvent::MouseButton button = InputEvent::BUTTON_UNDEFINED;
    if (event->button() == Qt::MouseButton::LeftButton)        button = InputEvent::LEFT;
    else if (event->button() == Qt::MouseButton::MiddleButton) button = InputEvent::MIDDLE;
    else if (event->button() == Qt::MouseButton::RightButton)  button = InputEvent::RIGHT;
    else break;
    pressedMouseButtons.insert(button);

    float mx = event->x();
    float my = event->y();
    normalizeMouseCoordinates(mx, my);
    pushInputEvent(InputEvent(timeNow, InputEvent::MOUSE_KEY_PRESSED, button, mx, my));
    break;
  }
  case QEvent::MouseButtonRelease:
  {
    QMouseEvent* event = static_cast<QMouseEvent*>(e);

    InputEvent::MouseButton button = InputEvent::BUTTON_UNDEFINED;
    if (event->button() == Qt::MouseButton::LeftButton)        button = InputEvent::LEFT;
    else if (event->button() == Qt::MouseButton::MiddleButton) button = InputEvent::MIDDLE;
    else if (event->button() == Qt::MouseButton::RightButton)  button = InputEvent::RIGHT;
    else break;
    pressedMouseButtons.erase(button);

    float mx = event->x();
    float my = event->y();
    normalizeMouseCoordinates(mx, my);
    pushInputEvent(InputEvent(timeNow, InputEvent::MOUSE_KEY_RELEASED, button, mx, my));
    break;
  }
  case QEvent::MouseButtonDblClick:
  {
    QMouseEvent* event = static_cast<QMouseEvent*>(e);

    InputEvent::MouseButton button = InputEvent::BUTTON_UNDEFINED;
    if (event->button() == Qt::MouseButton::LeftButton)        button = InputEvent::LEFT;
    else if (event->button() == Qt::MouseButton::MiddleButton) button = InputEvent::MIDDLE;
    else if (event->button() == Qt::MouseButton::RightButton)  button = InputEvent::RIGHT;
    else break;
    pressedMouseButtons.insert(button);

    float mx = event->x();
    float my = event->y();
    normalizeMouseCoordinates(mx, my);
    pushInputEvent(InputEvent(timeNow, InputEvent::MOUSE_KEY_DOUBLE_PRESSED, button, mx, my));
    break;
  }
  case QEvent::KeyPress:
  {
    QKeyEvent* event    = static_cast<QKeyEvent*>(e);
    InputEvent::Key key = qtKeyCodeToPumex(event->key());
    pushInputEvent(InputEvent(timeNow, InputEvent::KEYBOARD_KEY_PRESSED, key));
    break;
  }
  case QEvent::KeyRelease:
  {
    QKeyEvent* event    = static_cast<QKeyEvent*>(e);
    InputEvent::Key key = qtKeyCodeToPumex(event->key());
    pushInputEvent(InputEvent(timeNow, InputEvent::KEYBOARD_KEY_RELEASED, key));
    break;
  }
  default:
    break;
  }
  return QWindow::event(e);
}