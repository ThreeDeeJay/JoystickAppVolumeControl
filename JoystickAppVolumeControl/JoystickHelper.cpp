#include "JoystickHelper.h"
#include <tchar.h>

BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    LPDIRECTINPUT8 pDI = (LPDIRECTINPUT8)pContext;
    LPDIRECTINPUTDEVICE8* ppDev = (LPDIRECTINPUTDEVICE8*)(((char*)pContext) + sizeof(LPDIRECTINPUT8));
    if (SUCCEEDED(pDI->CreateDevice(pdidInstance->guidInstance, ppDev, nullptr)))
        return DIENUM_STOP;
    return DIENUM_CONTINUE;
}

bool JoystickHelper::Init(HWND hwnd) {
    if (m_pJoy) return true;

    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&m_pDI, nullptr)))
        return false;

    char context[sizeof(LPDIRECTINPUT8) + sizeof(LPDIRECTINPUTDEVICE8)] = { 0 };
    *(LPDIRECTINPUT8*)context = m_pDI;
    *(LPDIRECTINPUTDEVICE8*)(context + sizeof(LPDIRECTINPUT8)) = nullptr;
    if (FAILED(m_pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, context, DIEDFL_ATTACHEDONLY)))
        return false;

    m_pJoy = *(LPDIRECTINPUTDEVICE8*)(context + sizeof(LPDIRECTINPUT8));
    if (!m_pJoy) return false;

    m_pJoy->SetDataFormat(&c_dfDIJoystick);
    m_pJoy->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);

    return true;
}

bool JoystickHelper::GetAxisValue(LONG& value) {
    if (!m_pJoy) return false;
    DIJOYSTATE js = {};
    if (FAILED(m_pJoy->Poll())) {
        m_pJoy->Acquire();
        if (FAILED(m_pJoy->Poll())) return false;
    }
    if (FAILED(m_pJoy->GetDeviceState(sizeof(js), &js))) return false;
    value = js.lZ;
    m_lastValue = value;
    return true;
}

JoystickHelper::~JoystickHelper() {
    if (m_pJoy) m_pJoy->Unacquire(), m_pJoy->Release();
    if (m_pDI) m_pDI->Release();
}