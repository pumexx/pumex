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
#include <pumex/Export.h>
#include <pumex/HPClock.h>

namespace pumex
{

class Viewer;

struct PUMEX_EXPORT InputEvent
{
  enum Type { INPUT_UNDEFINED, MOUSE_MOVE, MOUSE_KEY_PRESSED, MOUSE_KEY_RELEASED, MOUSE_KEY_DOUBLE_PRESSED, KEYBOARD_KEY_PRESSED, KEYBOARD_KEY_RELEASED };
  enum MouseButton { BUTTON_UNDEFINED, LEFT, MIDDLE, RIGHT };
  enum Key { KEY_UNDEFINED, ESCAPE, SPACE, TAB, SHIFT, N0, N1, N2, N3, N4, N5, N6, N7, N8, N9, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12 };
  // mouse events
  InputEvent(HPClock::time_point t, Type mt, MouseButton b, float mx, float my)
    : time{ t }, type{ mt }, mouseButton{ b }, x{ mx }, y{ my }
  {
  }
  // keyboard events
  InputEvent(HPClock::time_point t, Type mt, Key k)
    : time{ t }, type{ mt }, key{ k }
  {
  }

  HPClock::time_point time;
  Type                type = INPUT_UNDEFINED;
  MouseButton         mouseButton = BUTTON_UNDEFINED;
  float               x = 0.0f;
  float               y = 0.0f;
  Key                 key = KEY_UNDEFINED;
};

class PUMEX_EXPORT InputEventHandler
{
public:
  virtual ~InputEventHandler();
  virtual bool handle(const InputEvent& iEvent, Viewer* viewer) = 0;
};

}
