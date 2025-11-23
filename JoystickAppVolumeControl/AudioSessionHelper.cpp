#include "AudioSessionHelper.h"
#include <psapi.h>
#include "log.h"

AudioSessionHelper::AudioSessionHelper() {
    CoInitialize(NULL);
    pEnum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
    if (pEnum) pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (pDevice) {
        HRESULT hr1 = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pMgr);
        HRESULT hr2 = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&pEndpointVol);
        DEBUG_LOG("AudioSessionHelper: activation hr1=0x%08X hr2=0x%08X", hr1, hr2);
    }
}

bool AudioSessionHelper::EnumerateSessions(std::vector<ProcSessionInfo>& outList, bool includeSystem) {
    outList.clear();
    if (includeSystem) {
        ProcSessionInfo sys;
        sys.processName = L"System Volume";
        sys.pid = 0xFFFFFFFF;
        outList.push_back(sys);
    }
    if (!pMgr) return includeSystem;
    CComPtr<IAudioSessionEnumerator> pEnumSess;
    HRESULT hr = pMgr->GetSessionEnumerator(&pEnumSess);
    if (!pEnumSess || FAILED(hr)) {
        DEBUG_LOG("GetSessionEnumerator failed: hr=0x%08X", hr);
        return includeSystem;
    }
    int count = 0;
    pEnumSess->GetCount(&count);
    DEBUG_LOG("Found %d audio sessions", count);
    for (int i = 0; i < count; ++i) {
        CComPtr<IAudioSessionControl> pCtrl;
        if (FAILED(pEnumSess->GetSession(i, &pCtrl))) continue;
        CComPtr<IAudioSessionControl2> pCtrl2;
        if (FAILED(pCtrl->QueryInterface(&pCtrl2))) continue;
        DWORD pid = 0;
        if (FAILED(pCtrl2->GetProcessId(&pid)) || pid == 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        wchar_t name[MAX_PATH] = L"";
        if (h) {
            GetModuleBaseNameW(h, NULL, name, MAX_PATH);
            CloseHandle(h);
        }
        if (wcslen(name) == 0)
            wcscpy_s(name, L"Unknown");
        ProcSessionInfo psi;
        psi.processName = name;
        psi.pid = pid;
        outList.push_back(psi);
        DEBUG_LOG("Audio session: %ws (pid=%u)", name, pid);
    }
    return true;
}

bool AudioSessionHelper::GetSimpleAudioVolume(ProcSessionInfo& session, TargetType tgt) {
    if (tgt == TargetType::System) {
        return pEndpointVol != nullptr;
    }
    if (!pMgr) return false;
    CComPtr<IAudioSessionEnumerator> pEnumSess;
    HRESULT hr = pMgr->GetSessionEnumerator(&pEnumSess);
    if (!pEnumSess || FAILED(hr)) {
        DEBUG_LOG("GetSessionEnumerator failed: hr=0x%08X", hr);
        return false;
    }
    int count = 0;
    pEnumSess->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        CComPtr<IAudioSessionControl> pCtrl;
        if (FAILED(pEnumSess->GetSession(i, &pCtrl))) continue;
        CComPtr<IAudioSessionControl2> pCtrl2;
        if (FAILED(pCtrl->QueryInterface(&pCtrl2))) continue;
        DWORD pid = 0;
        if (FAILED(pCtrl2->GetProcessId(&pid)) || pid == 0) continue;
        if (pid == session.pid) {
            CComPtr<ISimpleAudioVolume> pVol;
            if (SUCCEEDED(pCtrl->QueryInterface(&pVol))) {
                session.pVolume = pVol;
                DEBUG_LOG("Found ISimpleAudioVolume for PID %u", pid);
                return true;
            }
        }
    }
    DEBUG_LOG("Did not find ISimpleAudioVolume for PID %u", session.pid);
    return false;
}
void AudioSessionHelper::ReleaseSimpleAudioVolume(ProcSessionInfo& session) {
    session.pVolume.Release();
}

void AudioSessionHelper::SetSessionVolume(float v, ProcSessionInfo& session, TargetType tgt) {
    if (tgt == TargetType::System && pEndpointVol)
        pEndpointVol->SetMasterVolumeLevelScalar(v, NULL);
    else if (session.pVolume)
        session.pVolume->SetMasterVolume(v, NULL);
}