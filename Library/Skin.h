/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 对应 Rainmeter: Library/Skin.h（皮肤容器，简化版）。
 */
#ifndef RAINDOCK_LIBRARY_SKIN_H_
#define RAINDOCK_LIBRARY_SKIN_H_

#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID2D1RenderTarget;

namespace raindock {

class Measure;
class Meter;
class ConfigParser;
class CommandHandler;

class Skin
{
public:
    Skin();
    ~Skin();
    Skin(const Skin&) = delete;
    Skin& operator=(const Skin&) = delete;

    // 加载皮肤 INI（含其变体/默认值）。
    bool Load(const std::wstring& iniPath);
    // 周期刷新（驱动所有 Measure.Update -> Meter.Update）。
    void Update();
    // 渲染（驱动所有 Meter.Draw）。
    void Render(ID2D1RenderTarget* rt);
    // 执行 Bang 命令。
    void DoBang(const std::wstring& bang);

    HWND GetWindow() const { return m_Window; }
    void SetWindow(HWND hWnd) { m_Window = hWnd; }

    const std::vector<std::unique_ptr<Measure>>& GetMeasures() const { return m_Measures; }
    const std::vector<std::unique_ptr<Meter>>&   GetMeters()  const { return m_Meters; }

private:
    std::vector<std::unique_ptr<Measure>> m_Measures;
    std::vector<std::unique_ptr<Meter>>   m_Meters;
    std::unique_ptr<ConfigParser>         m_Parser;
    HWND                                   m_Window = nullptr;
    uint32_t                               m_UpdateInterval = 1000;
};

}  // namespace raindock

#endif  // RAINDOCK_LIBRARY_SKIN_H_
