#include "AudioSessionHelper.h"
#include <psapi.h>

AudioSessionHelper::AudioSessionHelper() {
    CoInitialize(NULL);
    pEnum.CoCreateInstance(__uuidof(MMDeviceEnumerator));
    if (pEnum) pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    if (pDevice) pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pMgr);
}

bool AudioSessionHelper::EnumerateSessions(std::vector<ProcSessionInfo>& outList) {
    outList.clear();
    if (!pMgr) return false;
    CComPtr<IAudioSessionEnumerator> pEnumSess;
    pMgr->GetSessionEnumerator(&pEnumSess);
    int count = 0;
    if (!pEnumSess || FAILED(pEnumSess->GetCount(&count))) return false;

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
    }
    return true;
}

bool AudioSessionHelper::GetSimpleAudioVolume(ProcSessionInfo& session) {
    if (!pMgr) return false;
    CComPtr<IAudioSessionEnumerator> pEnumSess;
    pMgr->GetSessionEnumerator(&pEnumSess);
    int count = 0;
    if (!pEnumSess || FAILED(pEnumSess->GetCount(&count))) return false;

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
                return true;
            }
        }
    }
    return false;
}
void AudioSessionHelper::ReleaseSimpleAudioVolume(ProcSessionInfo& session) {
    session.pVolume.Release();
}

void AudioSessionHelper::SetSessionVolume(float v, ProcSessionInfo& session) {
    if (session.pVolume)
        session.pVolume->SetMasterVolume(v, NULL);
}