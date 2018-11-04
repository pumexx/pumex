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

#pragma once
#include <set>
#include <unordered_map>
#include <mutex>
#include <pumex/Window.h>
#include <QtGui/QWindow>

namespace pumex
{

class WindowQT;

// QWindow descendant
class PUMEX_EXPORT QWindowPumex : public QWindow
{
  Q_OBJECT

public:
  explicit QWindowPumex(QWindow *parent = nullptr);
  explicit QWindowPumex(const WindowTraits& windowTraits);
  virtual ~QWindowPumex();

  std::shared_ptr<WindowQT> getWindowQT();

  bool event(QEvent *) override;
protected:
  std::shared_ptr<WindowQT> window;
};

// class implementing a pumex::Window for QT, contained within QWindowPumex
class PUMEX_EXPORT WindowQT : public pumex::Window, public std::enable_shared_from_this<WindowQT>
{
public:
  explicit WindowQT(QWindowPumex *owner = nullptr, const WindowTraits& windowTraits = WindowTraits());
  WindowQT(const WindowQT&)            = delete;
  WindowQT& operator=(const WindowQT&) = delete;
  virtual ~WindowQT();

  std::shared_ptr<Surface> createSurface(std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits) override;

  void endFrame() override;

  bool event(QEvent *e);

  void normalizeMouseCoordinates( float& x, float& y) const;

  InputEvent::Key qtKeyCodeToPumex(int keycode) const;

  static std::unique_ptr<QVulkanInstance> qtInstance;
protected:
  static void fillQTKeyCodes();

  static std::unordered_map<int, InputEvent::Key> qtKeycodes;
  QWindowPumex* owner = nullptr;

  std::set<InputEvent::MouseButton> pressedMouseButtons;
};

}
