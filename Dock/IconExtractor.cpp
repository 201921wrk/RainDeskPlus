/*
 * RainDeskPlus - Desktop beautification platform
 * Dock 模块（全新实现，GPL v2）
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * IconExtractor 实现。拿到 HICON 后统一走「32bpp 顶朝下 DIB → DrawIconEx（GDI 缩放）
 * → WIC 位图 → Canvas::SaveBitmapToPng」，全程不引入 GDI+ 初始化（DD-14）。
 * 详见 docs/DOCK_DESIGN.md §7.6.4。
 */
#include "IconExtractor.h"

#include "Canvas.h"
#include "Logger.h"
#include "PathUtil.h"
#include "StringUtil.h"

#include <cstdint>
#include <cstring>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>        // IMAGEINFO 等类型（commoncontrols.h 的前置依赖）
#include <commoncontrols.h>  // IImageList（SHIL_JUMBO 图标列表）
#include <shellapi.h>        // SHGetFileInfoW
#include <shlobj.h>          // SHGetImageList / SHCreateDirectoryExW
#include <wincodec.h>        // GUID_WICPixelFormat32bppPBGRA

namespace raindock {

namespace {

// 输出边长：128 取自 jumbo（256）图列表的一半——是「缩小」而不是把 32/48 放大。
constexpr int kIconPx = 128;

// 这些扩展名的文件**本身就是图标**，直接复制，避免被 shell 当成「PNG 文件类型」。
bool IsImageFile(const std::wstring& lowerPath)
{
    static const WCHAR* const kExtensions[] = { L".png", L".jpg", L".jpeg", L".bmp", L".gif" };
    for (const WCHAR* ext : kExtensions)
    {
        const std::wstring suffix(ext);
        if (lowerPath.size() > suffix.size() &&
            lowerPath.compare(lowerPath.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            return true;
        }
    }
    return false;
}

bool IsIcoFile(const std::wstring& lowerPath)
{
    const std::wstring suffix(L".ico");
    return lowerPath.size() > suffix.size() &&
           lowerPath.compare(lowerPath.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// 递归创建目录；已存在视为成功（SHCreateDirectoryExW 复用 shell32 的路径解析）。
bool EnsureDirectory(const std::wstring& folder)
{
    const int result = ::SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
           result == ERROR_FILE_EXISTS;
}

// .ico：直接按目标尺寸从文件加载（LR_LOADFROMFILE 返回的 HICON 需 DestroyIcon）。
HICON LoadIcoFile(const std::wstring& path)
{
    return static_cast<HICON>(
        ::LoadImageW(nullptr, path.c_str(), IMAGE_ICON, kIconPx, kIconPx, LR_LOADFROMFILE));
}

// 其余文件：取 shell 图标。优先 jumbo（256，缩放到 128 观感最好），失败回退大图标。
HICON LoadShellIcon(const std::wstring& path)
{
    SHFILEINFOW info = {};
    if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX) != 0)
    {
        IImageList* imageList = nullptr;
        if (SUCCEEDED(::SHGetImageList(SHIL_JUMBO, IID_PPV_ARGS(&imageList))) &&
            imageList != nullptr)
        {
            HICON hIcon = nullptr;
            const HRESULT hr = imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &hIcon);
            imageList->Release();
            if (SUCCEEDED(hr) && hIcon != nullptr) return hIcon;
        }
    }

    if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                         SHGFI_ICON | SHGFI_LARGEICON) != 0)
    {
        return info.hIcon;
    }
    return nullptr;
}

