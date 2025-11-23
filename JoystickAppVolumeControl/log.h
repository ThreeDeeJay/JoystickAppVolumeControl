#pragma once
#include <windows.h>
#include <stdio.h>

void DebugLog(const char* fmt, ...);

#ifdef _DEBUG
#define DEBUG_LOG(fmt, ...) DebugLog(fmt, __VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...) /* nothing */
#endif