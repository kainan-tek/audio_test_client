# Audio Test Client

中文 | [English](README_EN.md)

专业的 Android 系统级音频测试工具，专为车载 AAOS 音频开发与测试设计，支持录音、播放、回环测试和参数配置。

## 目录

- [项目简介](#项目简介)
- [快速开始](#快速开始)
- [安装部署](#安装部署)
- [参数说明](#参数说明)
- [故障排除](#故障排除)
- [许可证](#许可证)

## 项目简介

Audio Test Client 是一个 Android 系统级音频测试工具，基于 Android AudioRecord 和 AudioTrack Native API 开发。

- **四种工作模式**: 录音、播放、回环测试、参数设置
- **完整音频支持**: 1-16声道，8kHz-192kHz采样率，8/16/24/32位PCM
- **多格式文件支持**: WAV 和 Raw PCM 格式读写，自动格式检测
- **智能配置**: ContentType 根据 Usage 自动映射
- **双线程回环**: 生产者-消费者架构，防止录音数据丢失

| 模式 | 参数 | 功能描述 | 应用场景 |
| --- | --- | --- | --- |
| 录音模式 | `-m0` | 从指定音频源录制到文件 | 音频采集、质量测试 |
| 播放模式 | `-m1` | 播放音频文件 | 音频输出测试、兼容性验证 |
| 回环模式 | `-m2` | 同时录音和播放（实时回声测试） | 延迟测试、音频链路验证 |
| 参数设置 | `-m100` | 配置音频系统参数 | 系统调优、参数验证 |

## 快速开始

### 基本语法

```bash
audio_test_client -m<mode> [options]
```

### 常用命令

#### 录音测试

```bash
# 使用麦克风录制48kHz双声道音频20秒
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20

# 录制为 Raw PCM 格式
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20 -T1
```

#### 播放测试

```bash
# 播放 WAV 文件
./audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav

# 播放 Raw PCM 文件（需指定参数）
./audio_test_client -m1 -u1 -O0 -F960 -P/data/audio.pcm -r48000 -c2 -f1
```

#### 回环延迟测试

```bash
# 同时录音和播放，测试音频延迟
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
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

# 推送到设备并设置权限
adb push out/target/product/[device]/system/bin/audio_test_client /data/
adb shell chmod 755 /data/audio_test_client
```

**版本兼容性**: Android.mk 会自动检测 `PLATFORM_VERSION`（14/15/16），自动适配不同 Android 版本的接口差异，无需手动修改。

#### 使用 Android.bp（可选）

默认使用 Android.mk。若改用 Soong 构建，需先启用 Android.bp 并禁用 Android.mk（两者定义同名模块，不能共存）：

```bash
mv Android.bp_bk Android.bp      # 启用 Android.bp
mv Android.mk Android.mk_bk      # 禁用 Android.mk
m audio_test_client
```

推送与权限设置与 Android.mk 方式一致。

**版本兼容性**: Android.bp 默认为 Android 14+ 构建，如需支持其他版本需手动修改配置文件。

**注意**: 本项目依赖 Android 系统库（libmedia、libaudioclient、libbinder 等），必须在 Android 源码树环境中编译。

## 参数说明

### 通用参数

| 参数 | 说明 | 示例 |
| --- | --- | --- |
| `-m<mode>` | 工作模式：0=录音, 1=播放, 2=回环, 100=设置参数 | `-m0` |
| `-F<frames>` | 帧数（未指定：FAST=20ms，非 FAST=系统最小值） | `-F960` (20ms@48kHz) |
| `-P<path>` | 音频文件路径 | `-P/data/test.wav` |
| `-h` | 显示详细帮助信息 | `-h` |

### 录音模式参数 (-m0)

| 参数 | 说明 | 常用值 |
| --- | --- | --- |
| `-s<source>` | 音频输入源 | 1=麦克风, 6=语音识别, 7=语音通信 |
| `-r<rate>` | 采样率 (Hz) | 8000, 16000, 48000 |
| `-c<count>` | 声道数 | 1, 2 |
| `-f<format>` | 音频格式 | 1=PCM16, 3=PCM32, 6=PCM24_PACKED |
| `-I<flag>` | 输入标志位 | 0=标准, 1=FAST低延迟 |
| `-d<seconds>` | 录音时长（秒，0=无限） | `10` |
| `-T<type>` | 文件格式 | 0=WAV(默认), 1=Raw PCM |

**文件输出**: 若未指定 `-P` 参数，自动生成文件名格式：

```text
/data/record_[采样率]Hz_[声道数]ch_[位深]bit_YYYYMMDD_HH.MM.SS.wav
```

例如: `/data/record_48000Hz_2ch_16bit_20260315_14.30.52.wav`

**文件格式说明**：

- WAV 格式（`-T0`）：带文件头，包含音频参数信息
- Raw PCM 格式（`-T1`）：纯音频数据，无文件头
- 文件扩展名优先级高于 `-T` 参数

### 播放模式参数 (-m1)

| 参数 | 说明 | 常用值 |
| --- | --- | --- |
| `-u<usage>` | 音频用途类型 | 1=媒体, 2=通话, 14=游戏 |
| `-O<flag>` | 输出标志位 | 0=标准, 4=FAST低延迟 |
| `-P<path>` | 播放文件路径（默认 `/data/audio_test.wav`） | `-P/data/test.wav` |

**文件格式自动识别**：

- `.wav` → WAV 格式（仅支持标准 44 字节头 PCM）
- `.pcm`, `.raw` → Raw PCM 格式（需通过 `-r`、`-c`、`-f` 指定参数）

**注意**: ContentType 会根据 Usage 自动设置，无需手动指定。

### 参数设置模式 (-m100)

```bash
./audio_test_client -m100 <operation>,<usage>
```

**注意**：此模式默认编译时禁用（`audio_test_client.h` 中 `kEnableSetParams = false`）。如需使用，将该常量改为 `true` 并重新编译。

| 位置 | 参数 | 说明 |
| --- | --- | --- |
| 1 | operation | 1=open_source, 2=close_source |
| 2 | usage | 音频用途（见 Usage 枚举） |

**完整枚举值请使用 `-h` 参数查看**

## 故障排除

### 常见问题

#### 权限问题

```bash
# 错误: Permission denied
adb root && adb remount
adb shell setenforce 0
```

### 调试日志

```bash
adb logcat -s audio_test_client AudioFlinger AudioPolicyService
```

### 运行时监控

- **进度显示**: 每10秒报告已处理的数据量和时长
- **电平表**: 实时显示音频电平（dB），支持 16/24/32 位 PCM
- **中断处理**: `Ctrl+C` 安全停止，已录制数据自动保存并更新 WAV 文件头

## 相关项目

- [AudioTester](https://github.com/kainan-tek/AudioTester) - 基于 AudioTrack/AudioRecord 的音频测试工具
- [AAudioTester](https://github.com/kainan-tek/AAudioTester) - 基于 AAudio 的音频测试工具

## 许可证

本项目采用 GNU General Public License v3.0 许可证。详细信息请参阅 [LICENSE](LICENSE) 文件。

**注意**: 本工具专为 Android 系统级音频开发和测试设计，需要系统级权限。请确保在合适的测试环境中使用。

## 联系方式

- **作者**: kainan-tek
- **邮箱**: <kainanos@outlook.com>
- **GitHub**: <https://github.com/kainan-tek/audio_test_client>

---

**如果这个项目对你有帮助，请给个 ⭐ Star！**

[⬆ 回到顶部](#audio-test-client)
