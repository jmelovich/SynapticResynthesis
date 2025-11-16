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
#elif defined(__APPLE__)
#include <string>
#include <vector>
#include <codecvt>
#include <locale>

namespace platform
{
  // Helper to convert wchar_t* to UTF-8 string
  inline static std::string WideToUtf8(const wchar_t* s)
  {
    if (!s || !*s) return std::string();
    std::wstring ws(s);
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(ws);
  }

  // Parse Windows-style filter string and extract file extensions
  // Input format: L"Description\0*.ext1;*.ext2\0...\0\0"
  inline static std::vector<std::string> ParseFilterExtensions(const wchar_t* filterW)
  {
    std::vector<std::string> extensions;
    if (!filterW) return extensions;

    std::string filter = WideToUtf8(filterW);
    size_t pos = 0;
    bool inPattern = false;
    
    // Parse null-separated string pairs
    while (pos < filter.size())
    {
      size_t nullPos = filter.find('\0', pos);
      if (nullPos == std::string::npos) break;
      
      std::string section = filter.substr(pos, nullPos - pos);
      if (section.empty()) break; // Double null terminates
      
      if (inPattern)
      {
        // This is a pattern section like "*.ext1;*.ext2"
        size_t extPos = 0;
        while (extPos < section.size())
        {
          // Find next semicolon or end
          size_t semiPos = section.find(';', extPos);
          if (semiPos == std::string::npos) semiPos = section.size();
          
          std::string pattern = section.substr(extPos, semiPos - extPos);
          // Extract extension from "*.ext" format
          if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.')
          {
            std::string ext = pattern.substr(2);
            if (!ext.empty() && ext != "*")
              extensions.push_back(ext);
          }
          
          extPos = semiPos + 1;
        }
      }
      
      inPattern = !inPattern; // Alternate between description and pattern
      pos = nullPos + 1;
    }
    
    return extensions;
  }

  // Forward declare the Obj-C++ implementation functions
  bool ShowMacSavePanel_ObjC(std::string& outPath, const std::string& defaultFileName, const std::vector<std::string>& extensions);
  bool ShowMacOpenPanel_ObjC(std::string& outPath, const std::vector<std::string>& extensions);

  inline bool GetSaveFilePath(std::string& outPathUtf8, const wchar_t* filterW, const wchar_t* defaultFileNameW)
  {
    std::string defaultFileName = defaultFileNameW ? WideToUtf8(defaultFileNameW) : "Untitled.sbrain";
    std::vector<std::string> extensions = ParseFilterExtensions(filterW);
    
    return ShowMacSavePanel_ObjC(outPathUtf8, defaultFileName, extensions);
  }

  inline bool GetOpenFilePath(std::string& outPathUtf8, const wchar_t* filterW)
  {
    std::vector<std::string> extensions = ParseFilterExtensions(filterW);
    return ShowMacOpenPanel_ObjC(outPathUtf8, extensions);
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


