#include "ClassFactory.h"
#include "ShellFolder.h"
#include "CategoryProvider.h"

extern std::atomic<ULONG> g_dllRefCount;

namespace GameExplorer {

ClassFactory::ClassFactory(REFCLSID rclsid) : m_clsid(rclsid) {
    ++g_dllRefCount;
}

IFACEMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return ++m_refCount;
}

IFACEMETHODIMP_(ULONG) ClassFactory::Release() {
    const ULONG count = --m_refCount;
    if (count == 0) {
        --g_dllRefCount;
        delete this;
    }
    return count;
}

IFACEMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (ppv == nullptr) return E_POINTER;
    *ppv = nullptr;

    if (pUnkOuter != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }

    if (IsEqualCLSID(m_clsid, CLSID_GameExplorer)) {
        ShellFolder* pFolder = new (std::nothrow) ShellFolder();
        if (pFolder == nullptr) {
            return E_OUTOFMEMORY;
        }

        const HRESULT hr = pFolder->QueryInterface(riid, ppv);
        pFolder->Release();
        return hr;
    }

    if (IsEqualCLSID(m_clsid, CLSID_GameCategorizer)) {
        GameCategorizer* pCat = new (std::nothrow) GameCategorizer();
        if (pCat == nullptr) {
            return E_OUTOFMEMORY;
        }

        const HRESULT hr = pCat->QueryInterface(riid, ppv);
        pCat->Release();
        return hr;
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

IFACEMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        ++g_dllRefCount;
    } else {
        --g_dllRefCount;
    }
    return S_OK;
}

} // namespace GameExplorer
