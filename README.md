# Audio Test Client

中文 | [English](README_EN.md)

专业的 Android 系统级音频测试工具，基于 Native C++ 开发，支持录音、播放、回环测试和参数配置。

## 目录

- [项目简介](#项目简介)
- [快速开始](#快速开始)
- [安装部署](#安装部署)
- [参数说明](#参数说明)
- [故障排除](#故障排除)
- [许可证](#许可证)

## 项目简介

Audio Test Client 是一个 Android 系统级音频测试工具，基于 Android AudioRecord 和 AudioTrack Native
API 开发。

### 核心特性

- **四种工作模式**: 录音、播放、回环测试、参数设置
- **完整音频支持**: 1-16声道，8kHz-192kHz采样率，8/16/24/32位PCM
- **多格式文件支持**: WAV 和 Raw PCM 格式读写，自动格式检测
- **智能配置**: ContentType 根据 Usage 自动映射
- **双线程回环**: 生产者-消费者架构，降低延迟
- **模块化设计**: 清晰的类层次结构和工厂模式

### 工作模式

| 模式   | 参数      | 功能描述            | 应用场景         |
|------|---------|-----------------|--------------|
| 录音模式 | `-m0`   | 从指定音频源录制到文件     | 音频采集、质量测试    |
| 播放模式 | `-m1`   | 播放音频文件          | 音频输出测试、兼容性验证 |
| 回环模式 | `-m2`   | 同时录音和播放（实时回声测试） | 延迟测试、音频链路验证  |
| 参数设置 | `-m100` | 配置音频系统参数        | 系统调优、参数验证    |

## 快速开始

### 基本语法

```bash
audio_test_client -m<mode> [options] [audio_file]
```

### 常用命令

#### 录音测试

```bash
# 使用麦克风录制48kHz双声道音频20秒
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d20

# 录制为 Raw PCM 格式
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d20 -T1
```

#### 播放测试

```bash
# 播放 WAV 文件
./audio_test_client -m1 -u1 -P/data/audio_test.wav

# 播放 Raw PCM 文件（需指定参数）
./audio_test_client -m1 -u1 -P/data/audio.pcm -r48000 -c2 -f1
```

#### 回环延迟测试

```bash
# 同时录音和播放，测试音频延迟
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -u1 -d20
```

#### 系统参数配置

```bash
# 打开音频源（媒体播放）
./audio_test_client -m100 1,1

# 关闭音频源
./audio_test_client -m100 2,1
```

**提示**: 使用 `-h` 参数查看完整帮助信息和所有参数选项。

## 安装部署

### 环境准备

```bash
# 确保设备已 root 并开启调试模式
adb root
adb remount

# 可选：关闭 SELinux（如遇权限问题）
adb shell setenforce 0
```

### 编译方式

#### 使用 Android.mk（推荐）

```bash
# 在 Android 源码环境中编译
mm audio_test_client

# 推送到设备
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

**版本兼容性**: Android.mk 会自动检测 `PLATFORM_VERSION`（14/15/16），自动适配不同 Android
版本的接口差异，无需手动修改。

#### 使用 Android.bp

```bash
# 使用 Soong 构建系统
m audio_test_client

# 推送到设备
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

**版本兼容性**: Android.bp 默认为 Android 14+ 构建，如需支持其他版本需手动修改配置文件。

**注意**: 本项目依赖 Android 系统库（libmedia、libaudioclient、libbinder 等），必须在 Android 源码树环境中编译。

### 权限设置

```bash
adb shell
cd /data
chmod 755 audio_test_client
```

## 参数说明

### 通用参数

| 参数           | 说明                              | 示例                 |
|--------------|---------------------------------|--------------------|
| `-m<mode>`   | 工作模式：0=录音, 1=播放, 2=回环, 100=设置参数 | `-m0`              |
| `-F<frames>` | 最小帧数缓冲区大小（默认系统自动）               | `-F960`            |
| `-P<path>`   | 音频文件路径                          | `-P/data/test.wav` |
| `-h`         | 显示详细帮助信息                        | `-h`               |

### 录音模式参数 (-m0)

| 参数            | 说明           | 常用值                              |
|---------------|--------------|----------------------------------|
| `-s<source>`  | 音频输入源        | 1=麦克风, 6=语音识别, 7=语音通信            |
| `-r<rate>`    | 采样率 (Hz)     | 8000, 16000, 48000               |
| `-c<count>`   | 声道数          | 1, 2                             |
| `-f<format>`  | 音频格式         | 1=PCM16, 3=PCM32, 6=PCM24_PACKED |
| `-I<flag>`    | 输入标志位        | 0=标准, 1=低延迟                      |
| `-d<seconds>` | 录音时长（秒，0=无限） | `10`                             |
| `-T<type>`    | 文件格式         | 0=WAV(默认), 1=Raw PCM             |

**文件输出**: 若未指定 `-P` 参数，自动生成文件名格式：

```
/data/audio_record_YYYYMMDD_HHMMSS_[采样率]_[声道数]_[位深].wav
```

例如: `/data/audio_record_20260315_143052_48000_2_16.wav`

**文件格式说明**：

- WAV 格式（`-T0`）：带文件头，包含音频参数信息
- Raw PCM 格式（`-T1`）：纯音频数据，无文件头
- 文件扩展名优先级高于 `-T` 参数

### 播放模式参数 (-m1)

| 参数          | 说明           | 常用值                |
|-------------|--------------|--------------------|
| `-u<usage>` | 音频用途类型       | 1=媒体, 2=通话, 14=游戏  |
| `-O<flag>`  | 输出标志位        | 0=标准, 4=低延迟        |
| `-P<path>`  | 播放文件路径（必须指定） | `-P/data/test.wav` |

**文件格式自动识别**：

- `.wav` → WAV 格式
- `.pcm`, `.raw` → Raw PCM 格式（需通过 `-r`、`-c`、`-f` 指定参数）

**注意**: ContentType 会根据 Usage 自动设置，无需手动指定。

### 参数设置模式 (-m100)

```bash
./audio_test_client -m100 <operation>,<usage>
```

| 位置 | 参数        | 说明                            |
|----|-----------|-------------------------------|
| 1  | operation | 1=open_source, 2=close_source |
| 2  | usage     | 音频用途（见 Usage 枚举）              |

### 常用枚举值

#### 音频输入源 (Audio Source)

| 值 | 常量名                              | 说明   | 适用场景    |
|---|----------------------------------|------|---------|
| 1 | AUDIO_SOURCE_MIC                 | 主麦克风 | 语音录制    |
| 6 | AUDIO_SOURCE_VOICE_RECOGNITION   | 语音识别 | ASR 应用  |
| 7 | AUDIO_SOURCE_VOICE_COMMUNICATION | 语音通信 | VoIP 应用 |

**完整枚举值请使用 `-h` 参数查看**

#### 音频用途类型 (Audio Usage)

| 值  | 常量名                             | 说明   | 自动映射 ContentType    |
|----|---------------------------------|------|---------------------|
| 1  | AUDIO_USAGE_MEDIA               | 媒体播放 | CONTENT_TYPE_MUSIC  |
| 2  | AUDIO_USAGE_VOICE_COMMUNICATION | 语音通信 | CONTENT_TYPE_SPEECH |
| 14 | AUDIO_USAGE_GAME                | 游戏   | CONTENT_TYPE_MUSIC  |

**完整枚举值请使用 `-h` 参数查看**

#### 输入/输出标志位

| 值 | 输入标志 (-I) | 输出标志 (-O)   | 说明               |
|---|-----------|-------------|------------------|
| 0 | NONE      | NONE        | 标准延迟 (~40-80ms)  |
| 1 | FAST      | -           | 低延迟输入 (~10-20ms) |
| 4 | -         | FAST        | 低延迟输出 (~10-20ms) |
| 8 | SYNC      | DEEP_BUFFER | 同步/省电模式          |

## 故障排除

### 常见问题

#### 1. 权限问题

```bash
# 错误: Permission denied
adb root && adb remount
adb shell setenforce 0
chmod 755 /data/audio_test_client
```

#### 2. 音频设备占用

```bash
# 错误: AudioRecord/AudioTrack initialization failed
adb shell stop audioserver
adb shell start audioserver
```

#### 3. 文件写入失败

```bash
# 错误: Failed to create WAV file
adb shell df /data  # 检查磁盘空间
```

### 调试模式

```bash
# 查看实时日志
adb logcat -s audio_test_client

# 查看音频系统日志
adb logcat -s AudioFlinger AudioPolicyService
```

## 实时监控

### 进度显示

录音和播放过程中会定期显示进度：

```
Recording ... , processed 10.00 seconds, 1.92 MB
Recording ... , processed 20.00 seconds, 3.84 MB
```

- 显示已处理的秒数
- 显示已处理的数据量 (MB)

### 电平表

录音和播放过程中会实时显示音频电平：

```
[2026-03-15 14:30:52] Audio Level: -6.2 dB, size: 4096 bytes
```

- 支持 16/24/32 位 PCM 格式
- 显示 dB 值（专业音频标准单位）

### 中断处理

按 `Ctrl+C` 可随时安全停止录音或播放，已录制的数据会自动保存并更新 WAV 文件头。

## 性能指标

- **低延迟模式**: ~10-20ms (使用 FAST 标志)
- **标准模式**: ~40-80ms
- **深度缓冲**: ~80-200ms (省电模式)
- **采样率**: 8kHz - 192kHz
- **声道数**: 1-16 声道
- **最大文件**: 4GB WAV 文件

## 相关项目

- [AudioRecorder](https://github.com/kainan-tek/AudioRecorder) - 基于 AudioRecord API 的音频录制器
- [AAudioRecorder](https://github.com/kainan-tek/AAudioRecorder) - 基于 AAudio API 的高性能录音器
- [AudioPlayer](https://github.com/kainan-tek/AudioPlayer) - 基于 AudioTrack API 的音频播放器
- [AAudioPlayer](https://github.com/kainan-tek/AAudioPlayer) - 基于 AAudio API 的高性能播放器

## 许可证

本项目采用 GNU General Public License v3.0 许可证。详细信息请参阅 [LICENSE](LICENSE) 文件。

---

**注意**: 本工具专为 Android 系统级音频开发和测试设计，需要系统级权限。请确保在合适的测试环境中使用。
