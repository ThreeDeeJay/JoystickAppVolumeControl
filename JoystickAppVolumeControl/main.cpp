#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "JoystickHelper.h"
#include "AudioSessionHelper.h"
#include "resource.h"
#include "log.h"

#pragma comment(lib, "comctl32.lib")

#define APP_MENU_BIND   2001

HINSTANCE g_hInst;
HWND g_hListBox, g_hStartBtn, g_hJoystickLabel;

int g_selectedSession = -1;
int g_selectedDeviceIdx = -1;
int g_selectedAxisIdx = 2; // Default: Z axis
bool g_running = false;

int g_axisMin = -32768, g_axisMax = 32767;
float g_volMin = 0.0f, g_volMax = 1.0f;

JoystickHelper joystick;
AudioSessionHelper audioHelper;
std::vector<DInputDeviceInfo> g_devices;
std::vector<std::wstring> g_axes = {
    L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"Slider0", L"Slider1"
};
std::vector<ProcSessionInfo> g_sessions;

void RefreshSessionList(HWND hwnd = nullptr);
void RefreshDeviceList();
INT_PTR CALLBACK BindDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

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

// Clamp
template<typename T> T Clamp(T val, T min, T max) { return (val < min) ? min : (val > max ? max : val); }

DWORD WINAPI PollingThreadProc(LPVOID param) {
    HWND hwnd = (HWND)param;
    ProcSessionInfo& sess = g_sessions[g_selectedSession];
    AudioSessionHelper::TargetType tgtType = sess.pid == 0xFFFFFFFF ? AudioSessionHelper::TargetType::System : AudioSessionHelper::TargetType::Process;
    audioHelper.GetSimpleAudioVolume(sess, tgtType);

    while (g_running) {
        DIJOYSTATE js = {};
        if (joystick.GetJoyState(js)) {
            LONG axisRaw = 0;
            if (GetSelectedAxisValue(js, g_selectedAxisIdx, axisRaw)) {
                // Robust user-mapping:
                double a_min = double(g_axisMin), a_max = double(g_axisMax);
                double v_min = double(g_volMin), v_max = double(g_volMax);
                double input = double(axisRaw);
                if (a_min == a_max) { a_max = a_min + 1.0; } // avoid division by zero

                double t = (input - a_min) / (a_max - a_min);
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;

                double mapped = v_min + t * (v_max - v_min);
                float v = Clamp((float)mapped, (float)Clamp(v_min, 0.0, 1.0), (float)Clamp(v_max, 0.0, 1.0));
                audioHelper.SetSessionVolume(v, sess, tgtType);

                wchar_t buf[256];
                swprintf_s(buf, 256, L"Dev: %s Axis: %s|%ld\nV=%.2f [%.2f-%.2f], Axis in [%d-%d]",
                    g_devices[g_selectedDeviceIdx].name.c_str(), g_axes[g_selectedAxisIdx].c_str(), axisRaw,
                    v, g_volMin, g_volMax, g_axisMin, g_axisMax);
                SetWindowText(g_hJoystickLabel, buf);
                DEBUG_LOG("AxisValue=%ld mapped=%.3f", axisRaw, v);
            } else {
                SetWindowText(g_hJoystickLabel, L"Selected axis not available!");
                DEBUG_LOG("Axis not available. DeviceIdx=%d AxisIdx=%d", g_selectedDeviceIdx, g_selectedAxisIdx);
            }
        } else {
            SetWindowText(g_hJoystickLabel, L"Joystick No Data");
            DEBUG_LOG("Joystick poll failed");
        }
        Sleep(40);
    }
    audioHelper.ReleaseSimpleAudioVolume(sess);
    return 0;
}

