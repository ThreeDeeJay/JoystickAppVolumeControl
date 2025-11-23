#include "JoystickHelper.h"
#include "log.h"

BOOL CALLBACK EnumJoyDevCB(const DIDEVICEINSTANCE* pdidInstance, VOID* pContext) {
    std::vector<DInputDeviceInfo>* outList = (std::vector<DInputDeviceInfo>*)pContext;
    DInputDeviceInfo info = { pdidInstance->guidInstance, pdidInstance->tszProductName };
    outList->push_back(info);
    return DIENUM_CONTINUE;
}

void JoystickHelper::EnumerateDevices(std::vector<DInputDeviceInfo>& outList) {
    LPDIRECTINPUT8 pDI = nullptr;
    HRESULT hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (VOID**)&pDI, nullptr);
    if (FAILED(hr)) {
        DEBUG_LOG("DirectInput8Create failed: hr=0x%08X", hr);
        return;
    }
    outList.clear();
    pDI->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoyDevCB, &outList, DIEDFL_ATTACHEDONLY);
    pDI->Release();
    DEBUG_LOG("EnumerateDevices found %zu devices.", outList.size());
}

bool JoystickHelper::Init(const GUID& guid, HWND hwnd) {
    if (m_pJoy) m_pJoy->Unacquire(), m_pJoy->Release(), m_pJoy = nullptr;
    if (!m_pDI)
        if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
                IID_IDirectInput8, (VOID**)&m_pDI, nullptr))) {
            DEBUG_LOG("DirectInput8Create failed for device init.");
            return false;
        }

    if (FAILED(m_pDI->CreateDevice(guid, &m_pJoy, nullptr))) {
        DEBUG_LOG("CreateDevice failed.");
        return false;
    }
    if (FAILED(m_pJoy->SetDataFormat(&c_dfDIJoystick))) {
        DEBUG_LOG("SetDataFormat failed.");
        return false;
    }
    if (FAILED(m_pJoy->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
        DEBUG_LOG("SetCooperativeLevel failed.");
        return false;
    }
    m_guid = guid;
    DEBUG_LOG("JoystickHelper::Init succeeded (GUID set).");
    return true;
}

bool JoystickHelper::GetJoyState(DIJOYSTATE& js) {
    if (!m_pJoy) return false;
    if (FAILED(m_pJoy->Poll())) {
        m_pJoy->Acquire();
        if (FAILED(m_pJoy->Poll())) {
            DEBUG_LOG("Joystick Poll failed even after Acquire.");
            return false;
        }
    }
    if (FAILED(m_pJoy->GetDeviceState(sizeof(js), &js))) {
        DEBUG_LOG("GetDeviceState failed.");
        return false;
    }
    return true;
}

JoystickHelper::~JoystickHelper() {
    if (m_pJoy) m_pJoy->Unacquire(), m_pJoy->Release();
    if (m_pDI) m_pDI->Release();
}