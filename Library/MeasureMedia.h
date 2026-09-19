/*
 * RainDeskPlus - Desktop beautification platform
 * Derived from Rainmeter - GPL v2
 * Copyright (C) 2014-2025 Rainmeter Project
 * Copyright (C) 2026 RainDeskPlus Project
 *
 * 媒体播放 Measure（D24）。
 *
 * 与其他 Measure 一致，本阶段是「离线可测骨架」：StateFile= 指向一份媒体会话
 * JSON 快照（title / artist / album / state / position / duration），无播放器、
 * 无网络也能全量回归；真实会话读取（各播放器 SDK / SMTC）作为同一接口的第二段
 * 后续接上，届时快照来源换成实时采样，本类的解析与产出契约不变。
 *
 * INI 键：
 *   StateFile=  快照路径。绝对路径直接用；相对路径依次相对皮肤 @Resources 目录、
 *               皮肤 INI 所在目录解析（无皮肤上下文时按进程工作目录）。
 *   Field=      取快照中的哪个字段。一个 Measure 段只产出一条字符串，故多字段
 *               挂件写成多个 Measure=Media 段，各用 Field= 分工：
 *               Title（默认）/ Artist / Album / State / Progress。
 *   MaxValue=   进度量程，默认 1（见下）。一般无需显式给出。
 *
 * 输出约定：
 *   m_Value       恒为播放进度归一化比（0..1；duration<=0 时为 0）。因此 MeterBar
 *                 可绑定任意一个 Media Measure。未显式给量程时本类补 MaxValue=1，
 *                 否则 MeterBar 会因 span<=0 恒取 0（MeterBar.cpp:71-74）。
 *   m_StringValue 随 Field= 变化。字段缺失时给占位 "-"，而不是留空串 —— 空串会让
 *                 基类 GetString() 回退成 "0.0" 这类裸数值（Measure.cpp:141-145），
 *                 对文本挂件是噪声。
 *
 * 失败回退：快照缺失 / JSON 畸形 / 非媒体报文时沿用上一次成功值（数值本就留在
 * m_Value 上，字符串靠 m_LastString 自留一份），不打回 0；从未成功过则值为 0、
 * 字符串取该字段的空形态。
 *
 * 播放控制：Command() 广播 WM_APPCOMMAND（只依赖 user32，零新增构建依赖）。
 * 该通道的系统指令集是固定的四条，见 ResolveAppCommand()；无播放器会话时广播
 * 无人响应，静默无副作用。
 */
#pragma once

#include "Measure.h"

namespace raindock {

class MeasureMedia : public Measure
{
public:
    MeasureMedia(Skin* skin, const WCHAR* name);
    ~MeasureMedia() override = default;

    UINT GetTypeID() override { return TypeID<MeasureMedia>(); }

    void Initialize(ConfigParser& parser, const std::wstring& iniPath) override;

    // 执行播放控制命令（!CommandMeasure 的落点）。命令名大小写不敏感。
    void Command(const std::wstring& command) override;

    // 命令名 → WM_APPCOMMAND 的 lParam 值（MAKELPARAM(0, appCommand)）。
    // 公开是为了让离线 Smoke 能断言控制通道的映射，而不必真的去操作播放器
    // （与 MeasureWeather::HasApiKey 的用途一致：给测试一个可观察的接口）。
    static bool ResolveAppCommand(const std::wstring& command, WPARAM& appCommand);

protected:
    void ReadOptions(ConfigParser& parser, std::wstring_view section) override;
    void UpdateValue() override;

private:
    // 本段产出哪个字段（一个 Measure 段只对应一条字符串）。
    enum class Field { Title, Artist, Album, State, Progress };

    // StateFile= 相对路径解析（绝对路径/空值原样返回）。
    std::wstring ResolveStatePath(const std::wstring& raw);

    // 失败回退：有成功基准则灌回上一次字符串，否则给该字段的空形态。
    void RestoreLastValue();

    std::wstring m_StateFile;             // 解析后的可用路径
    Field        m_Field = Field::Title;

    // 回退基准：Measure::Update() 每帧都会清空 m_StringValue（Measure.cpp:90），
    // 故上一次成功产出的字符串必须自己留一份，否则回退时只剩裸数值。
    bool         m_HasValue  = false;
    std::wstring m_LastString;
};

}  // namespace raindock
