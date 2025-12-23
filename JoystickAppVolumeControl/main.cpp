#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "JoystickHelper.h"
#include "AudioSessionHelper.h"
#include "resource.h"
#include "log.h"

#pragma comment(lib, "comctl32.lib")

#define MAX_BINDINGS 32

// --- Struct for persisting (and in-memory) a full binding ---
struct AxisBinding {
    int deviceIdx;
    int axisIdx;
    int axisMin, axisMax;
    float volMin, volMax;
    int sessionIdx;
    GUID deviceGuid;
};

HINSTANCE g_hInst;
HWND g_hBindingsList, g_hAddBtn, g_hDeleteBtn, g_hStartBtn, g_hStopBtn, g_hSaveBtn, g_hLoadBtn, g_hJoystickLabel;
bool g_running = false;

std::vector<DInputDeviceInfo> g_devices;
std::vector<std::wstring> g_axes = {
    L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"Slider0", L"Slider1"
};
std::vector<ProcSessionInfo> g_sessions;
std::vector<AxisBinding> g_bindings;

JoystickHelper joystick;
AudioSessionHelper audioHelper;

// --- UI/Binding Editing ---
void RefreshSessionList();
void RefreshDeviceList();
void RefreshBindingsList(HWND hBindingList);

INT_PTR CALLBACK BindDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void SaveBindingsToFile(const wchar_t* filename);
bool LoadBindingsFromFile(const wchar_t* filename);

// --- Axis Value Mapping ---
bool GetSelectedAxisValue(const DIJOYSTATE& js, int axisIdx, LONG& val) {
    switch(axisIdx) {
    case 0: val = js.lX; break;
    case 1: val = js.lY; break;
    case 2: val = js.lZ; break;
    case 3: val = js.lRx; break;
    case 4: val = js.lRy; break;
    case 5: val = js.lRz; break;
    case 6: val = js.rglSlider[0]; break;
    case 7: val = js.rglSlider[1]; break;
    default: return false;
    }
    return true;
}

template<typename T> T Clamp(T val, T min, T max) { return (val < min) ? min : (val > max ? max : val); }

DWORD WINAPI PollingThreadProc(LPVOID param) {
    while (g_running) {
        for (const AxisBinding& ab : g_bindings) {
            if (ab.deviceIdx < 0 || ab.deviceIdx >= (int)g_devices.size() ||
                ab.sessionIdx < 0 || ab.sessionIdx >= (int)g_sessions.size())
                continue;

            // Each binding calls its own DirectInput device instance for simplest polling.
            JoystickHelper localJoy;
            if (!localJoy.Init(g_devices[ab.deviceIdx].guid, nullptr))
                continue;
            DIJOYSTATE js = {};
            if (!localJoy.GetJoyState(js)) continue;

            LONG axisRaw = 0;
            if (GetSelectedAxisValue(js, ab.axisIdx, axisRaw)) {
                double t = (axisRaw-ab.axisMin) / double(ab.axisMax-ab.axisMin);
                t = Clamp(t, 0.0, 1.0);
                double mapped = ab.volMin + t * (ab.volMax-ab.volMin);
                float minV = (ab.volMin < ab.volMax) ? ab.volMin : ab.volMax;
                float maxV = (ab.volMin > ab.volMax) ? ab.volMin : ab.volMax;
                float v = Clamp((float)mapped, minV, maxV);
                ProcSessionInfo& sess = g_sessions[ab.sessionIdx];
                AudioSessionHelper::TargetType tgtType = sess.pid == 0xFFFFFFFF ? AudioSessionHelper::TargetType::System : AudioSessionHelper::TargetType::Process;
                audioHelper.GetSimpleAudioVolume(sess, tgtType);
                audioHelper.SetSessionVolume(v, sess, tgtType);
                audioHelper.ReleaseSimpleAudioVolume(sess);

                wchar_t buf[256];
                swprintf_s(buf, 256, L"[%s:%s|%ld]->%s: V=%.2f [%.2f-%.2f]",
                    g_devices[ab.deviceIdx].name.c_str(),
                    g_axes[ab.axisIdx].c_str(), axisRaw, g_sessions[ab.sessionIdx].processName.c_str(),
                    v, ab.volMin, ab.volMax);
                SetWindowText(g_hJoystickLabel, buf);
            }
        }
        Sleep(40);
    }
    return 0;
}

