# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目信息

- **项目名称**：Audio Test Client
- **项目描述**：Android 系统级音频测试工具，专为车载 AAOS 音频开发与测试设计，支持录音、播放、回环测试和参数配置
- **技术栈**：C++17; AAOS Audio; Android AudioRecord/AudioTrack Native API
- **源文件**：单文件架构 — `audio_test_client.h`（448行）+ `audio_test_client.cpp`（1562行）
- **许可证**：GPL v3.0
- **当前版本**：3.5.0（定义在 `audio_test_client.h` 的 `AUDIO_TEST_CLIENT_VERSION`）

## 构建与开发

### Android 源码树编译（唯一可生成可执行文件的方式）

本项目依赖 Android 系统库（libmedia、libaudioclient、libbinder 等），**必须在 Android 源码树环境中编译**。

```bash
# Android.mk（推荐，自动适配 PLATFORM_VERSION 14/15/16）
mm audio_test_client

# Android.bp（可选，Soong 构建，默认 ANDROID_API_14_PLUS，需手动适配旧版本）
# 以 Android.bp_bk 备份禁用（与 Android.mk 同名冲突）；启用：mv Android.bp_bk Android.bp && mv Android.mk Android.mk_bk
m audio_test_client
```

### 部署到设备

```bash
adb root && adb remount                    # 系统级权限
adb shell setenforce 0                     # 关闭 SELinux（如遇权限问题）
adb push out/target/product/[device]/system/bin/audio_test_client /data/
adb shell chmod 755 /data/audio_test_client
```

### 代码格式化

```bash
# clang-format（Google Style, 4空格缩进, 120列宽, 左对齐指针）
clang-format -i audio_test_client.cpp audio_test_client.h
```

### 调试

```bash
# 查看 audio_test_client 及 Android 音频子系统日志
adb logcat -s audio_test_client AudioFlinger AudioPolicyService
```

### 测试

测试目录 `tests/` 尚未创建。

## 架构概览

### 执行流程

`main()` → `CommandLineParser::parseArguments()` 解析参数 → `AudioOperationFactory::createOperation()` 创建操作 → 调用 `execute()`

### 类层次

```
AudioFileBase（抽象基类）
├── WavFile（WAV 读写，44字节标准头，4GB 限制检测）
└── RawPcmFile（纯 PCM 数据流，无文件头）

AudioOperation（抽象基类）
├── AudioRecordOperation（录音：AudioRecord → AudioFileBase 写入文件）
├── AudioPlayOperation（播放：AudioFileBase 读取文件 → AudioTrack 播放）
├── AudioLoopbackOperation（回环：双线程生产者-消费者，ThreadSafeBufferQueue 解耦）
└── SetParamsOperation（参数设置：AudioParameterManager 设置系统音频参数）

工具类：AudioUtils | AudioParameterManager | SignalGuard(RAII) | ThreadSafeBufferQueue(有界阻塞队列)
```

### 数据流

- **录音**：麦克风 → AudioRecord → AudioFileBase::writeData() → 文件
- **播放**：文件 → AudioFileBase::readData() → AudioTrack → 扬声器
- **回环**：麦克风 → AudioRecord → ThreadSafeBufferQueue → AudioTrack → 扬声器（同时写入文件，默认 WAV）

### 关键实现细节

- **Android 版本兼容**：`ANDROID_API_14_PLUS` 宏控制条件编译，Android 14+ 移除了 AudioRecord/AudioTrack 构造函数的 callback 参数。Android.mk 根据 `PLATFORM_VERSION` 自动检测；Android.bp 以 `Android.bp_bk` 备份（可选启用，默认启用该宏）。
- **`kEnableSetParams`**：编译时常量（当前为 `false`），控制 SetParamsOperation 是否可用。
- **WAV 4GB 限制**：`AudioOperation::kMaxAudioDataSize` 限制 WAV 数据不超过 `UINT32_MAX - 36` 字节。
- **Usage→ContentType 自动映射**：播放模式无需手动指定 ContentType，`AudioUtils::getUsageInfo()` 根据 Usage 自动推断。
- **文件格式检测**：优先按扩展名判断（`.wav`/`.pcm`/`.raw`），扩展名优先级高于 `-T` 参数。
- **CLI 语法**：`audio_test_client -m<mode> [options]`，模式：0=录音, 1=播放, 2=回环, 100=设置参数。使用 `-h` 查看完整帮助。

## 编码规范

- 命名遵循 Google C++ Style Guide
- 前向声明优先于 `#include`
- 构建目标命名加项目前缀，避免冲突
- 错误必须被处理，不允许静默忽略