void RefreshSessionList(HWND hwnd) {
    SendMessage(g_hListBox, LB_RESETCONTENT, 0, 0);
    g_sessions.clear();
    audioHelper.EnumerateSessions(g_sessions, true);
    for (size_t i = 0; i < g_sessions.size(); ++i) {
        std::wstring item = g_sessions[i].processName + L" (PID: " +
            (g_sessions[i].pid == 0xFFFFFFFF ? L"System" : std::to_wstring(g_sessions[i].pid)) + L")";
        SendMessage(g_hListBox, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

void RefreshDeviceList() {
    g_devices.clear();
    JoystickHelper::EnumerateDevices(g_devices);
    DEBUG_LOG("Found %zu DirectInput devices", g_devices.size());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HANDLE hThread = nullptr;
    switch (msg) {
    case WM_CREATE: {
        HMENU hMenubar = CreateMenu();
        HMENU hApp = CreateMenu();
        AppendMenu(hApp, MF_STRING, APP_MENU_BIND, L"&Bind...");
        AppendMenu(hMenubar, MF_POPUP, (UINT_PTR)hApp, L"&Configure");
        SetMenu(hwnd, hMenubar);

        CreateWindow(TEXT("STATIC"), TEXT("Select an application to control:"),
            WS_CHILD | WS_VISIBLE, 22, 15, 300, 18, hwnd, nullptr, g_hInst, nullptr);
        g_hListBox = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTBOX, nullptr,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            20, 40, 415, 200, hwnd, (HMENU)1001, g_hInst, nullptr);
        HWND hRefresh = CreateWindow(TEXT("BUTTON"), TEXT("Refresh"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 250, 80, 30, hwnd, (HMENU)1002, g_hInst, nullptr);
        g_hStartBtn = CreateWindow(TEXT("BUTTON"), TEXT("Start"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 120, 250, 80, 30, hwnd, (HMENU)1003, g_hInst, nullptr);
        HWND hExitBtn = CreateWindow(TEXT("BUTTON"), TEXT("Exit"),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220, 250, 80, 30, hwnd, (HMENU)1004, g_hInst, nullptr);
        g_hJoystickLabel = CreateWindow(TEXT("STATIC"), TEXT("Joystick Axis Value: "),
            WS_CHILD | WS_VISIBLE, 22, 300, 440, 28, hwnd, nullptr, g_hInst, nullptr);

        RefreshDeviceList();
        RefreshSessionList(hwnd);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1002) {
            RefreshSessionList(hwnd);
        } else if (LOWORD(wParam) == 1003) { // Start
            if (g_selectedDeviceIdx < 0 || g_selectedDeviceIdx >= (int)g_devices.size()) {
                MessageBox(hwnd, TEXT("Bind a valid joystick device first!"), TEXT("Error"), MB_OK);
                DEBUG_LOG("Invalid joystick selected: idx=%d", g_selectedDeviceIdx);
                break;
            }
            if (g_selectedAxisIdx < 0 || g_selectedAxisIdx >= (int)g_axes.size()) {
                MessageBox(hwnd, TEXT("Bind a valid axis first!"), TEXT("Error"), MB_OK);
                DEBUG_LOG("Invalid axis selected: idx=%d", g_selectedAxisIdx);
                break;
            }
            int sel = (int)SendMessage(g_hListBox, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)g_sessions.size()) {
                MessageBox(hwnd, TEXT("Select a session first!"), TEXT("Info"), MB_OK);
                DEBUG_LOG("No audio session/app selected for binding");
                break;
            }
            g_selectedSession = sel;
            if (!joystick.Init(g_devices[g_selectedDeviceIdx].guid, hwnd)) {
                MessageBox(hwnd, TEXT("Failed to initialize joystick!"), TEXT("Error"), MB_OK);
                DEBUG_LOG("Joystick initialization failed for idx=%d", g_selectedDeviceIdx);
                break;
            }
            g_running = true;
            EnableWindow(g_hStartBtn, FALSE);
            hThread = CreateThread(nullptr, 0, PollingThreadProc, hwnd, 0, nullptr);
            DEBUG_LOG("Started polling thread for device=%d axis=%d session=%d", g_selectedDeviceIdx, g_selectedAxisIdx, g_selectedSession);
        } else if (LOWORD(wParam) == 1004) {
            g_running = false;
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            DEBUG_LOG("Exit requested.");
        } else if (LOWORD(wParam) == APP_MENU_BIND) {
            DialogBox(g_hInst, MAKEINTRESOURCE(IDD_BIND_DIALOG), hwnd, BindDlgProc);
        }
        break;
    case WM_CLOSE:
        g_running = false;
        if (hThread) WaitForSingleObject(hThread, 1000);
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Simple combo box helpers
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
    SendMessage(hCombo, CB_SETCURSEL, g_selectedDeviceIdx >= 0 ? g_selectedDeviceIdx : 0, 0);
}
void FillComboSessions(HWND hCombo) {
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    for (auto& s : g_sessions) {
        std::wstring item = s.processName + L" (PID: " +
            (s.pid == 0xFFFFFFFF ? L"System" : std::to_wstring(s.pid)) + L")";
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    SendMessage(hCombo, CB_SETCURSEL, g_selectedSession >= 0 ? g_selectedSession : 0, 0);
}

// Robust float/int parsing
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
        RefreshSessionList(nullptr);
        FillComboSessions(hSessCombo);

        SendMessage(hDevCombo, CB_SETCURSEL, g_selectedDeviceIdx >= 0 ? g_selectedDeviceIdx : 0, 0);
        SendMessage(hAxisCombo, CB_SETCURSEL, g_selectedAxisIdx >= 0 ? g_selectedAxisIdx : 2, 0);
        SendMessage(hSessCombo, CB_SETCURSEL, g_selectedSession >= 0 ? g_selectedSession : 0, 0);

        SetInt(hAxisMin, g_axisMin);
        SetInt(hAxisMax, g_axisMax);
        SetFloat(hVolMin, g_volMin);
        SetFloat(hVolMax, g_volMax);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            int deviceIdx = (int)SendMessage(hDevCombo, CB_GETCURSEL, 0, 0);
            int axisIdx = (int)SendMessage(hAxisCombo, CB_GETCURSEL, 0, 0);
            int sessionIdx = (int)SendMessage(hSessCombo, CB_GETCURSEL, 0, 0);
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

            g_selectedDeviceIdx = deviceIdx;
            g_selectedAxisIdx = axisIdx;
            g_selectedSession = sessionIdx;
            g_axisMin = axisMin;
            g_axisMax = axisMax;
            g_volMin = volMin;
            g_volMax = volMax;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return 0;
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
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 420,
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