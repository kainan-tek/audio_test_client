# audio_test_client

This is a simple audio test client for Android devices. It can be used to record, playback, and duplex audio (simultaneously record and play).

steps to run:
1. Build with cmd: mm audio_test_client. Make sure you have the necessary environment
2. Push the generated binary into the device using adb push
3. Run the binary on the device using adb shell

## Usage
```bash
Example:
adb root
adb remount
adb push audio_test.wav /data/
adb push audio_test_client /data/
adb shell chmod 777 /data/audio_test_client
adb shell setenforce 0
adb shell
cd /data

// for recording
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480 -d30
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480 -d30 /data/audio_test.wav

// for playback
./audio_test_client -m1 -u5 -C0 -O4 -z480 /data/audio_test.wav

// for duplex (record and play simultaneously)
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480 -d30
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480 -d30 /data/audio_test.wav

// pull out recorded file
adb pull /data/audio_test.wav


Parameters:
Modes:
    -m0   Record mode
    -m1   Play mode
    -m2   Duplex mode (record and play simultaneously)

Record Options:
    -s<inputSource>     Set audio source
                        0: AUDIO_SOURCE_DEFAULT (Default audio source)
                        1: AUDIO_SOURCE_MIC (Microphone)
                        2: AUDIO_SOURCE_VOICE_UPLINK (Phone call uplink)
                        3: AUDIO_SOURCE_VOICE_DOWNLINK (Phone call downlink)
                        4: AUDIO_SOURCE_VOICE_CALL (Phone call bidirectional)
                        5: AUDIO_SOURCE_CAMCORDER (Video camera)
                        6: AUDIO_SOURCE_VOICE_RECOGNITION (Voice recognition)
                        7: AUDIO_SOURCE_VOICE_COMMUNICATION (Voice communication)
                        8: AUDIO_SOURCE_REMOTE_SUBMIX (Remote submix)
                        9: AUDIO_SOURCE_UNPROCESSED (Unprocessed audio)
                        10: AUDIO_SOURCE_VOICE_PERFORMANCE (Voice performance)
                        1997: AUDIO_SOURCE_ECHO_REFERENCE (Echo reference)
                        1998: AUDIO_SOURCE_FM_TUNER (FM tuner)
                        1999: AUDIO_SOURCE_HOTWORD (Hotword)
                        2000: AUDIO_SOURCE_ULTRASOUND (Ultrasound)
    -r<sampleRate>      Set sample rate (e.g., 8000, 16000, 48000)
    -c<channelCount>    Set channel count (1, 2, 4, 6, 8, 12, 16)
    -f<format>          Set audio format
                        0: AUDIO_FORMAT_DEFAULT (Default audio format)
                        1: AUDIO_FORMAT_PCM_16_BIT (16-bit PCM)
                        2: AUDIO_FORMAT_PCM_8_BIT (8-bit PCM)
                        3: AUDIO_FORMAT_PCM_32_BIT (32-bit PCM)
                        4: AUDIO_FORMAT_PCM_8_24_BIT (8-bit PCM with 24-bit padding)
                        5: AUDIO_FORMAT_PCM_FLOAT (32-bit floating-point PCM)
                        6: AUDIO_FORMAT_PCM_24_BIT_PACKED (24-bit packed PCM)
    -F<inputFlag>       Set audio input flag
                        0: AUDIO_INPUT_FLAG_NONE (No special input flag)
                        1: AUDIO_INPUT_FLAG_FAST (Fast input flag)
                        2: AUDIO_INPUT_FLAG_HW_HOTWORD (Hardware hotword input)
                        4: AUDIO_INPUT_FLAG_RAW (Raw audio input)
                        8: AUDIO_INPUT_FLAG_SYNC (Synchronous audio input)
                        16: AUDIO_INPUT_FLAG_MMAP_NOIRQ (MMAP input without IRQ)
                        32: AUDIO_INPUT_FLAG_VOIP_TX (VoIP transmission input)
                        64: AUDIO_INPUT_FLAG_HW_AV_SYNC (Hardware audio/visual sync input)
                        128: AUDIO_INPUT_FLAG_DIRECT (Direct audio input)
                        256: AUDIO_INPUT_FLAG_ULTRASOUND (Ultrasound input)
                        512: AUDIO_INPUT_FLAG_HOTWORD_TAP (Hotword tap input)
                        1024: AUDIO_INPUT_FLAG_HW_LOOKBACK (Hardware lookback input)
    -z<minFrameCount>   Set min frame count (default: system selected)
    -d<duration>        Set recording duration(s) (0 = unlimited)

Play Options:
    -u<usage>           Set audio usage
                        0: AUDIO_USAGE_UNKNOWN (Unknown audio usage)
                        1: AUDIO_USAGE_MEDIA (Media playback)
                        2: AUDIO_USAGE_VOICE_COMMUNICATION (Voice call)
                        3: AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING (Call signaling)
                        4: AUDIO_USAGE_ALARM (Alarm clock)
                        5: AUDIO_USAGE_NOTIFICATION (General notification)
                        6: AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE (Ringtone)
                        7: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST (Incoming call)
                        8: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT (Instant message)
                        9: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED (Delayed message)
                        10: AUDIO_USAGE_NOTIFICATION_EVENT (Event notification)
                        11: AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY (Accessibility)
                        12: AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE (Navigation)
                        13: AUDIO_USAGE_ASSISTANCE_SONIFICATION (System sonification)
                        14: AUDIO_USAGE_GAME (Game audio)
                        15: AUDIO_USAGE_VIRTUAL_SOURCE (Virtual source)
                        16: AUDIO_USAGE_ASSISTANT (Assistant)
                        17: AUDIO_USAGE_CALL_ASSISTANT (Call assistant)
                        1000: AUDIO_USAGE_EMERGENCY (Emergency)
                        1001: AUDIO_USAGE_SAFETY (Safety)
                        1002: AUDIO_USAGE_VEHICLE_STATUS (Vehicle status)
                        1003: AUDIO_USAGE_ANNOUNCEMENT (Announcement)
                        1004: AUDIO_USAGE_SPEAKER_CLEANUP (Speaker cleanup)
    -C<contentType>     Set content type
                        0: AUDIO_CONTENT_TYPE_UNKNOWN (Unknown content type)
                        1: AUDIO_CONTENT_TYPE_SPEECH (Speech)
                        2: AUDIO_CONTENT_TYPE_MUSIC (Music)
                        3: AUDIO_CONTENT_TYPE_MOVIE (Movie)
                        4: AUDIO_CONTENT_TYPE_SONIFICATION (Sonification)
                        1997: AUDIO_CONTENT_TYPE_ULTRASOUND (Ultrasound)
    -O<outputFlag>      Set audio output flag
                        0: AUDIO_OUTPUT_FLAG_NONE (No special output flag)
                        1: AUDIO_OUTPUT_FLAG_DIRECT (Direct audio output)
                        2: AUDIO_OUTPUT_FLAG_PRIMARY (Primary audio output)
                        4: AUDIO_OUTPUT_FLAG_FAST (Fast audio output)
                        8: AUDIO_OUTPUT_FLAG_DEEP_BUFFER (Deep buffer audio output)
                        16: AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD (Compress offload audio output)
                        32: AUDIO_OUTPUT_FLAG_NON_BLOCKING (Non-blocking audio output)
                        64: AUDIO_OUTPUT_FLAG_HW_AV_SYNC (Hardware audio/visual sync output)
                        128: AUDIO_OUTPUT_FLAG_TTS (Text-to-speech output)
                        256: AUDIO_OUTPUT_FLAG_RAW (Raw audio output)
                        512: AUDIO_OUTPUT_FLAG_SYNC (Synchronous audio output)
                        1024: AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO (IEC958 non-audio output)
                        8192: AUDIO_OUTPUT_FLAG_DIRECT_PCM (Direct PCM audio output)
                        16384: AUDIO_OUTPUT_FLAG_MMAP_NOIRQ (MMAP no IRQ audio output)
                        32768: AUDIO_OUTPUT_FLAG_VOIP_RX (VoIP receive audio output)
                        65536: AUDIO_OUTPUT_FLAG_INCALL_MUSIC (In-call music audio output)
                        131072: AUDIO_OUTPUT_FLAG_GAPLESS_OFFLOAD (Gapless offload audio output)
                        262144: AUDIO_OUTPUT_FLAG_SPATIALIZER (Spatializer audio output)
                        524288: AUDIO_OUTPUT_FLAG_ULTRASOUND (Ultrasound audio output)
                        1048576: AUDIO_OUTPUT_FLAG_BIT_PERFECT (Bit perfect audio output)
    -z<minFrameCount>   Set min frame count (default: system selected)

General Options:
    -h                  Show this help message

For more details, please refer to system/media/audio/include/system/audio-hal-enums.h
```
