#pragma once
#include <dinput.h>
#include <string>
#include <vector>

struct DInputDeviceInfo {
    GUID guid;
    std::wstring name;
};

class JoystickHelper {
    LPDIRECTINPUT8         m_pDI = nullptr;
    LPDIRECTINPUTDEVICE8   m_pJoy = nullptr;
    GUID                   m_guid;
public:
    JoystickHelper() = default;
    static void EnumerateDevices(std::vector<DInputDeviceInfo>& outList);

    bool Init(const GUID& guid, HWND hwnd);
    bool GetJoyState(DIJOYSTATE& js);
    ~JoystickHelper();
};