#pragma once

#include <string>

namespace platform
{
  // Opens a native file-save dialog. Returns true if a path was selected.
  // filter example (Windows): L"Synaptic Brain (*.sbrain)\0*.sbrain\0All Files (*.*)\0*.*\0\0"
  bool GetSaveFilePath(std::string& outPathUtf8, const wchar_t* filterW, const wchar_t* defaultFileNameW = L"");

  // Opens a native file-open dialog. Returns true if a path was selected.
  bool GetOpenFilePath(std::string& outPathUtf8, const wchar_t* filterW);
}

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <string>

namespace platform
{
  inline static std::string WideToUtf8(const std::wstring& s)
  {
    if (s.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int) s.size(), nullptr, 0, nullptr, nullptr);
    std::string out; out.resize(len);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int) s.size(), &out[0], len, nullptr, nullptr);
    return out;
  }

  inline bool GetSaveFilePath(std::string& outPathUtf8, const wchar_t* filterW, const wchar_t* defaultFileNameW)
  {
    wchar_t fileBuf[MAX_PATH] = L"";
    if (defaultFileNameW) lstrcpynW(fileBuf, defaultFileNameW, MAX_PATH);
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterW;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"sbrain";
    if (GetSaveFileNameW(&ofn))
    {
      outPathUtf8 = WideToUtf8(ofn.lpstrFile);
      return true;
    }
    return false;
  }

  inline bool GetOpenFilePath(std::string& outPathUtf8, const wchar_t* filterW)
  {
    wchar_t fileBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterW;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn))
    {
      outPathUtf8 = WideToUtf8(ofn.lpstrFile);
      return true;
    }
    return false;
  }
}
#else
namespace platform
{
  inline bool GetSaveFilePath(std::string& outPathUtf8, const wchar_t* filterW, const wchar_t* defaultFileNameW)
  {
    (void) outPathUtf8; (void) filterW; (void) defaultFileNameW; return false;
  }
  inline bool GetOpenFilePath(std::string& outPathUtf8, const wchar_t* filterW)
  {
    (void) outPathUtf8; (void) filterW; return false;
  }
}
#endif