// HICON → 128×128 PNG（复用 Canvas 的 WIC 工厂与 PNG 编码器，零新增依赖）。
bool RenderIconToPng(HICON hIcon, const std::wstring& outPngPath)
{
    if (hIcon == nullptr) return false;

    // 1. 32bpp 顶朝下 DIB：负高度免去逐行翻转；BI_RGB 与 WIC 的 BGRA 布局一致。
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = kIconPx;
    bmi.bmiHeader.biHeight      = -kIconPx;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hScreenDc = ::GetDC(nullptr);
    if (hScreenDc == nullptr) return false;

    void* bits = nullptr;
    HBITMAP hDib = ::CreateDIBSection(hScreenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, hScreenDc);
    if (hDib == nullptr || bits == nullptr)
    {
        if (hDib != nullptr) ::DeleteObject(hDib);
        return false;
    }

    HDC hMemDc = ::CreateCompatibleDC(nullptr);
    if (hMemDc == nullptr)
    {
        ::DeleteObject(hDib);
        return false;
    }
    HGDIOBJ hOldBitmap = ::SelectObject(hMemDc, hDib);

    // 2. 先清成全透明，再由 GDI 完成 256→128 的缩放绘制。
    std::memset(bits, 0, static_cast<size_t>(kIconPx) * kIconPx * 4u);
    ::DrawIconEx(hMemDc, 0, 0, hIcon, kIconPx, kIconPx, 0, nullptr, DI_NORMAL);

    ::SelectObject(hMemDc, hOldBitmap);
    ::DeleteDC(hMemDc);

    // 3. alpha 修补：GDI 不会为「无 alpha 通道的老图标」写 alpha 位，落盘后会出现
    //    「有颜色但完全透明」。凡 RGB 非零而 alpha 为 0 的像素补成不透明。
    //    带 alpha 的图标由 DrawIconEx 写入预乘值，故整体按 PBGRA 交给 WIC。
    auto* pixels = static_cast<uint32_t*>(bits);
    for (int i = 0; i < kIconPx * kIconPx; ++i)
    {
        const uint32_t value = pixels[i];
        const uint32_t alpha = (value >> 24) & 0xFFu;
        if (alpha == 0 && (value & 0x00FFFFFFu) != 0)
        {
            pixels[i] = value | 0xFF000000u;
        }
    }

    // 4. DIB 像素 → WIC 位图 → PNG。DIB 必须在 SaveBitmapToPng 之后才释放。
    bool ok = false;
    IWICImagingFactory* wic = Canvas::GetWICFactory();
    if (wic != nullptr)
    {
        IWICBitmap* bitmap = nullptr;
        const UINT side   = static_cast<UINT>(kIconPx);
        const UINT stride = side * 4u;
        const UINT size   = stride * side;
        if (SUCCEEDED(wic->CreateBitmapFromMemory(side, side, GUID_WICPixelFormat32bppPBGRA,
                                                  stride, size, static_cast<BYTE*>(bits),
                                                  &bitmap)) &&
            bitmap != nullptr)
        {
            ok = Canvas::SaveBitmapToPng(bitmap, outPngPath);
            bitmap->Release();
        }
    }

    ::DeleteObject(hDib);   // 与 CreateDIBSection 配对
    return ok;
}

}  // namespace

bool IconExtractor::ExtractToPng(const std::wstring& path, const std::wstring& outPngPath)
{
    if (path.empty() || outPngPath.empty()) return false;

    const std::wstring folder = PathUtil::GetFolderFromFilePath(outPngPath);
    if (!folder.empty() && !EnsureDirectory(folder))
    {
        LogWarningF(L"RainDeskPlus Dock: 创建图标目录失败（\"%s\"）", folder.c_str());
        return false;
    }

    std::wstring lowerPath = path;
    StringUtil::ToLowerCase(lowerPath);

    // 图片文件本身就是图标：直接复制，保证用户拖入的图片不失真（§7.6.4）。
    if (IsImageFile(lowerPath))
    {
        if (::CopyFileW(path.c_str(), outPngPath.c_str(), FALSE) != FALSE) return true;
        LogWarningF(L"RainDeskPlus Dock: 复制图标文件失败（\"%s\"）", path.c_str());
        return false;
    }

    // HICON 的所有权：LoadIcoFile / LoadShellIcon 的返回值都必须 DestroyIcon。
    HICON hIcon = IsIcoFile(lowerPath) ? LoadIcoFile(path) : LoadShellIcon(path);
    if (hIcon == nullptr)
    {
        LogWarningF(L"RainDeskPlus Dock: 提取图标失败（\"%s\"）", path.c_str());
        return false;
    }

    const bool ok = RenderIconToPng(hIcon, outPngPath);
    ::DestroyIcon(hIcon);   // 与 LoadImageW / SHGetFileInfoW / IImageList::GetIcon 配对

    if (!ok)
    {
        LogWarningF(L"RainDeskPlus Dock: 图标转 PNG 失败（\"%s\"）", outPngPath.c_str());
    }
    return ok;
}

}  // namespace raindock
