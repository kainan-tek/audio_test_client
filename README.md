# Audio Test Client

专业的 Android 系统级音频测试工具，基于 Native C++ 开发，支持多种音频操作模式和参数配置。

## 📋 项目概述

Audio Test Client 是一个功能强大的 Android 系统级音频测试工具，基于 Android AudioRecord 和 AudioTrack Native API 开发。该项目提供了完整的音频录制、播放、回环测试和系统参数配置功能，支持多种音频格式和配置选项，是音频系统开发和测试的专业工具。

## ✨ 主要特性

- **🎵 四种工作模式**: 录音、播放、回环测试、参数设置
- **🔊 完整音频支持**: 支持1-16声道，8kHz-192kHz采样率
- **🌟 Native层实现**: 基于C++17和Android Native API
- **🔧 灵活配置**: 支持多种音频源、用途、格式和标志位
- **📱 WAV文件支持**: 完整的WAV文件读写功能，支持PCM格式
- **🛠️ 实时监控**: 提供详细的音频流状态和性能信息
- **🎯 信号处理**: 优雅的信号处理机制，支持安全中断
- **🏗️ 模块化设计**: 清晰的类层次结构和工厂模式

## 🎵 工作模式

### 模式概览
| 模式 | 参数 | 功能描述 | 应用场景 |
|-----|------|----------|----------|
| 录音模式 | `-m0` | 从指定音频源录制到 WAV 文件 | 音频采集、质量测试、延迟测量 |
| 播放模式 | `-m1` | 播放 WAV 音频文件 | 音频输出测试、兼容性验证 |
| 回环模式 | `-m2` | 同时录音和播放（实时回声测试） | 延迟测试、音频链路验证 |
| 参数设置 | `-m100` | 配置音频系统参数 | 系统调优、参数验证 |

### 音频格式支持

**采样率范围**: 8kHz - 192kHz  
**声道配置**: 1-16声道  
**位深度**: 8/16/24/32位PCM  
**文件格式**: WAV (RIFF/WAVE)

## 🚀 快速开始

### 系统要求

- Android 系统 (API Level 21+)
- Root 权限 (推荐)
- Native 开发环境

### 环境准备

```bash
# 确保设备已 root 并开启调试模式
adb root
adb remount

# 可选：关闭 SELinux（如遇权限问题）
adb shell setenforce 0
```

### 编译安装

#### 使用 Android.bp (推荐)
```bash
# 使用 Soong 构建系统
m audio_test_client

# 推送到设备
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

#### 使用 Android.mk (传统方式)
```bash
# 在 Android 源码环境中编译
mm audio_test_client

# 推送到设备
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

#### 使用 CMake (跨平台)
```bash
mkdir build && cd build
cmake ..
make

# 推送到设备
adb push audio_test_client /data/
```

### 设置权限

```bash
adb shell
cd /data
chmod 755 audio_test_client
```

## 📖 使用说明

### 基本操作

#### 录音测试
```bash
# 使用麦克风录制 10 秒 48kHz 立体声音频
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d10 -P/data/test_record.wav

# 自动生成文件名的录音
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d10

# 低延迟录音模式
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I1 -d10
```

#### 播放测试
```bash
# 播放指定的 WAV 文件
./audio_test_client -m1 -u1 -C0 -O4 -P/data/test_record.wav

# 使用特定缓冲区大小播放
./audio_test_client -m1 -u1 -C0 -O4 -F960 -P/data/test_record.wav

# 低延迟播放模式
./audio_test_client -m1 -u1 -C0 -O4 -F480 -P/data/test_record.wav
```

#### 回环延迟测试
```bash
# 录音和播放同时进行，测试音频延迟
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -I1 -u1 -C0 -O4 -F960 -d20 -P/data/loopback_test.wav
```

#### 系统参数配置
```bash
# 打开音频源并设置用途为媒体播放
./audio_test_client -m100 1,1

# 关闭音频源
./audio_test_client -m100 2,1
```

