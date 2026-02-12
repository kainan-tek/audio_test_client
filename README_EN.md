# Audio Test Client

[中文文档](README.md) | English

Professional Android system-level audio testing tool based on Native C++ development, supporting multiple audio operation modes and parameter configurations.

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Usage Guide](#usage-guide)
- [Parameter Reference](#parameter-reference)
- [Technical Architecture](#technical-architecture)
- [Performance Metrics](#performance-metrics)
- [Troubleshooting](#troubleshooting)
- [Development Guide](#development-guide)
- [License](#license)

## Overview

Audio Test Client is an Android system-level audio testing tool based on Android AudioRecord and AudioTrack Native APIs. This project provides complete audio recording, playback, loopback testing, and system parameter configuration capabilities, supporting various audio formats and configuration options. It's a professional tool for audio system development and testing.

### Operation Modes

| Mode | Parameter | Description | Use Cases |
|------|-----------|-------------|-----------|
| Record | `-m0` | Record from specified audio source to WAV file | Audio capture, quality testing, latency measurement |
| Playback | `-m1` | Play WAV audio file | Audio output testing, compatibility verification |
| Loopback | `-m2` | Simultaneous recording and playback (real-time echo test) | Latency testing, audio chain verification |
| Set Parameters | `-m100` | Configure audio system parameters | System tuning, parameter verification |

### Audio Format Support

- **Sample Rate Range**: 8kHz - 192kHz
- **Channel Configuration**: 1-16 channels
- **Bit Depth**: 8/16/24/32-bit PCM
- **File Format**: WAV (RIFF/WAVE)

## Key Features

- **🎵 Four Operation Modes**: Recording, playback, loopback testing, parameter setting
- **🔊 Complete Audio Support**: 1-16 channels, 8kHz-192kHz sample rates
- **🌟 Native Layer Implementation**: Based on C++17 and Android Native API
- **🔧 Smart Configuration**: Multiple audio sources, usages, formats with automatic contentType mapping
- **📱 WAV File Support**: Complete WAV file read/write functionality with PCM format support
- **🛠️ Real-time Monitoring**: Detailed audio stream status and performance information
- **🎯 Signal Processing**: Graceful signal handling mechanism with safe interruption
- **🏗️ Modular Design**: Clear class hierarchy and factory pattern

## System Requirements

- **Operating System**: Android (API Level 21+)
- **Permission Requirements**: Root access (recommended)
- **Development Environment**: Native development environment
- **Architecture Support**: ARM64, ARM32

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

#### Using Android.bp (Recommended)
```bash
# Use Soong build system
m audio_test_client

# Push to device
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

#### Using Android.mk (Traditional)
```bash
# Build in Android source environment
mm audio_test_client

# Push to device
adb push out/target/product/[device]/system/bin/audio_test_client /data/
```

#### Using CMake (Cross-platform)
```bash
mkdir build && cd build
cmake ..
make

# Push to device
adb push audio_test_client /data/
```

### Permission Setup

```bash
adb shell
cd /data
chmod 755 audio_test_client
```

## Usage Guide

### Basic Syntax

```bash
audio_test_client -m<mode> [options] [audio_file]
```

### Quick Start

#### Recording Test
```bash
# Record 48kHz stereo audio for 20 seconds using microphone
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20
```

#### Playback Test
```bash
# Play specified WAV file (contentType automatically set based on usage)
./audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav
```

#### Loopback Latency Test
```bash
# Simultaneous recording and playback to test audio latency (contentType auto-set)
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
```

#### System Parameter Configuration
```bash
# Open audio source and set usage to media playback
./audio_test_client -m100 1,1

# Close audio source
./audio_test_client -m100 2,1
```

## Parameter Reference

### Common Parameters

| Parameter | Type | Description | Default | Example |
|-----------|------|-------------|---------|---------|
| `-m<mode>` | int | Operation mode: 0=record, 1=playback, 2=loopback, 100=set params | Required | `-m0` |
| `-F<frames>` | int | Minimum frame buffer size | Auto | `-F960` |
| `-P<path>` | string | Audio file path | Auto-generated | `-P/data/test.wav` |
| `-h` | - | Display detailed help information | - | `-h` |

### Recording Mode Parameters (-m0)

| Parameter | Type | Description | Valid Values | Example |
|-----------|------|-------------|--------------|---------|
| `-s<source>` | int | Audio input source | See audio source enum table | `-s1` |
| `-r<rate>` | int | Sample rate (Hz) | 8000-192000 | `-r48000` |
| `-c<count>` | int | Channel count | 1-16 | `-c2` |
| `-f<format>` | int | Audio format | 1=PCM16, 2=PCM8, 3=PCM32 | `-f1` |
| `-I<flag>` | int | Input flags | See input flags enum table | `-I1` |
| `-d<seconds>` | int | Recording duration (seconds) | 0=infinite | `-d10` |

### Playback Mode Parameters (-m1)

| Parameter | Type | Description | Valid Values | Example |
|-----------|------|-------------|--------------|---------|
| `-u<usage>` | int | Audio usage type | See usage type enum table | `-u1` |
| `-O<flag>` | int | Output flags | See output flags enum table | `-O4` |

**Note**: ContentType is automatically set based on audio usage, no manual specification needed.

### Set Parameters Mode (-m100)

Supports comma-separated multi-parameter format:
```bash
./audio_test_client -m100 <operation>,<usage>[,<param3>,...]
```

| Position | Parameter | Description | Valid Values |
|----------|-----------|-------------|--------------|
| 1 | operation | Operation type | 1=open_source, 2=close_source |
| 2 | usage | Audio usage | See usage type enum table |
| 3+ | reserved | Reserved extension parameters | TBD |

### Enumeration Reference

#### Audio Source

| Value | Constant Name | Description | Use Case |
|-------|---------------|-------------|----------|
| 0 | AUDIO_SOURCE_DEFAULT | System default | General recording |
| 1 | AUDIO_SOURCE_MIC | Main microphone | Voice recording |
| 2 | AUDIO_SOURCE_VOICE_UPLINK | Call uplink | Call recording |
| 3 | AUDIO_SOURCE_VOICE_DOWNLINK | Call downlink | Call recording |
| 4 | AUDIO_SOURCE_VOICE_CALL | Bidirectional call | Call recording |
| 5 | AUDIO_SOURCE_CAMCORDER | Camera recording | Video recording |
| 6 | AUDIO_SOURCE_VOICE_RECOGNITION | Voice recognition | ASR applications |
| 7 | AUDIO_SOURCE_VOICE_COMMUNICATION | Voice communication | VoIP applications |
| 8 | AUDIO_SOURCE_REMOTE_SUBMIX | Remote submix | System audio capture |
| 9 | AUDIO_SOURCE_UNPROCESSED | Unprocessed audio | Raw audio capture |

#### Audio Usage Type

| Value | Constant Name | Description | Audio Characteristics |
|-------|---------------|-------------|----------------------|
| 1 | AUDIO_USAGE_MEDIA | Media playback | Music, video |
| 2 | AUDIO_USAGE_VOICE_COMMUNICATION | Voice communication | Calls, VoIP |
| 3 | AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING | Communication signaling | Dial tone, busy tone |
| 4 | AUDIO_USAGE_ALARM | Alarm | System alarm |
| 5 | AUDIO_USAGE_NOTIFICATION | Notification | System notification |
| 6 | AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE | Incoming ringtone | Phone ringtone |
| 7 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST | Communication request | Message alert |
| 8 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT | Instant communication | IM message |
| 9 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED | Delayed communication | Email alert |
| 10 | AUDIO_USAGE_NOTIFICATION_EVENT | Event notification | Calendar reminder |
| 11 | AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY | Accessibility | Assistive features |
| 12 | AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE | Navigation | GPS navigation |
| 13 | AUDIO_USAGE_ASSISTANCE_SONIFICATION | Sound feedback | Key sounds |
| 14 | AUDIO_USAGE_GAME | Game | Game sound effects |
| 15 | AUDIO_USAGE_ASSISTANT | Voice assistant | AI assistant |

#### Content Type Auto-Mapping

Content type is automatically set based on audio usage, no manual specification needed:

| Usage Type | Auto-Mapped Content Type | Description |
|------------|-------------------------|-------------|
| MEDIA, GAME, UNKNOWN | CONTENT_TYPE_MUSIC | Media and entertainment content |
| VOICE_COMMUNICATION, ASSISTANT, ACCESSIBILITY, NAVIGATION | CONTENT_TYPE_SPEECH | Voice and communication content |
| ALARM, NOTIFICATION, SONIFICATION, EMERGENCY | CONTENT_TYPE_SONIFICATION | System sounds and alerts |

#### Input Flags

| Value | Constant Name | Description | Performance Characteristics |
|-------|---------------|-------------|----------------------------|
| 0 | AUDIO_INPUT_FLAG_NONE | No special flags | Standard latency |
| 1 | AUDIO_INPUT_FLAG_FAST | Low latency recording | ~10-20ms |
| 2 | AUDIO_INPUT_FLAG_HW_HOTWORD | Hardware hotword detection | Low power |
| 4 | AUDIO_INPUT_FLAG_RAW | Raw audio input | Unprocessed |
| 8 | AUDIO_INPUT_FLAG_SYNC | Synchronized audio input | Precise sync |

#### Output Flags

| Value | Constant Name | Description | Performance Characteristics |
|-------|---------------|-------------|----------------------------|
| 0 | AUDIO_OUTPUT_FLAG_NONE | No special flags | Standard latency |
| 1 | AUDIO_OUTPUT_FLAG_DIRECT | Direct output | Bypass mixer |
| 2 | AUDIO_OUTPUT_FLAG_PRIMARY | Primary output | System main output |
| 4 | AUDIO_OUTPUT_FLAG_FAST | Low latency playback | ~10-20ms |
| 8 | AUDIO_OUTPUT_FLAG_DEEP_BUFFER | Deep buffer | Power saving mode |

## Technical Architecture

### Class Hierarchy

```
AudioOperation (Abstract Base Class)
├── AudioRecordOperation    (Recording)
├── AudioPlayOperation      (Playback)
├── AudioLoopbackOperation  (Loopback)
└── SetParamsOperation      (Parameter Setting)
```

### Core Components

#### 1. WAV File Management (WAVFile)
- Complete WAV file header parsing and generation
- PCM format read/write operations
- Automatic byte order and data alignment handling
- Large file support (up to 2GB)

#### 2. Buffer Management (BufferManager)
- Dynamic buffer allocation and management
- Memory safety protection mechanism
- Efficient data copy operations
- Smart size adjustment (480B - 64MB)

#### 3. Audio Utilities (AudioUtils)
- Audio format conversion and validation
- Sample rate and channel count calculation
- Audio parameter validity checking
- Timestamp and file path generation
- **Usage to StreamType auto-mapping** (New)
- **Usage to ContentType auto-mapping** (New)

#### 4. Parameter Manager (AudioParameterManager)
- System audio parameter configuration
- Channel mask settings
- Audio source on/off control
- Usage type string conversion

#### 5. Command Line Parser (CommandLineParser)
- Complete parameter parsing and validation
- Error handling and help information
- Parameter compatibility checking
- Multi-parameter format support

### Technology Stack

- **Language**: C++17
- **Audio API**: Android AudioRecord/AudioTrack Native API
- **Build System**: Android.bp (Soong) / Android.mk / CMake
- **Dependencies**: libmedia, libaudioclient, libutils, libbinder
- **Minimum Version**: Android API Level 21
- **Target Architecture**: ARM64, ARM32

### Data Flow Architecture

```
Recording: AudioRecord → BufferManager → WAVFile → Storage
Playback: Storage → WAVFile → BufferManager → AudioTrack
Loopback: AudioRecord → BufferManager → AudioTrack + WAVFile
```

### AudioRecord/AudioTrack Integration

- Synchronous transfer mode for audio operations
- Multiple audio format support (8/16/24/32-bit PCM)
- Complete error handling mechanism
- Audio focus and parameter management

### WAV File Support

- Standard RIFF/WAVE format parsing
- Multi-channel audio support (1-16 channels)
- Sample rate range: 8kHz - 192kHz
- Bit depth support: 8/16/24/32-bit
- Automatic file header update and integrity checking

## Performance Metrics

- **Low Latency Mode**: ~10-20ms (using FAST flag)
- **Standard Mode**: ~40-80ms
- **Deep Buffer**: ~80-200ms (power saving mode)
- **Sample Rate**: 8kHz - 192kHz
- **Channel Count**: 1-16 channels
- **Bit Depth**: 8/16/24/32-bit
- **Maximum File**: 2GB WAV file

## Troubleshooting

### Common Issues

#### 1. Permission Issues
```bash
# Error: Permission denied
# Solution:
adb root
adb remount
adb shell setenforce 0
chmod 755 /data/audio_test_client
```

#### 2. Audio Device Busy
```bash
# Error: AudioRecord/AudioTrack initialization failed
# Solution: Stop other audio apps or restart audio service
adb shell stop audioserver
adb shell start audioserver
```

#### 3. File Write Failure
```bash
# Error: Failed to create WAV file
# Solution: Check disk space and directory permissions
adb shell df /data
adb shell ls -la /data/
```

#### 4. Buffer Size Issues
```bash
# Error: Buffer allocation failed
# Solution: Adjust frame size
./audio_test_client -m0 -F480 -s1 -r48000 -c2 -f1
```

### Debug Mode

Enable verbose logging:
```bash
# Set log level
adb shell setprop log.tag.audio_test_client VERBOSE

# View real-time logs
adb logcat -s audio_test_client

# View audio system logs
adb logcat -s AudioFlinger AudioPolicyService
```

## Development Guide

### Build Requirements

- **Android SDK**: API Level 21+
- **NDK**: r21+
- **C++ Standard**: C++17
- **Build System**: Android.bp / Android.mk / CMake

### Dependencies

```cpp
// Core audio dependencies
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>
#include <media/AudioParameter.h>

// System utility dependencies
#include <utils/Log.h>
#include <utils/String8.h>
#include <binder/Binder.h>
```

### Extension Development

#### Adding New Audio Formats
1. Add format mapping in `AudioUtils::parseFormatOption()`
2. Update format support in `WAVFile::getAudioFormat()`
3. Modify command line parser validation logic

#### Adding New Operation Modes
1. Inherit from `AudioOperation` base class
2. Implement `execute()` pure virtual function
3. Register new mode in `AudioOperationFactory::createOperation()`
4. Update command line parser and help information

#### Performance Optimization Tips
- Use FAST flag to reduce latency
- Set buffer size appropriately
- Avoid file I/O in audio thread
- Use MMAP mode for better performance

### Code Structure

The project uses modular design with the following main files:

- `audio_test_client.cpp` - Main program file containing all core functionality
- `Android.bp` - Soong build configuration file
- `Android.mk` - Traditional Make build configuration file
- `CMakeLists.txt` - CMake build configuration file

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

---

**Note**: This tool is designed for Android system-level audio development and testing, requiring system-level permissions. Please ensure use in appropriate testing environments.
