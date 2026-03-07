# Audio Test Client

[中文文档](README.md) | English

Professional Android system-level audio testing tool based on Native C++, supporting recording, playback, loopback testing, and parameter configuration.

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Parameter Reference](#parameter-reference)
- [Troubleshooting](#troubleshooting)
- [Development Guide](#development-guide)
- [License](#license)

## Introduction

Audio Test Client is an Android system-level audio testing tool based on Android AudioRecord and AudioTrack Native APIs.

### Key Features

- **🎵 Four Operation Modes**: Recording, playback, loopback testing, parameter setting
- **🔊 Complete Audio Support**: 1-16 channels, 8kHz-192kHz sample rates, 8/16/24/32-bit PCM
- **📱 Multi-format File Support**: WAV and Raw PCM format read/write with automatic format detection
- **🔧 Smart Configuration**: ContentType auto-mapping based on Usage
- **⚡ Dual-thread Loopback**: Producer-consumer architecture for reduced latency
- **🏗️ Modular Design**: Clear class hierarchy and factory pattern

### Operation Modes

| Mode | Parameter | Description | Use Cases |
|------|-----------|-------------|-----------|
| Record | `-m0` | Record from specified audio source to file | Audio capture, quality testing |
| Playback | `-m1` | Play audio file | Audio output testing, compatibility verification |
| Loopback | `-m2` | Simultaneous recording and playback (real-time echo test) | Latency testing, audio chain verification |
| Set Parameters | `-m100` | Configure audio system parameters | System tuning, parameter verification |

## Quick Start

### Basic Syntax

```bash
audio_test_client -m<mode> [options] [audio_file]
```

### Common Commands

#### Recording Test
```bash
# Record 48kHz stereo audio for 20 seconds using microphone
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d20

# Record as Raw PCM format
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -d20 -T1
```

#### Playback Test
```bash
# Play WAV file
./audio_test_client -m1 -u1 -P/data/audio_test.wav

# Play Raw PCM file (parameters required)
./audio_test_client -m1 -u1 -P/data/audio.pcm -r48000 -c2 -f1
```

#### Loopback Latency Test
```bash
# Simultaneous recording and playback to test audio latency
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -u1 -d20
```

#### System Parameter Configuration
```bash
# Open audio source (media playback)
./audio_test_client -m100 1,1

# Close audio source
./audio_test_client -m100 2,1
```

**Tip**: Use `-h` parameter to view complete help information and all parameter options.

## Installation

### Environment Preparation

```bash
# Ensure device is rooted and debugging is enabled
adb root
adb remount

# Optional: Disable SELinux (if permission issues occur)
adb shell setenforce 0
```

### Build Methods

#### Using Android.mk (Recommended)
```bash
# Build in Android source environment
mm audio_test_client

# Push to device
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

#### Using Android.bp
```bash
# Use Soong build system
m audio_test_client

# Push to device
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

**Note**: This project depends on Android system libraries (libmedia, libaudioclient, libbinder, etc.) and must be built within the Android source tree environment.

### Permission Setup

```bash
adb shell
cd /data
chmod 755 audio_test_client
```

## Parameter Reference

### Common Parameters

| Parameter | Description | Example |
|-----------|-------------|---------|
| `-m<mode>` | Operation mode: 0=record, 1=playback, 2=loopback, 100=set params | `-m0` |
| `-F<frames>` | Minimum frame buffer size (default: auto) | `-F960` |
| `-P<path>` | Audio file path | `-P/data/test.wav` |
| `-h` | Display detailed help information | `-h` |

### Recording Mode Parameters (-m0)

| Parameter | Description | Common Values |
|-----------|-------------|---------------|
| `-s<source>` | Audio input source | 1=mic, 6=voice recognition, 7=voice communication |
| `-r<rate>` | Sample rate (Hz) | 8000, 16000, 48000 |
| `-c<count>` | Channel count | 1, 2 |
| `-f<format>` | Audio format | 1=PCM16, 3=PCM32, 6=PCM24_PACKED |
| `-I<flag>` | Input flags | 0=standard, 1=low latency |
| `-d<seconds>` | Recording duration (seconds, 0=infinite) | `10` |
| `-T<type>` | File format | 0=WAV(default), 1=Raw PCM |

**File Format Notes**:
- WAV format (`-T0`): With header, contains audio parameter info
- Raw PCM format (`-T1`): Pure audio data, no header
- File extension takes priority over `-T` parameter

### Playback Mode Parameters (-m1)

| Parameter | Description | Common Values |
|-----------|-------------|---------------|
| `-u<usage>` | Audio usage type | 1=media, 2=call, 14=game |
| `-O<flag>` | Output flags | 0=standard, 4=low latency |
| `-P<path>` | Playback file path (required) | `-P/data/test.wav` |

**File Format Auto-Detection**:
- `.wav` → WAV format
- `.pcm`, `.raw` → Raw PCM format (requires `-r`, `-c`, `-f` parameters)

**Note**: ContentType is automatically set based on Usage, no manual specification needed.

### Set Parameters Mode (-m100)

```bash
./audio_test_client -m100 <operation>,<usage>
```

| Position | Parameter | Description |
|----------|-----------|-------------|
| 1 | operation | 1=open_source, 2=close_source |
| 2 | usage | Audio usage (see Usage enum) |

### Common Enumeration Values

#### Audio Source

| Value | Constant Name | Description | Use Case |
|-------|---------------|-------------|----------|
| 1 | AUDIO_SOURCE_MIC | Main microphone | Voice recording |
| 6 | AUDIO_SOURCE_VOICE_RECOGNITION | Voice recognition | ASR applications |
| 7 | AUDIO_SOURCE_VOICE_COMMUNICATION | Voice communication | VoIP applications |

**For complete enumeration values, use `-h` parameter**

#### Audio Usage Type

| Value | Constant Name | Description | Auto-Mapped ContentType |
|-------|---------------|-------------|------------------------|
| 1 | AUDIO_USAGE_MEDIA | Media playback | CONTENT_TYPE_MUSIC |
| 2 | AUDIO_USAGE_VOICE_COMMUNICATION | Voice communication | CONTENT_TYPE_SPEECH |
| 14 | AUDIO_USAGE_GAME | Game | CONTENT_TYPE_MUSIC |

**For complete enumeration values, use `-h` parameter**

#### Input/Output Flags

| Value | Input Flag | Output Flag | Description |
|-------|-----------|-------------|-------------|
| 0 | NONE | NONE | Standard latency (~40-80ms) |
| 1 | FAST | FAST | Low latency (~10-20ms) |
| 8 | SYNC | DEEP_BUFFER | Sync/power-saving mode |

## Troubleshooting

### Common Issues

#### 1. Permission Issues
```bash
# Error: Permission denied
adb root && adb remount
adb shell setenforce 0
chmod 755 /data/audio_test_client
```

#### 2. Audio Device Busy
```bash
# Error: AudioRecord/AudioTrack initialization failed
adb shell stop audioserver
adb shell start audioserver
```

#### 3. File Write Failure
```bash
# Error: Failed to create WAV file
adb shell df /data  # Check disk space
```

### Debug Mode

```bash
# View real-time logs
adb logcat -s audio_test_client

# View audio system logs
adb logcat -s AudioFlinger AudioPolicyService
```

## Development Guide

### Build Requirements

- **Android SDK**: API Level 21+
- **C++ Standard**: C++17
- **Build System**: Android.mk (Recommended) / Android.bp

### Platform Version Compatibility

**Android.mk Method (Recommended)**:
- Automatically detects `PLATFORM_VERSION` (14/15/16)
- Automatically adapts to interface differences across Android versions

**Android.bp Method**:
- Defaults to Android 14+ build
- Requires manual config modification to support older versions

### Code Structure

- `audio_test_client.h` - Class declarations and interface definitions
- `audio_test_client.cpp` - Core functionality implementation
- `Android.mk` - Make build configuration (recommended)
- `Android.bp` - Soong build configuration

### Extension Development

#### Adding New Operation Modes
1. Inherit from `AudioOperation` base class
2. Implement `execute()` pure virtual function
3. Register new mode in `AudioOperationFactory`
4. Update command line parser and help information

## Performance Metrics

- **Low Latency Mode**: ~10-20ms (using FAST flag)
- **Standard Mode**: ~40-80ms
- **Deep Buffer**: ~80-200ms (power saving mode)
- **Sample Rate**: 8kHz - 192kHz
- **Channel Count**: 1-16 channels
- **Maximum File**: 2GB WAV file

## 🔗 Related Projects

- [**AudioRecorder**](https://github.com/kainan-tek/AudioRecorder) - Audio recorder based on AudioRecord API
- [**AAudioRecorder**](https://github.com/kainan-tek/AAudioRecorder) - High-performance recorder based on AAudio API
- [**AudioPlayer**](https://github.com/kainan-tek/AudioPlayer) - Audio player based on AudioTrack API
- [**AAudioPlayer**](https://github.com/kainan-tek/AAudioPlayer) - High-performance player based on AAudio API

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

---

**Note**: This tool is designed for Android system-level audio development and testing, requiring system-level permissions. Please ensure use in appropriate testing environments.