## 🔧 命令行参数详解

### 基本语法
```
audio_test_client -m<mode> [options] [audio_file]
```

### 通用参数

| 参数 | 类型 | 说明 | 默认值 | 示例 |
|-----|------|------|-------|------|
| `-m<mode>` | int | 工作模式：0=录音, 1=播放, 2=回环, 100=设置参数 | 必填 | `-m0` |
| `-F<frames>` | int | 最小帧数缓冲区大小 | 系统自动 | `-F960` |
| `-P<path>` | string | 音频文件路径 | 自动生成 | `-P/data/test.wav` |
| `-h` | - | 显示详细帮助信息 | - | `-h` |

### 录音模式参数 (-m0)

| 参数 | 类型 | 说明 | 可选值 | 示例 |
|-----|------|------|-------|------|
| `-s<source>` | int | 音频输入源 | 见音频源枚举表 | `-s1` |
| `-r<rate>` | int | 采样率 (Hz) | 8000-192000 | `-r48000` |
| `-c<count>` | int | 声道数 | 1-16 | `-c2` |
| `-f<format>` | int | 音频格式 | 1=PCM16, 2=PCM8, 3=PCM32 | `-f1` |
| `-I<flag>` | int | 输入标志位 | 见输入标志枚举表 | `-I1` |
| `-d<seconds>` | int | 录音时长（秒） | 0=无限录音 | `-d10` |

### 播放模式参数 (-m1)

| 参数 | 类型 | 说明 | 可选值 | 示例 |
|-----|------|------|-------|------|
| `-u<usage>` | int | 音频用途类型 | 见用途类型枚举表 | `-u1` |
| `-C<type>` | int | 内容类型 | 0=MUSIC, 1=SPEECH, 2=SONIFICATION, 3=MOVIE | `-C0` |
| `-O<flag>` | int | 输出标志位 | 见输出标志枚举表 | `-O4` |

### 参数设置模式 (-m100)

支持逗号分隔的多参数格式：
```bash
./audio_test_client -m100 <operation>,<usage>[,<param3>,...]
```

| 位置 | 参数 | 说明 | 可选值 |
|-----|------|------|-------|
| 1 | operation | 操作类型 | 1=open_source, 2=close_source |
| 2 | usage | 音频用途 | 见用途类型枚举表 |
| 3+ | reserved | 预留扩展参数 | 待定义 |

## 📚 枚举值参考

### 音频输入源 (Audio Source)

| 值 | 常量名 | 说明 | 适用场景 |
|---|--------|------|----------|
| 0 | AUDIO_SOURCE_DEFAULT | 系统默认 | 通用录音 |
| 1 | AUDIO_SOURCE_MIC | 主麦克风 | 语音录制 |
| 2 | AUDIO_SOURCE_VOICE_UPLINK | 通话上行 | 通话录音 |
| 3 | AUDIO_SOURCE_VOICE_DOWNLINK | 通话下行 | 通话录音 |
| 4 | AUDIO_SOURCE_VOICE_CALL | 双向通话 | 通话录音 |
| 5 | AUDIO_SOURCE_CAMCORDER | 摄像头录音 | 视频录制 |
| 6 | AUDIO_SOURCE_VOICE_RECOGNITION | 语音识别 | ASR 应用 |
| 7 | AUDIO_SOURCE_VOICE_COMMUNICATION | 语音通信 | VoIP 应用 |
| 8 | AUDIO_SOURCE_REMOTE_SUBMIX | 远程子混音 | 系统音频捕获 |
| 9 | AUDIO_SOURCE_UNPROCESSED | 未处理音频 | 原始音频采集 |

### 音频用途类型 (Audio Usage)

