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
#include <QtGui/QVulkanInstance>
#include <QtGui/QPlatformSurfaceEvent>
#include <QtCore/QEvent>

using namespace pumex;

std::unordered_map<int, InputEvent::Key> WindowQT::qtKeycodes;
std::unique_ptr<QVulkanInstance>         WindowQT::qtInstance;

QWindowPumex::QWindowPumex(QWindow *parent)
  : QWindow(parent)
{
  window = std::make_shared<WindowQT>(this);
  setSurfaceType(QSurface::VulkanSurface);
  create();
}

QWindowPumex::QWindowPumex(const WindowTraits& windowTraits)
  : QWindow()
{
  window = std::make_shared<WindowQT>(this, windowTraits);
  setSurfaceType(QSurface::VulkanSurface);
  setPosition(windowTraits.x, windowTraits.y);
  resize(windowTraits.w, windowTraits.h);
  setTitle(QString::fromStdString(windowTraits.windowName));
  create();
}

QWindowPumex::~QWindowPumex()
{
  window = nullptr;
}

std::shared_ptr<WindowQT> QWindowPumex::getWindowQT()
{
  return window;
}

bool QWindowPumex::event(QEvent *e)
{
  window->event(e);
  return QWindow::event(e);
}

WindowQT::WindowQT(QWindowPumex* o, const WindowTraits& wt)
  : pumex::Window(wt), std::enable_shared_from_this<pumex::WindowQT>(), owner{ o }
{
  CHECK_LOG_THROW(wt.type != WindowTraits::WINDOW, "QT window may use only WindowTraits::WINDOW type");
  if (owner != nullptr)
  {
    width  = newWidth  = owner->width();
    height = newHeight = owner->height();
  }
  else
  {
    width  = newWidth  = wt.w;
    height = newHeight = wt.h;
  }
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
  owner->setVulkanInstance(qtInstance.get());

  VkSurfaceKHR vkSurface = QVulkanInstance::surfaceForWindow(owner);
  // surfaces used by QT are owned by QT and therefore are also destroyed by QT. We must inform Surface class not to call vkDestroySurfaceKHR() when QT window is used
  SurfaceTraits st = surfaceTraits;
  st.destroySurfaceDuringCleanup = false;
  std::shared_ptr<Surface> result = std::make_shared<Surface>(device, shared_from_this(), vkSurface, st);
  viewer->addSurface(result);

  surface = result;
  return result;
}

void WindowQT::endFrame()
{
  qtInstance->presentQueued(owner);
}

void WindowQT::normalizeMouseCoordinates(float& x, float& y) const
{
  // x and y are defined in windows coordinates as oposed to Windows OS
  x = x / width;
  y = y / height;
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

bool WindowQT::event(QEvent *e)
{
  auto timeNow = HPClock::now();
  switch (e->type()) {
  case QEvent::UpdateRequest:
    break;
  case QEvent::Resize:
  {
    QResizeEvent* event = static_cast<QResizeEvent*>(e);
    newWidth  = event->size().width();
    newHeight = event->size().height();
    auto surf = surface.lock();
    if(surf.get() != nullptr)
      surf->actions.addAction(std::bind(&Surface::resizeSurface, surf, newWidth, newHeight));
    width     = newWidth;
    height    = newHeight;
    break;
  }
  case QEvent::PlatformSurface:
    if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
    {
      auto surf = surface.lock();
      if (surf.get() != nullptr)
      {
        auto viewer = surf->viewer.lock();
        auto id     = surf->getID();
        viewer->removeSurface(id);
      }
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
  return true;
}
