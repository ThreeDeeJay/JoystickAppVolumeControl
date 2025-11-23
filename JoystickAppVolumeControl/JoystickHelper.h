#pragma once
#include <dinput.h>

class JoystickHelper {
    LPDIRECTINPUT8         m_pDI = nullptr;
    LPDIRECTINPUTDEVICE8   m_pJoy = nullptr;
    LONG                   m_lastValue = 0;
public:
    bool Init(HWND hwnd);
    bool GetAxisValue(LONG& value);
    ~JoystickHelper();
};