| 值 | 常量名 | 说明 | 音频特性 |
|---|--------|------|----------|
| 1 | AUDIO_USAGE_MEDIA | 媒体播放 | 音乐、视频 |
| 2 | AUDIO_USAGE_VOICE_COMMUNICATION | 语音通信 | 通话、VoIP |
| 3 | AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING | 通信信号 | 拨号音、忙音 |
| 4 | AUDIO_USAGE_ALARM | 闹钟 | 系统闹钟 |
| 5 | AUDIO_USAGE_NOTIFICATION | 通知 | 系统通知 |
| 6 | AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE | 来电铃声 | 电话铃声 |
| 7 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST | 通信请求 | 消息提示 |
| 8 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT | 即时通信 | IM 消息 |
| 9 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED | 延迟通信 | 邮件提示 |
| 10 | AUDIO_USAGE_NOTIFICATION_EVENT | 事件通知 | 日历提醒 |
| 11 | AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY | 无障碍 | 辅助功能 |
| 12 | AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE | 导航 | GPS 导航 |
| 13 | AUDIO_USAGE_ASSISTANCE_SONIFICATION | 声音反馈 | 按键音 |
| 14 | AUDIO_USAGE_GAME | 游戏 | 游戏音效 |
| 15 | AUDIO_USAGE_ASSISTANT | 语音助手 | AI 助手 |

### 输入标志位 (Input Flags)

| 值 | 常量名 | 说明 | 性能特性 |
|---|--------|------|----------|
| 0 | AUDIO_INPUT_FLAG_NONE | 无特殊标志 | 标准延迟 |
| 1 | AUDIO_INPUT_FLAG_FAST | 低延迟录音 | ~10-20ms |
| 2 | AUDIO_INPUT_FLAG_HW_HOTWORD | 硬件热词检测 | 低功耗 |
| 4 | AUDIO_INPUT_FLAG_RAW | 原始音频输入 | 未处理 |
| 8 | AUDIO_INPUT_FLAG_SYNC | 同步音频输入 | 精确同步 |

### 输出标志位 (Output Flags)

| 值 | 常量名 | 说明 | 性能特性 |
|---|--------|------|----------|
| 0 | AUDIO_OUTPUT_FLAG_NONE | 无特殊标志 | 标准延迟 |
| 1 | AUDIO_OUTPUT_FLAG_DIRECT | 直接输出 | 绕过混音器 |
| 2 | AUDIO_OUTPUT_FLAG_PRIMARY | 主要输出 | 系统主输出 |
| 4 | AUDIO_OUTPUT_FLAG_FAST | 低延迟播放 | ~10-20ms |
| 8 | AUDIO_OUTPUT_FLAG_DEEP_BUFFER | 深度缓冲 | 省电模式 |

## 🏗️ 技术架构

### 类层次结构

```
AudioOperation (抽象基类)
├── AudioRecordOperation    (录音操作)
├── AudioPlayOperation      (播放操作)
├── AudioLoopbackOperation  (回环操作)
└── SetParamsOperation      (参数设置)
```

### 核心组件

#### 1. WAV 文件管理 (WAVFile)
- 完整的 WAV 文件头解析和生成
- 支持 PCM 格式的读写操作
- 自动处理字节序和数据对齐
- 支持大文件处理 (最大2GB)

#### 2. 缓冲区管理 (BufferManager)
- 动态缓冲区分配和管理
- 内存安全保护机制
- 高效的数据拷贝操作
- 智能大小调整 (480B - 64MB)

#### 3. 音频工具类 (AudioUtils)
- 音频格式转换和验证
- 采样率和通道数计算
- 音频参数合法性检查
- 时间戳和文件路径生成

#### 4. 参数管理器 (AudioParameterManager)
- 系统音频参数配置
- 通道掩码设置
- 音频源开关控制
- 用途类型字符串转换

#### 5. 命令行解析器 (CommandLineParser)
- 完整的参数解析和验证
- 错误处理和帮助信息
- 参数兼容性检查
- 多参数格式支持

### 技术栈

