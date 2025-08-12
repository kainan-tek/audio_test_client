# audio_test_client

This is a simple audio test client for Android devices. It can be used to record, playback, and loopback audio.

steps to run:
1. Build the project using mm or mmm command. Make sure you have the necessary environment
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
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480
./audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480 /data/audio_test.wav

// for playback
./audio_test_client -m1 -u5 -C0 -O4 -z480 /data/audio_test.wav

// for loopback
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480
./audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480 /data/audio_test.wav

// pull out recorded file
adb pull /data/audio_test.wav


Parameters:
  -m <mode>            : mode (0 for record, 1 for playback, 2 for loopback)

  // for recording
  -s <source>          : input source (0 for default, 1 for mic ...)
  -r <sample rate>     : sample rate in Hz (16000, 48000 ...)
  -c <channels>        : number of channels (1 for mono, 2 for stereo ...)
  -f <format>          : audio format (1 for PCM 16-bit, 3 for PCM 32-bit ...)
  -F <input flag>      : input flag (0 for normal, 1 for fast ...)

  // for playback
  -u <usage>           : usage (0 for normal, 1 for media ...)
  -C <content type>    : content type (0 for unknown, 2 for music ...)
  -O <output flag>     : output flag (0 for normal, 4 for fast ...)

  // for both
  -z <min framecount>  : min frame count (e.g., 480, 960, 1920, 3840)

  -h                   : help (print this message and exit)
```
