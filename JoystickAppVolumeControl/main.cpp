#include <windows.h>
#include <commctrl.h>
#include "JoystickHelper.h"
#include "AudioSessionHelper.h"
#include "resource.h"
#include <vector>
#include <string>

#pragma comment(lib, "comctl32.lib")

HINSTANCE g_hInst;
HWND g_hListBox, g_hStartBtn, g_hJoystickLabel;
std::vector<ProcSessionInfo> g_sessions;
int g_selectedSession = -1;
bool g_running = false;

JoystickHelper joystick;
AudioSessionHelper audioHelper;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI PollingThreadProc(LPVOID);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TEXT("VolJoyWinClass");
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName, TEXT("Joystick App Volume Control"),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 480, 420,
        nullptr, nullptr, hInstance, nullptr);

    CreateWindow(TEXT("STATIC"), TEXT("Select an application to control:"),
        WS_CHILD | WS_VISIBLE,
        22, 15, 300, 18,
        hwnd, nullptr, hInstance, nullptr);

    g_hListBox = CreateWindowEx(
        WS_EX_CLIENTEDGE, WC_LISTBOX, nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        20, 40, 415, 200,
        hwnd, (HMENU)1001, hInstance, nullptr);

    HWND hRefresh = CreateWindow(TEXT("BUTTON"), TEXT("Refresh"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 250, 80, 30,
        hwnd, (HMENU)1002, hInstance, nullptr);

    g_hStartBtn = CreateWindow(TEXT("BUTTON"), TEXT("Start"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        120, 250, 80, 30,
        hwnd, (HMENU)1003, hInstance, nullptr);

    HWND hExitBtn = CreateWindow(TEXT("BUTTON"), TEXT("Exit"),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        220, 250, 80, 30,
        hwnd, (HMENU)1004, hInstance, nullptr);

    g_hJoystickLabel = CreateWindow(TEXT("STATIC"), TEXT("Joystick Axis Value: "),
        WS_CHILD | WS_VISIBLE,
        22, 300, 440, 28,
        hwnd, nullptr, hInstance, nullptr);

    // List audio sessions
    audioHelper.EnumerateSessions(g_sessions);
    for (const auto& sess : g_sessions) {
        std::wstring item = sess.processName + L" (PID: " + std::to_wstring(sess.pid) + L")";
        SendMessage(g_hListBox, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void RefreshSessionList() {
    SendMessage(g_hListBox, LB_RESETCONTENT, 0, 0);
    g_sessions.clear();
    audioHelper.EnumerateSessions(g_sessions);
    for (const auto& sess : g_sessions) {
        std::wstring item = sess.processName + L" (PID: " + std::to_wstring(sess.pid) + L")";
        SendMessage(g_hListBox, LB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HANDLE hThread = nullptr;

    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1002) {
            RefreshSessionList();
        } else if (LOWORD(wParam) == 1003) {
            int sel = (int)SendMessage(g_hListBox, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)g_sessions.size()) {
                MessageBox(hwnd, TEXT("Select a process first!"), TEXT("Info"), MB_OK);
                break;
            }
            g_selectedSession = sel;
            if (!joystick.Init(hwnd)) {
                MessageBox(hwnd, TEXT("Failed to initialize joystick!"), TEXT("Error"), MB_OK);
                break;
            }
            g_running = true;
            EnableWindow(g_hStartBtn, FALSE);
            hThread = CreateThread(nullptr, 0, PollingThreadProc, hwnd, 0, nullptr);
        } else if (LOWORD(wParam) == 1004) {
            g_running = false;
            PostMessage(hwnd, WM_CLOSE, 0, 0);
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

DWORD WINAPI PollingThreadProc(LPVOID param) {
    HWND hwnd = (HWND)param;
    ProcSessionInfo& sess = g_sessions[g_selectedSession];
    audioHelper.GetSimpleAudioVolume(sess);

    while (g_running) {
        LONG axis = 0;
        if (joystick.GetAxisValue(axis)) {
            float v = (axis + 32768.0f) / 65535.0f;
            audioHelper.SetSessionVolume(v, sess);

            wchar_t buf[128];
            swprintf_s(buf, 128, L"Joystick Axis Value: %ld   -->   App Volume: %.2f", axis, v);
            SetWindowText(g_hJoystickLabel, buf);
        }
        Sleep(40);
    }
    audioHelper.ReleaseSimpleAudioVolume(sess);
    return 0;
}