//
// Copyright(c) 2017 Pawe³ Ksiê¿opolski ( pumexx )
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

#include <type_traits>
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <mutex>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/HPClock.h>

namespace pumex
{

class Viewer;
class Device;
class Surface;
struct SurfaceTraits;

// struct storing all information required to create a window
struct PUMEX_EXPORT WindowTraits
{
  enum Type{ WINDOW, FULLSCREEN, HALFSCREEN_LEFT, HALFSCREEN_RIGHT };
  WindowTraits(uint32_t screenNum, uint32_t x, uint32_t y, uint32_t w, uint32_t h, Type wType, const std::string& windowName);

  uint32_t screenNum = 0;
  uint32_t x         = 0;
  uint32_t y         = 0;
  uint32_t w         = 1;
  uint32_t h         = 1;
  Type     type      = WINDOW;
  std::string windowName;
};

// Class storing a single input event ( mouse or keyboard )
struct InputEvent
{
  enum Type { INPUT_UNDEFINED, MOUSE_MOVE, MOUSE_KEY_PRESSED, MOUSE_KEY_RELEASED, MOUSE_KEY_DOUBLE_PRESSED, KEYBOARD_KEY_PRESSED, KEYBOARD_KEY_RELEASED };
  enum MouseButton { BUTTON_UNDEFINED, LEFT, MIDDLE, RIGHT };
  enum Key { KEY_UNDEFINED, ESCAPE, SPACE, TAB, SHIFT, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12 };
  // mouse events
  InputEvent(HPClock::time_point t, Type mt, MouseButton b, float mx, float my )
    : time{ t }, type{ mt }, mouseButton{ b }, x{ mx }, y{ my }
  {
  }
  // keyboard events
  InputEvent(HPClock::time_point t, Type mt, Key k )
    : time{ t }, type{ mt }, key{ k }
  {
  }
  
  HPClock::time_point time;
  Type                type        = INPUT_UNDEFINED;
  MouseButton         mouseButton = BUTTON_UNDEFINED;
  float               x           = 0.0f;
  float               y           = 0.0f;
  Key                 key         = KEY_UNDEFINED;
};

// Helper class that enables iterating over modern C++ enums.
// Found this gem on Stack Overflow : https://stackoverflow.com/questions/261963/how-can-i-iterate-over-an-enum
// FIXME : this definitely should be moved out of here...

template < typename C, C beginVal, C endVal>
class EnumIterator 
{
  typedef typename std::underlying_type<C>::type val_t;
  int val;
public:
  EnumIterator(const C & f) : val(static_cast<val_t>(f)) 
  {
  }
  EnumIterator() : val(static_cast<val_t>(beginVal)) 
  {
  }
  EnumIterator operator++() 
  {
    ++val;
    return *this;
  }
  C operator*() 
  { 
    return static_cast<C>(val); 
  }
  EnumIterator begin() //default ctor is good
  { 
    return *this; 
  } 
  EnumIterator end() 
  {
      static const EnumIterator endIter=++EnumIterator(endVal); // cache it
      return endIter;
  }
  bool operator!=(const EnumIterator& i) 
  { 
    return val != i.val; 
  }
};

// Abstract base class representing a system window. Window is associated to a surface ( 1 to 1 association )
class PUMEX_EXPORT Window
{
public:
  Window()                         = default;
  Window(const Window&)            = delete;
  Window& operator=(const Window&) = delete;
  virtual ~Window()
  {}

  static std::shared_ptr<Window> createWindow(const WindowTraits& windowTraits);

  virtual std::shared_ptr<Surface> createSurface(std::shared_ptr<Viewer> viewer, std::shared_ptr<Device> device, const SurfaceTraits& surfaceTraits) = 0;
  uint32_t width     = 1;
  uint32_t height    = 1;
  uint32_t newWidth  = 1;
  uint32_t newHeight = 1;

  void pushInputEvent( const InputEvent& event );
  std::vector<InputEvent> getInputEvents();
protected:
  std::weak_ptr<Viewer>  viewer;
  std::weak_ptr<Surface> surface;

  mutable std::mutex inputMutex;
  std::vector<InputEvent> inputEvents;
};

}