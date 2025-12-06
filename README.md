# Audio Test Client

A powerful and flexible audio testing tool for Android devices, supporting recording, playback, and duplex audio operations.

## Features

- **Audio Recording**: Record audio from various sources with configurable parameters
- **Audio Playback**: Play back WAV files with flexible settings
- **Duplex Audio**: Simultaneously record and play audio for loopback testing
- **Configurable Parameters**: Support for custom sample rates, channel counts, and audio formats
- **Real-time Level Meter**: Display audio levels during recording and playback
- **Signal Handling**: Safe termination with Ctrl+C, ensuring proper resource cleanup
- **Comprehensive Error Handling**: Detailed error messages and logging

## Prerequisites

- Android device with root access
- Android build environment set up
- ADB tools installed on your development machine

## Build and Installation

### Build the Binary

```bash
# Build the project using Android build system
mm audio_test_client
```

### Install on Device

```bash
# Push the binary to the device
adb push audio_test_client /data/

# Set executable permissions
adb shell chmod 777 /data/audio_test_client

# Optional: Disable SELinux enforcement if needed
adb shell setenforce 0
```

## Usage

### Command Line Syntax

```bash
audio_test_client -m<mode> [options] [output_file]
```

### Modes

| Mode | Description |
|------|-------------|
| `-m0` | Record mode |
| `-m1` | Playback mode |
| `-m2` | Duplex mode (simultaneous record and play) |

### Common Options

| Option | Description |
|--------|-------------|
| `-z<minFrameCount>` | Minimum frame count (default: system selected) |
| `-h` | Show help message |

### Recording Options

| Option | Description |
|--------|-------------|
| `-s<inputSource>` | Audio source (see below for details) |
| `-r<sampleRate>` | Sample rate (e.g., 8000, 16000, 48000) |
| `-c<channelCount>` | Channel count (1, 2, 4, 6, 8, 12, 16) |
| `-f<format>` | Audio format (see below for details) |
| `-F<inputFlag>` | Audio input flag (see below for details) |
| `-d<duration>` | Recording duration in seconds (0 = unlimited) |

### Playback Options

| Option | Description |
|--------|-------------|
| `-u<usage>` | Audio usage type (see below for details) |
| `-C<contentType>` | Audio content type (see below for details) |
| `-O<outputFlag>` | Audio output flag (see below for details) |

### Audio Sources (`-s`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_SOURCE_DEFAULT |
| 1 | AUDIO_SOURCE_MIC (Microphone) |
| 2 | AUDIO_SOURCE_VOICE_UPLINK |
| 3 | AUDIO_SOURCE_VOICE_DOWNLINK |
| 4 | AUDIO_SOURCE_VOICE_CALL |
| 5 | AUDIO_SOURCE_CAMCORDER |
| 6 | AUDIO_SOURCE_VOICE_RECOGNITION |
| 7 | AUDIO_SOURCE_VOICE_COMMUNICATION |
| 8 | AUDIO_SOURCE_REMOTE_SUBMIX |
| 9 | AUDIO_SOURCE_UNPROCESSED |
| 10 | AUDIO_SOURCE_VOICE_PERFORMANCE |
| 1997 | AUDIO_SOURCE_ECHO_REFERENCE |
| 1998 | AUDIO_SOURCE_FM_TUNER |
| 1999 | AUDIO_SOURCE_HOTWORD |
| 2000 | AUDIO_SOURCE_ULTRASOUND |

### Audio Formats (`-f`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_FORMAT_DEFAULT |
| 1 | AUDIO_FORMAT_PCM_16_BIT |
| 2 | AUDIO_FORMAT_PCM_8_BIT |
| 3 | AUDIO_FORMAT_PCM_32_BIT |
| 4 | AUDIO_FORMAT_PCM_8_24_BIT |
| 5 | AUDIO_FORMAT_PCM_FLOAT |
| 6 | AUDIO_FORMAT_PCM_24_BIT_PACKED |

### Input Flags (`-F`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_INPUT_FLAG_NONE |
| 1 | AUDIO_INPUT_FLAG_FAST |
| 2 | AUDIO_INPUT_FLAG_HW_HOTWORD |
| 4 | AUDIO_INPUT_FLAG_RAW |
| 8 | AUDIO_INPUT_FLAG_SYNC |
| 16 | AUDIO_INPUT_FLAG_MMAP_NOIRQ |
| 32 | AUDIO_INPUT_FLAG_VOIP_TX |
| 64 | AUDIO_INPUT_FLAG_HW_AV_SYNC |
| 128 | AUDIO_INPUT_FLAG_DIRECT |
| 256 | AUDIO_INPUT_FLAG_ULTRASOUND |
| 512 | AUDIO_INPUT_FLAG_HOTWORD_TAP |
| 1024 | AUDIO_INPUT_FLAG_HW_LOOKBACK |

