# Audio Test Client

Android 音频录制/播放测试工具，支持三种工作模式。

**版本:** 2.0.0

## 功能特性

| 模式 | 参数 | 说明 |
|-----|------|------|
| 录音 | `-m0` | 从指定音频源录制到 WAV 文件 |
| 播放 | `-m1` | 播放 WAV 音频文件 |
| 回环 | `-m2` | 同时录音和播放（回声测试） |
| 设置参数 | `-m100` | 设置音频系统参数（open_source/close_source） |

## 快速开始

```bash
# 编译
mm audio_test_client

# 推送到设备
adb root
adb remount
adb push audio_test_client /data/
adb shell
cd /data
chmod 777 /data/audio_test_client

# 使用示例
# 录音示例，-P 为可选参数，若不指定则自动生成文件名
./audio_test_client -m 0 -s 1 -r 48000 -c 2 -f 1 -I 1 -F 960 -d 20 -P /data/audio_test.wav
./audio_test_client -m 0 -s 1 -r 48000 -c 2 -f 1 -I 1 -z 960 -d 20

# 播放示例
./audio_test_client -m 1 -u 1 -C 0 -O 4 -F 960 -P /data/audio_test.wav

# 回环示例
./audio_test_client -m 2 -s 1 -r 48000 -c 2 -f 1 -I 1 -u 1 -C 0 -O 4 -F 960 -d 20 -P /data/audio_test.wav

# 设置参数示例（打开音频源）
./audio_test_client -m 100 1,1
```

## 命令行参数

```
audio_test_client -m<mode> [options] [audio_file]
```

### Common参数

| 参数 | 说明 | 默认值 |
|-----|------|-------|
| `-m<mode>` | 模式: 0=录音, 1=播放, 2=回环, 100=设置参数 | 必填 |
| `-F<frames>` | 最小帧数 | 系统自动 |
| `-P<filePath>` | 音频文件路径（播放模式为输入，录音/回环模式为输出） | - |
| `-h` | 显示帮助信息 | - |

### 录音参数 (-m0)

| 参数 | 说明 |
|-----|------|
| `-s<source>` | 音频源（见下方枚举） |
| `-r<rate>` | 采样率（8000/16000/48000） |
| `-c<count>` | 通道数（1/2/4/8） |
| `-f<format>` | 格式：1=PCM16, 2=PCM8, 3=PCM32 |
| `-I<flag>` | 输入标志 |
| `-d<seconds>` | 录音时长（0=无限） |

### 播放参数 (-m1)

| 参数 | 说明 |
|-----|------|
| `-u<usage>` | 用途类型（见下方枚举） |
| `-C<type>` | 内容类型 |
| `-O<flag>` | 输出标志 |

### 参数设置模式 (-m100)

参数设置模式支持逗号分隔的多参数格式，格式为：`./audio_test_client -m100 param1,param2[,param3,...]`

| 参数位置 | 说明 |
|---------|------|
| param1 | 主要参数：1=open_source, 2=close_source |
| param2 | 音频用途参数：1=AUDIO_USAGE_MEDIA, 2=AUDIO_USAGE_VOICE_COMMUNICATION, ... |
| param3+ | 预留参数，可用于扩展功能 |

### 常用枚举值

**音频源 (`-s`)**

| 值 | 说明 |
|---|------|
| 0 | 默认 |
| 1 | 麦克风 |
| 6 | 语音识别 |
| 7 | 通话 |

**用途类型 (`-u`)**

| 值 | 说明 |
|---|------|
| 1 | 媒体 (AUDIO_USAGE_MEDIA) |
| 2 | 通话 (AUDIO_USAGE_VOICE_COMMUNICATION) |
| 3 | 通话信号 (AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING) |
| 4 | 闹钟 (AUDIO_USAGE_ALARM) |
| 5 | 通知 (AUDIO_USAGE_NOTIFICATION) |
| 6 | 电话铃声 (AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE) |
| 7 | 通信请求 (AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST) |
| 8 | 即时通信 (AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT) |
| 9 | 延迟通信 (AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED) |
| 10 | 事件通知 (AUDIO_USAGE_NOTIFICATION_EVENT) |
| 11 | 无障碍服务 (AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY) |
| 12 | 导航引导 (AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE) |
| 13 | 声音反馈 (AUDIO_USAGE_ASSISTANCE_SONIFICATION) |
| 14 | 游戏 (AUDIO_USAGE_GAME) |
| 15 | 语音助手 (AUDIO_USAGE_ASSISTANT) |

## 注意事项

1. 设备需 root 权限
2. 可选：`adb shell setenforce 0` 关闭 SELinux
3. 完整参数列表运行：`./audio_test_client -h`
4. 录音文件自动保存到 `/data/` 目录
5. 最大录音文件限制：2 GiB
6. 按 `Ctrl+C` 可安全终止程序

## 代码架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        Utility Classes                          │
├─────────────────────────────────────────────────────────────────┤
│  WAVFile               - WAV File I/O Management                │
│  BufferManager         - Buffer Management                      │
│  AudioUtils            - Audio Utility Functions                │
│  CommandLineParser     - Command Line Parser                    │
│  AudioOperationFactory - Audio Operation Factory                │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     Configuration & Parameter Management        │
├─────────────────────────────────────────────────────────────────┤
│  struct AudioConfig                                             │
│    - Common params: minFrameCount, filePath                     │
│    - Recording params: inputSource, sampleRate, channelCount,   │
│                        format, inputFlag, durationSeconds       │
│    - Playback params: usage, contentType, outputFlag            │
│    - Setting params: setParams (only for set params)            │
│                                                                 │
│  class AudioParameterManager                                    │
│    - setOpenSourceWithUsage() / setCloseSourceWithUsage()       │
│    - setChannelMask()                                           │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     Core Class Hierarchy                        │
├─────────────────────────────────────────────────────────────────┤
│                           AudioConfig                           │
│                              │                                  │
│                              ▼                                  │
│              ┌─────────────────────────────────┐                │
│              │     AudioOperation              │                │
│              │   (abstract base class)         │                │
│              │  - mConfig                      │                │
│              │  - mParamManager                │                │
│              │  + setupSignalHandler()         │                │
│              │  + execute() = 0                │                │
│              └─────────────────────────────────┘                │
│                               │                                 │
│           ┌────────────┬────────────┬────────────────┐          │
│           │            │            │                │          │
│           ▼            ▼            ▼                ▼          │
│   ┌────────────┐ ┌────────────┐ ┌──────────────┐ ┌────────────┐ │
│   │AudioRecord │ │AudioPlay   │ │AudioLoopback │ │SetParams   │ │
│   │Operation   │ │Operation   │ │Operation     │ │Operation   │ │
│   │Record Mode │ │Play Mode   │ │Loopback Mode │ │Params Mode │ │
│   └────────────┘ └────────────┘ └──────────────┘ └────────────┘ │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                        File Format                              │
├─────────────────────────────────────────────────────────────────┤
│  Recording Output: /data/audio_<timestamp>.wav                  │
│  File Format: RIFF WAVE (PCM)                                   │
│  Header Size: 44 bytes                                          │
│  Data Limit: max 2 GiB                                          │
└─────────────────────────────────────────────────────────────────┘
```

## 项目文件

```
audio_test_client/
├── audio_test_client.cpp    # main program
├── Android.mk               # Android Makefile
├── Android.bp               # Android.bp
└── README.md                # project description
```
