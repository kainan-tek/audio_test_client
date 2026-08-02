# Audio Test Client

[中文文档](README.md) | English

Professional Android system-level audio testing tool designed for automotive AAOS audio development and testing, supporting recording, playback, loopback testing, and parameter configuration.

## Table of Contents

- [Introduction](#introduction)
- [Quick Start](#quick-start)
- [Installation](#installation)
- [Parameter Reference](#parameter-reference)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Introduction

Audio Test Client is an Android system-level audio testing tool based on Android AudioRecord and AudioTrack Native APIs.

- **Four Operation Modes**: Recording, playback, loopback testing, parameter setting
- **Complete Audio Support**: 1-16 channels, 8kHz-192kHz sample rates, 8/16/24/32-bit PCM
- **Multi-format File Support**: WAV and Raw PCM format read/write with automatic format detection
- **Smart Configuration**: ContentType auto-mapping based on Usage
- **Dual-thread Loopback**: Producer-consumer architecture prevents recording data loss

| Mode           | Parameter | Description                                               | Use Cases                                        |
|----------------|-----------|-----------------------------------------------------------|--------------------------------------------------|
| Record         | `-m0`     | Record from specified audio source to file                | Audio capture, quality testing                   |
| Playback       | `-m1`     | Play audio file                                           | Audio output testing, compatibility verification |
| Loopback       | `-m2`     | Simultaneous recording and playback (real-time echo test) | Latency testing, audio chain verification        |
| Set Parameters | `-m100`   | Configure audio system parameters                         | System tuning, parameter verification            |

## Quick Start

### Basic Syntax

```bash
audio_test_client -m<mode> [options]
```

### Common Commands

#### Recording Test

```bash
# Record 48kHz stereo audio for 20 seconds using microphone
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20

# Record as Raw PCM format
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20 -T1
```

#### Playback Test

```bash
# Play WAV file
./audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav

# Play Raw PCM file (parameters required)
./audio_test_client -m1 -u1 -O0 -F960 -P/data/audio.pcm -r48000 -c2 -f1
```

#### Loopback Latency Test

```bash
# Simultaneous recording and playback to test audio latency
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
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

# Push to device and set permissions
adb push out/target/product/[device]/system/bin/audio_test_client /data/
adb shell chmod 755 /data/audio_test_client
```

**Version Compatibility**: Android.mk automatically detects `PLATFORM_VERSION` (14/15/16) and adapts to interface differences across Android versions, no manual modification required.

#### Using Android.bp (Optional)

Android.mk is used by default. To switch to the Soong build, enable Android.bp and disable Android.mk first (both define the same module name and cannot coexist):

```bash
mv Android.bp_bk Android.bp      # Enable Android.bp
mv Android.mk Android.mk_bk      # Disable Android.mk
m audio_test_client
```

Push and permission settings are the same as for Android.mk.

**Version Compatibility**: Android.bp defaults to Android 14+ build, requires manual config modification to support other versions.

**Note**: This project depends on Android system libraries (libmedia, libaudioclient, libbinder, etc.) and must be built within the Android source tree environment.

## Parameter Reference

### Common Parameters

| Parameter    | Description                                                      | Example              |
| ------------ | ---------------------------------------------------------------- | -------------------- |
| `-m<mode>`   | Operation mode: 0=record, 1=playback, 2=loopback, 100=set params | `-m0`                |
| `-F<frames>` | Frame count (unspecified: FAST=20ms, Deep Buffer=system default) | `-F960` (20ms@48kHz) |
| `-P<path>`   | Audio file path                                                  | `-P/data/test.wav`   |
| `-h`         | Display detailed help information                                | `-h`                 |

### Recording Mode Parameters (-m0)

| Parameter     | Description                              | Common Values                                     |
|---------------|------------------------------------------|---------------------------------------------------|
| `-s<source>`  | Audio input source                       | 1=mic, 6=voice recognition, 7=voice communication |
| `-r<rate>`    | Sample rate (Hz)                         | 8000, 16000, 48000                                |
| `-c<count>`   | Channel count                            | 1, 2                                              |
| `-f<format>`  | Audio format                             | 1=PCM16, 3=PCM32, 6=PCM24_PACKED                  |
| `-I<flag>`    | Input flags                              | 0=standard, 1=FAST low latency                    |
| `-d<seconds>` | Recording duration (seconds, 0=infinite) | `10`                                              |
| `-T<type>`    | File format                              | 0=WAV(default), 1=Raw PCM                         |

**File Output**: If `-P` parameter is not specified, auto-generated filename format:

```text
/data/record_[SampleRate]Hz_[Channels]ch_[BitDepth]bit_YYYYMMDD_HH.MM.SS.wav
```

Example: `/data/record_48000Hz_2ch_16bit_20260315_14.30.52.wav`

**File Format Notes**:

- WAV format (`-T0`): With header, contains audio parameter info
- Raw PCM format (`-T1`): Pure audio data, no header
- File extension takes priority over `-T` parameter

### Playback Mode Parameters (-m1)

| Parameter   | Description                   | Common Values                  |
|-------------|-------------------------------|--------------------------------|
| `-u<usage>` | Audio usage type              | 1=media, 2=call, 14=game       |
| `-O<flag>`  | Output flags                  | 0=standard, 4=FAST low latency |
| `-P<path>`  | Playback file path (required) | `-P/data/test.wav`             |

**File Format Auto-Detection**:

- `.wav` → WAV format (standard 44-byte header PCM only)
- `.pcm`, `.raw` → Raw PCM format (requires `-r`, `-c`, `-f` parameters)

**Note**: ContentType is automatically set based on Usage, no manual specification needed.

### Set Parameters Mode (-m100)

```bash
./audio_test_client -m100 <operation>,<usage>
```

| Position | Parameter | Description                   |
|----------|-----------|-------------------------------|
| 1        | operation | 1=open_source, 2=close_source |
| 2        | usage     | Audio usage (see Usage enum)  |

**For complete enumeration values, use `-h` parameter**

## Troubleshooting

### Common Issues

#### Permission Issues

```bash
# Error: Permission denied
adb root && adb remount
adb shell setenforce 0
```

### Debug Logging

```bash
adb logcat -s audio_test_client AudioFlinger AudioPolicyService
```

### Runtime Monitoring

- **Progress**: Reports processed data size and duration every 10 seconds
- **Level Meter**: Real-time audio level display (dB), supports 16/24/32-bit PCM
- **Interrupt Handling**: `Ctrl+C` for safe stop, recorded data is automatically saved with updated WAV file header

## Related Projects

- [AudioRecorder](https://github.com/kainan-tek/AudioRecorder) - Audio recorder based on AudioRecord API
- [AAudioRecorder](https://github.com/kainan-tek/AAudioRecorder) - High-performance recorder based on AAudio API
- [AudioPlayer](https://github.com/kainan-tek/AudioPlayer) - Audio player based on AudioTrack API
- [AAudioPlayer](https://github.com/kainan-tek/AAudioPlayer) - High-performance player based on AAudio API

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](LICENSE) file for details.

**Note**: This tool is designed for Android system-level audio development and testing, requiring system-level permissions. Please ensure use in appropriate testing environments.

## Contact

- **Author**: kainan-tek
- **Email**: <kainanos@outlook.com>
- **GitHub**: <https://github.com/kainan-tek/audio_test_client>

---

**If this project helps you, please give it a ⭐ Star!**

[⬆ Back to top](#audio-test-client)