### Usage Types (`-u`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_USAGE_UNKNOWN |
| 1 | AUDIO_USAGE_MEDIA |
| 2 | AUDIO_USAGE_VOICE_COMMUNICATION |
| 3 | AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING |
| 4 | AUDIO_USAGE_ALARM |
| 5 | AUDIO_USAGE_NOTIFICATION |
| 6 | AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE |
| 7 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST |
| 8 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT |
| 9 | AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED |
| 10 | AUDIO_USAGE_NOTIFICATION_EVENT |
| 11 | AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY |
| 12 | AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE |
| 13 | AUDIO_USAGE_ASSISTANCE_SONIFICATION |
| 14 | AUDIO_USAGE_GAME |
| 15 | AUDIO_USAGE_VIRTUAL_SOURCE |
| 16 | AUDIO_USAGE_ASSISTANT |
| 17 | AUDIO_USAGE_CALL_ASSISTANT |
| 1000 | AUDIO_USAGE_EMERGENCY |
| 1001 | AUDIO_USAGE_SAFETY |
| 1002 | AUDIO_USAGE_VEHICLE_STATUS |
| 1003 | AUDIO_USAGE_ANNOUNCEMENT |
| 1004 | AUDIO_USAGE_SPEAKER_CLEANUP |

### Content Types (`-C`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_CONTENT_TYPE_UNKNOWN |
| 1 | AUDIO_CONTENT_TYPE_SPEECH |
| 2 | AUDIO_CONTENT_TYPE_MUSIC |
| 3 | AUDIO_CONTENT_TYPE_MOVIE |
| 4 | AUDIO_CONTENT_TYPE_SONIFICATION |
| 1997 | AUDIO_CONTENT_TYPE_ULTRASOUND |

### Output Flags (`-O`) 

| Value | Description |
|-------|-------------|
| 0 | AUDIO_OUTPUT_FLAG_NONE |
| 1 | AUDIO_OUTPUT_FLAG_DIRECT |
| 2 | AUDIO_OUTPUT_FLAG_PRIMARY |
| 4 | AUDIO_OUTPUT_FLAG_FAST |
| 8 | AUDIO_OUTPUT_FLAG_DEEP_BUFFER |
| 16 | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD |
| 32 | AUDIO_OUTPUT_FLAG_NON_BLOCKING |
| 64 | AUDIO_OUTPUT_FLAG_HW_AV_SYNC |
| 128 | AUDIO_OUTPUT_FLAG_TTS |
| 256 | AUDIO_OUTPUT_FLAG_RAW |
| 512 | AUDIO_OUTPUT_FLAG_SYNC |
| 1024 | AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO |
| 8192 | AUDIO_OUTPUT_FLAG_DIRECT_PCM |
| 16384 | AUDIO_OUTPUT_FLAG_MMAP_NOIRQ |
| 32768 | AUDIO_OUTPUT_FLAG_VOIP_RX |
| 65536 | AUDIO_OUTPUT_FLAG_INCALL_MUSIC |
| 131072 | AUDIO_OUTPUT_FLAG_GAPLESS_OFFLOAD |
| 262144 | AUDIO_OUTPUT_FLAG_SPATIALIZER |
| 524288 | AUDIO_OUTPUT_FLAG_ULTRASOUND |
| 1048576 | AUDIO_OUTPUT_FLAG_BIT_PERFECT |

## Examples

### Recording

```bash
# Record for 30 seconds at 48000 Hz, 2 channels, 16-bit, none input flag, 960 frame size, and save to auto-generated file
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z960 -d30

# Record for 30 seconds at 48000 Hz, 2 channels, 16-bit, none input flag, 960 frame size, and save to specified file
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z960 -d30 /data/audio_test.wav
```

### Playback

```bash
# Play audio with default file
./audio_test_client -m1 -u1 -C0 -O4 -z960

# Play audio with specified file
./audio_test_client -m1 -u1 -C0 -O4 -z960 /data/audio_test.wav
```

### Duplex Mode

```bash
# Duplex audio for 30 seconds with auto-generated recorded file
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u1 -C0 -O4 -z960 -d30

# Duplex audio for 30 seconds with specified recorded file
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u1 -C0 -O4 -z960 -d30 /data/audio_test.wav
```

## File Management

### Pull Recorded Files

```bash
# Pull recorded files from device to host
adb pull /data/audio_test.wav
```

### Push Files for Playback

```bash
# Push WAV files from host to device
adb push audio_test.wav /data/
```

## Technical Details

- **Default Output Directory**: `/data/`
- **File Format**: WAV (RIFF)
- **Maximum Recording Size**: 2 GiB
- **Signal Handling**: Uses atomic flags for safe termination
- **Resource Management**: Proper cleanup of AudioRecord/AudioTrack instances

## Troubleshooting

### Common Issues

1. **Permission Denied**: Ensure the binary has executable permissions and SELinux is properly configured
2. **Device Not Found**: Verify ADB connection and device is in debug mode
3. **Audio Source Not Available**: Some audio sources may not be supported on all devices
4. **File Write Error**: Check if the output directory exists and is writable

### Debugging

- Check logcat for detailed error messages
- Use `-h` to see all available options
- Verify audio device availability with `adb shell dumpsys media.audio_flinger`

## License

This project is licensed under the Apache License 2.0 - see the LICENSE file for details.
