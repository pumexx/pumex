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
#include <unordered_map>
#include <mutex>
#include <pumex/Window.h>
#include <QtGui/QWindow.h>

namespace pumex
{

// class implementing a pumex::Window for QT
class PUMEX_EXPORT WindowQT : public Window, public QWindow, public std::enable_shared_from_this<WindowQT>
{
  Q_OBJECT
public:
  explicit WindowQT(QWindow *parent);
  explicit WindowQT(const WindowTraits& windowTraits);
  WindowQT(const WindowQT&)            = delete;
  WindowQT& operator=(const WindowQT&) = delete;
  virtual ~WindowQT();

  std::shared_ptr<Surface> createSurface(std::shared_ptr<Viewer> viewer, std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits) override;

//  static bool checkWindowMessages();

  void normalizeMouseCoordinates( float& x, float& y) const;

  bool                swapChainResizable  = false;
  float               lastMouseX          = 0.0f;
  float               lastMouseY          = 0.0f;
  bool                resizeCalled        = false;
  HPClock::time_point lastResizeTimePoint;
};

}
