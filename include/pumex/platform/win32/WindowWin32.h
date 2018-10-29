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

#pragma once
#include <unordered_map>
#include <set>
#include <pumex/Window.h>
#include <windows.h>
#include <windowsx.h>

namespace pumex
{

// Class implementing a pumex::Window for MS Windows system
class PUMEX_EXPORT WindowWin32 : public Window, public std::enable_shared_from_this<WindowWin32>
{
public:
  explicit WindowWin32(const WindowTraits& windowTraits);
  WindowWin32(const WindowWin32&)            = delete;
  WindowWin32& operator=(const WindowWin32&) = delete;
  WindowWin32(WindowWin32&&)                 = delete;
  WindowWin32& operator=(WindowWin32&&)      = delete;
  virtual ~WindowWin32();

  static void              registerWindow(HWND hwnd, WindowWin32* window);
  static void              unregisterWindow(HWND hwnd);
  static WindowWin32*      getWindow(HWND hwnd);

  std::shared_ptr<Surface> createSurface(std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits) override;

  static bool              checkWindowMessages();

  LRESULT                  handleWin32Messages(UINT msg, WPARAM wParam, LPARAM lParam);

  void                     normalizeMouseCoordinates( float& x, float& y);

  InputEvent::Key          win32KeyCodeToPumex(WPARAM keycode) const;

protected:
  static void fillWin32Keycodes();

  HWND                                               _hwnd              = nullptr;
  bool                                               swapChainResizable = false;
  bool                                               sizeMaximized      = false;
  static std::unordered_map<WPARAM, InputEvent::Key> win32Keycodes;
  static std::unordered_map<HWND, WindowWin32*>      registeredWindows;
  std::set<InputEvent::MouseButton>                  pressedMouseButtons;
};

}
