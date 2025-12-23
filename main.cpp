#include <fstream>
#include <iostream>
#include <windows.h>

std::ofstream debug_log("debug_log.txt", std::ios::app);

BOOL CALLBACK BindDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    debug_log << "[BindDlgProc] Message: " << message << ", wParam: " << wParam << ", lParam: " << lParam << std::endl;
    switch (message)
    {
    case WM_INITDIALOG:
        debug_log << "[BindDlgProc] WM_INITDIALOG received." << std::endl;
        return TRUE;
    case WM_COMMAND:
        debug_log << "[BindDlgProc] WM_COMMAND received, wParam: " << wParam << std::endl;
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hwnd, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    debug_log << "[WndProc] Message: " << message << ", wParam: " << wParam << ", lParam: " << lParam << std::endl;
    switch (message)
    {
    case WM_CREATE:
        debug_log << "[WndProc] WM_CREATE received." << std::endl;
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            debug_log << "[WndProc] WM_PAINT received." << std::endl;
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_DESTROY:
        debug_log << "[WndProc] WM_DESTROY received." << std::endl;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    debug_log << "[WinMain] Application Start." << std::endl;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "AppClass", NULL };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, "Application Window", WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        debug_log << "[WinMain] Message Loop, Message: " << msg.message << std::endl;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    debug_log << "[WinMain] Application End." << std::endl;
    debug_log.close();

    return (int)msg.wParam;
}