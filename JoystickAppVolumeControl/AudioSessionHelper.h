#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <vector>
#include <string>
#include <atlbase.h> // For CComPtr

struct ProcSessionInfo {
    std::wstring processName;
    DWORD pid;
    CComPtr<ISimpleAudioVolume> pVolume;
};

class AudioSessionHelper {
    CComPtr<IMMDeviceEnumerator> pEnum;
    CComPtr<IMMDevice> pDevice;
    CComPtr<IAudioSessionManager2> pMgr;
public:
    AudioSessionHelper();
    bool EnumerateSessions(std::vector<ProcSessionInfo>& outList);
    bool GetSimpleAudioVolume(ProcSessionInfo& session);
    void ReleaseSimpleAudioVolume(ProcSessionInfo& session);
    void SetSessionVolume(float v, ProcSessionInfo& session);
};