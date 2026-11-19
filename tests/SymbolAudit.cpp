/*
  SymbolAudit.cpp — dump / verify gardenia-core.dll exports (Windows).
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>

static std::wstring dll_path()
{
#ifdef _DEBUG
    const char* dir = GARDENIA_CORE_DLL_DIR_DEBUG;
#else
    const char* dir = GARDENIA_CORE_DLL_DIR_RELEASE;
#endif
    std::string p = std::string(dir) + "\\gardenia-core.dll";
    return std::wstring(p.begin(), p.end());
}

int main()
{
    const auto path = dll_path();
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (mod == nullptr)
        mod = LoadLibraryW(path.c_str());
    if (mod == nullptr)
    {
        std::cerr << "FAIL load dll for audit\n";
        return 1;
    }

    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(mod);
    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uint8_t*>(mod) + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0)
    {
        std::cerr << "FAIL no export directory\n";
        FreeLibrary(mod);
        return 1;
    }

    auto* exports = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(
        reinterpret_cast<uint8_t*>(mod) + dir.VirtualAddress);
    auto* names = reinterpret_cast<DWORD*>(reinterpret_cast<uint8_t*>(mod) + exports->AddressOfNames);

    int gardenia = 0;
    int mangled = 0;
    int other = 0;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i)
    {
        const char* name = reinterpret_cast<const char*>(reinterpret_cast<uint8_t*>(mod) + names[i]);
        const std::string s(name);
        if (s.rfind("gardenia_core_", 0) == 0)
        {
            ++gardenia;
            std::cout << "EXPORT " << name << "\n";
        }
        else if (! s.empty() && s[0] == '?')
        {
            ++mangled;
            std::cout << "MANGLED " << name << "\n";
        }
        else
        {
            ++other;
        }
    }

    FreeLibrary(mod);

    if (gardenia < 10)
    {
        std::cerr << "FAIL too few gardenia_core exports: " << gardenia << "\n";
        return 1;
    }
    if (mangled > 0)
    {
        std::cerr << "FAIL mangled C++ exports present: " << mangled << "\n";
        return 1;
    }

    std::cout << "PASS SymbolAudit gardenia_core_exports=" << gardenia
              << " other=" << other << "\n";
    return 0;
}