// --- File Save/Load as JSON ---
void SaveBindingsToFile(const wchar_t* filename) {
    std::wofstream out(filename);
    if (!out) { MessageBox(nullptr,L"Could not open for save!",L"Error",MB_ICONERROR); return; }
    out << L"{\"bindings\":[";
    for (size_t i=0;i<g_bindings.size();++i) {
        const AxisBinding& ab = g_bindings[i];
        out << L"{\"deviceIdx\":" << ab.deviceIdx
            << L",\"axisIdx\":" << ab.axisIdx
            << L",\"axisMin\":" << ab.axisMin
            << L",\"axisMax\":" << ab.axisMax
            << L",\"volMin\":" << ab.volMin
            << L",\"volMax\":" << ab.volMax
            << L",\"sessionIdx\":" << ab.sessionIdx
            << L"}";
        if (i+1<g_bindings.size()) out << L",";
    }
    out << L"]}";
    out.close();
    MessageBox(nullptr, L"Bindings saved.", L"Save", 0);
}

bool LoadBindingsFromFile(const wchar_t* filename) {
    std::wifstream in(filename);
    if (!in) { MessageBox(nullptr,L"Could not open file!",L"Error",MB_ICONERROR); return false; }
    std::wstring content((std::istreambuf_iterator<wchar_t>(in)),std::istreambuf_iterator<wchar_t>());
    in.close();
    size_t pos=0;
    g_bindings.clear();
    while ((pos=content.find(L"{\"deviceIdx\":",pos))!=std::wstring::npos) {
        AxisBinding ab = {};
        size_t s=pos+12;
        swscanf(content.c_str()+s,L"%d,\"axisIdx\":%d,\"axisMin\":%d,\"axisMax\":%d,\"volMin\":%f,\"volMax\":%f,\"sessionIdx\":%d",
                &ab.deviceIdx,&ab.axisIdx,&ab.axisMin,&ab.axisMax,&ab.volMin,&ab.volMax,&ab.sessionIdx);
        g_bindings.push_back(ab);
        pos+=20;
    }
    return true;
}

// --- List controls and button helpers ---
void RefreshBindingsList(HWND hBindingList) {
    SendMessage(hBindingList, LB_RESETCONTENT, 0, 0);
    for (auto& ab : g_bindings) {
        wchar_t buf[256];
        swprintf(buf,256,L"%s, %s [%d-%d] \x2192 %s [%.2f-%.2f]",
            g_devices.size() > ab.deviceIdx ? g_devices[ab.deviceIdx].name.c_str() : L"(None)",
            g_axes.size() > ab.axisIdx ? g_axes[ab.axisIdx].c_str() : L"(Axis)",
            ab.axisMin, ab.axisMax,
            g_sessions.size() > ab.sessionIdx ? g_sessions[ab.sessionIdx].processName.c_str() : L"(App)",
            ab.volMin, ab.volMax);
        SendMessage(hBindingList, LB_ADDSTRING,0,(LPARAM)buf);
    }
}

// --- Standard combo-fill helpers and parsing (from previous impl) ---
void FillCombo(HWND hCombo, const std::vector<std::wstring>& items) {
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (auto& s : items)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}
void FillComboDevs(HWND hCombo) {
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (auto& d : g_devices)
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)d.name.c_str());
    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}
