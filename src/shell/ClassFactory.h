#pragma once

#include <windows.h>
#include <unknwn.h>
#include <atomic>
#include "GameExplorerCore/Guid.h"

namespace GameExplorer {

class ClassFactory final : public IClassFactory {
public:
    explicit ClassFactory(REFCLSID rclsid = CLSID_GameExplorer);
    ~ClassFactory() = default;

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override;
    IFACEMETHODIMP LockServer(BOOL fLock) override;

private:
    std::atomic<ULONG> m_refCount{ 1 };
    CLSID m_clsid{ CLSID_GameExplorer };
};

} // namespace GameExplorer