- **语言**: C++17
- **音频API**: Android AudioRecord/AudioTrack Native API
- **构建系统**: Android.bp (Soong) / Android.mk / CMake
- **依赖库**: libmedia, libaudioclient, libutils, libbinder
- **最低版本**: Android API Level 21
- **目标架构**: ARM64, ARM32, x86, x86_64

## 🔍 技术细节

### AudioRecord/AudioTrack集成

- 使用同步传输模式实现音频操作
- 支持多种音频格式 (8/16/24/32位PCM)
- 完整的错误处理机制
- 音频焦点和参数管理

### 数据流架构

```
录音: AudioRecord → BufferManager → WAVFile → 存储设备
播放: 存储设备 → WAVFile → BufferManager → AudioTrack
回环: AudioRecord → BufferManager → AudioTrack + WAVFile
```

### WAV文件支持

- 标准RIFF/WAVE格式解析
- 支持多声道音频 (1-16声道)
- 采样率范围: 8kHz - 192kHz
- 位深度支持: 8/16/24/32位
- 自动文件头更新和完整性检查

## 📊 性能指标

- **低延迟模式**: ~10-20ms (使用FAST标志)
- **标准模式**: ~40-80ms
- **深度缓冲**: ~80-200ms (省电模式)
- **采样率**: 8kHz - 192kHz
- **声道数**: 1-16声道
- **位深度**: 8/16/24/32位
- **最大文件**: 2GB WAV文件

## 🐛 故障排除

### 常见问题

#### 1. 权限问题
```bash
# 错误: Permission denied
# 解决方案:
adb root
adb remount
adb shell setenforce 0
chmod 755 /data/audio_test_client
```

#### 2. 音频设备占用
```bash
# 错误: AudioRecord/AudioTrack initialization failed
# 解决方案: 停止其他音频应用或重启音频服务
adb shell stop audioserver
adb shell start audioserver
```

#### 3. 文件写入失败
```bash
# 错误: Failed to create WAV file
# 解决方案: 检查磁盘空间和目录权限
adb shell df /data
adb shell ls -la /data/
```

#### 4. 缓冲区大小问题
```bash
# 错误: Buffer allocation failed
# 解决方案: 调整帧数大小
./audio_test_client -m0 -F480 -s1 -r48000 -c2 -f1
```

### 调试模式

启用详细日志输出：
```bash
# 设置日志级别
adb shell setprop log.tag.audio_test_client VERBOSE

# 查看实时日志
adb logcat -s audio_test_client

# 查看音频系统日志
adb logcat -s AudioFlinger AudioPolicyService
```

## 📈 开发指南

### 构建要求

- **Android SDK**: API Level 21+
- **NDK**: r21+
- **C++ 标准**: C++17
- **构建系统**: Android.bp / Android.mk / CMake

### 依赖库

```cpp
// 核心音频依赖
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>
#include <media/AudioParameter.h>

// 系统工具依赖
#include <utils/Log.h>
#include <utils/String8.h>
#include <binder/Binder.h>
```

### 扩展开发

#### 添加新的音频格式
1. 在 `AudioUtils::parseFormatOption()` 中添加格式映射
2. 更新 `WAVFile::getAudioFormat()` 的格式支持
3. 修改命令行解析器的验证逻辑

#### 添加新的操作模式
1. 继承 `AudioOperation` 基类
2. 实现 `execute()` 纯虚函数
3. 在 `AudioOperationFactory::createOperation()` 中注册新模式
4. 更新命令行解析器和帮助信息

#### 性能优化建议
- 使用FAST标志降低延迟
- 合理设置缓冲区大小
- 避免在音频线程中进行文件I/O
- 使用MMAP模式提高性能

## 📄 许可证

本项目采用 GNU General Public License v3.0 许可证。详细信息请参阅 [LICENSE](LICENSE) 文件。

---

**注意**: 本工具专为Android系统级音频开发和测试设计，需要系统级权限。请确保在合适的测试环境中使用。
