#include "log.h"
#include <stdarg.h>

void DebugLog(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
    // FILE* f = fopen("debuglog.txt", "a"); if (f) { fprintf(f, "%s\n", buffer); fclose(f); }
}