#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <vector>
#include <string>
#include <atlbase.h>

struct ProcSessionInfo {
    std::wstring processName;
    DWORD pid; // system-wide (-1/0xFFFFFFFF)
    CComPtr<ISimpleAudioVolume> pVolume;
};

class AudioSessionHelper {
public:
    enum class TargetType { Process, System };
    AudioSessionHelper();
    bool EnumerateSessions(std::vector<ProcSessionInfo>& outList, bool includeSystem = false);
    bool GetSimpleAudioVolume(ProcSessionInfo& session, TargetType tgt);
    void ReleaseSimpleAudioVolume(ProcSessionInfo& session);
    void SetSessionVolume(float v, ProcSessionInfo& session, TargetType tgt);
private:
    CComPtr<IMMDeviceEnumerator> pEnum;
    CComPtr<IMMDevice> pDevice;
    CComPtr<IAudioSessionManager2> pMgr;
    CComPtr<IAudioEndpointVolume> pEndpointVol;
};