void FillComboSessions(HWND hCombo) {
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (auto& s : g_sessions) {
        std::wstring item = s.processName + L" (PID: " +
            (s.pid == 0xFFFFFFFF ? L"System" : std::to_wstring(s.pid)) + L")";
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    SendMessage(hCombo, CB_SETCURSEL, 0, 0);
}
void RefreshSessionList() {
    g_sessions.clear();
    audioHelper.EnumerateSessions(g_sessions, true);
}
void RefreshDeviceList() {
    g_devices.clear();
    JoystickHelper::EnumerateDevices(g_devices);
}

// --- Bind Dialog and parsing helpers ---
static bool ParseInt(HWND hEdit, int& outVal) {
    wchar_t buf[32] = {0};
    GetWindowTextW(hEdit, buf, 31);
    wchar_t* endp = nullptr;
    long val = wcstol(buf, &endp, 10);
    if (endp == buf || *endp != 0) return false;
    outVal = (int)val; return true;
}
static bool ParseFloat(HWND hEdit, float& outVal) {
    wchar_t buf[32] = {0};
    GetWindowTextW(hEdit, buf, 31);
    wchar_t* endp = nullptr;
    float val = (float)wcstod(buf, &endp);
    if (endp == buf || *endp != 0) return false;
    outVal = val; return true;
}
static void SetInt(HWND hEdit, int val) {
    wchar_t buf[32]; wsprintfW(buf, L"%d", val);
    SetWindowTextW(hEdit, buf);
}
static void SetFloat(HWND hEdit, float val) {
    wchar_t buf[32]; swprintf(buf, 32, L"%f", val);
    SetWindowTextW(hEdit, buf);
}

INT_PTR CALLBACK BindDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hDevCombo, hAxisCombo, hSessCombo, hAxisMin, hAxisMax, hVolMin, hVolMax;
    AxisBinding* ab = (AxisBinding*)lParam;
    switch (msg) {
    case WM_INITDIALOG: {
        hDevCombo = GetDlgItem(hDlg, IDC_BIND_DEVICE);
        hAxisCombo = GetDlgItem(hDlg, IDC_BIND_AXIS);
        hSessCombo = GetDlgItem(hDlg, IDC_BIND_SESSION);
        hAxisMin = GetDlgItem(hDlg, IDC_AXIS_MIN);
        hAxisMax = GetDlgItem(hDlg, IDC_AXIS_MAX);
        hVolMin = GetDlgItem(hDlg, IDC_VOL_MIN);
        hVolMax = GetDlgItem(hDlg, IDC_VOL_MAX);

        RefreshDeviceList();
        FillComboDevs(hDevCombo);
        FillCombo(hAxisCombo, g_axes);
        RefreshSessionList();
        FillComboSessions(hSessCombo);

        SetInt(hAxisMin, 0);
        SetInt(hAxisMax, 65535);
        SetFloat(hVolMin, 0.0f);
        SetFloat(hVolMax, 0.1f);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            ab->deviceIdx = (int)SendMessage(hDevCombo, CB_GETCURSEL, 0, 0);
            ab->axisIdx = (int)SendMessage(hAxisCombo, CB_GETCURSEL, 0, 0);
            ab->sessionIdx = (int)SendMessage(hSessCombo, CB_GETCURSEL, 0, 0);

            int axisMin, axisMax; float volMin, volMax;
            if (!ParseInt(hAxisMin, axisMin)) {
                MessageBox(hDlg, L"Invalid axis min value.", L"Error", MB_ICONERROR);
                SetFocus(hAxisMin);
                break;
            }
            if (!ParseInt(hAxisMax, axisMax)) {
                MessageBox(hDlg, L"Invalid axis max value.", L"Error", MB_ICONERROR);
                SetFocus(hAxisMax);
                break;
            }
            if (axisMin == axisMax) {
                MessageBox(hDlg, L"Axis min and max must differ.", L"Error", MB_ICONERROR);
                SetFocus(hAxisMax);
                break;
            }
            if (!ParseFloat(hVolMin, volMin)) {
                MessageBox(hDlg, L"Invalid volume min value.", L"Error", MB_ICONERROR);
                SetFocus(hVolMin);
                break;
            }
            if (!ParseFloat(hVolMax, volMax)) {
                MessageBox(hDlg, L"Invalid volume max value.", L"Error", MB_ICONERROR);
                SetFocus(hVolMax);
                break;
            }
            if (volMin == volMax) {
                MessageBox(hDlg, L"Volume min and max must differ.", L"Error", MB_ICONERROR);
                SetFocus(hVolMax);
                break;
            }
            ab->axisMin=axisMin; ab->axisMax=axisMax; ab->volMin=volMin; ab->volMax=volMax;
            ab->deviceGuid = g_devices.size()>ab->deviceIdx ? g_devices[ab->deviceIdx].guid : GUID_NULL;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return 0;
}

// --- MAIN WINDOW / UI ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HANDLE hThread = nullptr;
    switch (msg) {
    case WM_CREATE: {
        RefreshDeviceList();
        RefreshSessionList();

        // List box for all bindings
        g_hBindingsList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, nullptr,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            20, 40, 420, 120, hwnd, (HMENU)IDC_BINDINGS_LIST, g_hInst, nullptr);
        g_hAddBtn = CreateWindow(TEXT("BUTTON"), TEXT("Add Binding"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 170, 100, 28, hwnd, (HMENU)IDC_ADD_BINDING, g_hInst, nullptr);
        g_hDeleteBtn = CreateWindow(TEXT("BUTTON"), TEXT("Remove Selected"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 130, 170, 120, 28, hwnd, (HMENU)IDC_DELETE_BINDING, g_hInst, nullptr);
        g_hStartBtn = CreateWindow(TEXT("BUTTON"), TEXT("Start"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 210, 80, 30, hwnd, (HMENU)IDC_START, g_hInst, nullptr);
        g_hStopBtn = CreateWindow(TEXT("BUTTON"), TEXT("Stop"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 210, 80, 30, hwnd, (HMENU)IDC_EXIT, g_hInst, nullptr);
        g_hSaveBtn = CreateWindow(TEXT("BUTTON"), TEXT("Save Bindings"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 170, 90, 28, hwnd, (HMENU)IDC_SAVE_BINDINGS, g_hInst, nullptr);
        g_hLoadBtn = CreateWindow(TEXT("BUTTON"), TEXT("Load Bindings"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 350, 170, 90, 28, hwnd, (HMENU)IDC_LOAD_BINDINGS, g_hInst, nullptr);
        g_hJoystickLabel = CreateWindow(TEXT("STATIC"), TEXT("Status: Ready"),
            WS_CHILD | WS_VISIBLE, 20, 260, 420, 24, hwnd, nullptr, g_hInst, nullptr);
        RefreshBindingsList(g_hBindingsList);
        break;
    }
    case WM_COMMAND:
        switch(LOWORD(wParam)) {
        case IDC_ADD_BINDING: {
                AxisBinding ab = {};
                if (DialogBoxParam(g_hInst, MAKEINTRESOURCE(IDD_BIND_DIALOG), hwnd, BindDlgProc, (LPARAM)&ab)==IDOK) {
                    g_bindings.push_back(ab);
                    RefreshBindingsList(g_hBindingsList);
                }
            } break;
        case IDC_DELETE_BINDING: {
            int idx = (int)SendMessage(g_hBindingsList, LB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx < (int)g_bindings.size()) {
                g_bindings.erase(g_bindings.begin()+idx);
                RefreshBindingsList(g_hBindingsList);
            }
            } break;
        case IDC_START:
            if (!g_running && !g_bindings.empty()) {
                g_running = true;
                hThread = CreateThread(nullptr, 0, PollingThreadProc, hwnd, 0, nullptr);
                SetWindowText(g_hJoystickLabel, L"Polling...");
            }
            break;
        case IDC_EXIT:
            g_running = false;
            if (hThread) WaitForSingleObject(hThread, 1000);
            SetWindowText(g_hJoystickLabel, L"Stopped.");
            break;
        case IDC_SAVE_BINDINGS:
            SaveBindingsToFile(L"bindings.json");
            break;
        case IDC_LOAD_BINDINGS:
            LoadBindingsFromFile(L"bindings.json");
            RefreshBindingsList(g_hBindingsList);
            break;
        }
        break;
    case WM_CLOSE:
        g_running = false;
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInstance;
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TEXT("VolJoyWinClass");
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, TEXT("Joystick App Volume Control"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 320,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
