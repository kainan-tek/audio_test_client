// Copyright 2026 Audio Test Client Authors
//
// Licensed under the GNU General Public License v3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.gnu.org/licenses/gpl-3.0.html
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Audio Test Client - A professional Android system-level audio testing tool.
// This file contains the implementation of audio recording, playback,
// loopback testing, and system parameter configuration using Android
// AudioRecord and AudioTrack Native APIs.

// C system headers
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// C++ standard library headers
#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Other library headers
#include <binder/Binder.h>
#include <media/AudioParameter.h>
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include <utils/Log.h>
#include <utils/String8.h>

#define LOG_TAG "audio_test_client"

#define AUDIO_TEST_CLIENT_VERSION "2.4.0"
#define ENABLE_SET_PARAMS 0

/************************** WAV File Management ******************************/
class WAVFile {
public:
    WAVFile() = default;
    ~WAVFile() noexcept { close(); }

    WAVFile(const WAVFile&) = delete;
    WAVFile& operator=(const WAVFile&) = delete;
    WAVFile(WAVFile&&) = delete;
    WAVFile& operator=(WAVFile&&) = delete;

    struct Header {
        char riff_id[4];
        uint32_t riff_size;
        char wave_id[4];
        char fmt_id[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char data_id[4];
        uint32_t data_size;

        void write(std::ostream& out) const {
            out.write(riff_id, 4);
            out.write(reinterpret_cast<const char*>(&riff_size), 4);
            out.write(wave_id, 4);
            out.write(fmt_id, 4);
            out.write(reinterpret_cast<const char*>(&fmt_size), 4);
            out.write(reinterpret_cast<const char*>(&audio_format), 2);
            out.write(reinterpret_cast<const char*>(&num_channels), 2);
            out.write(reinterpret_cast<const char*>(&sample_rate), 4);
            out.write(reinterpret_cast<const char*>(&byte_rate), 4);
            out.write(reinterpret_cast<const char*>(&block_align), 2);
            out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
            out.write(data_id, 4);
            out.write(reinterpret_cast<const char*>(&data_size), 4);
        }

        void read(std::istream& in) {
            in.read(riff_id, 4);
            in.read(reinterpret_cast<char*>(&riff_size), 4);
            in.read(wave_id, 4);
            in.read(fmt_id, 4);
            in.read(reinterpret_cast<char*>(&fmt_size), 4);
            in.read(reinterpret_cast<char*>(&audio_format), 2);
            in.read(reinterpret_cast<char*>(&num_channels), 2);
            in.read(reinterpret_cast<char*>(&sample_rate), 4);
            in.read(reinterpret_cast<char*>(&byte_rate), 4);
            in.read(reinterpret_cast<char*>(&block_align), 2);
            in.read(reinterpret_cast<char*>(&bits_per_sample), 2);
            in.read(data_id, 4);
            in.read(reinterpret_cast<char*>(&data_size), 4);
        }

        void print() const {
            printf("RiffID: %.4s\n", riff_id);
            printf("RiffSize: %" PRIu32 "\n", riff_size);
            printf("WaveID: %.4s\n", wave_id);
            printf("FmtID: %.4s\n", fmt_id);
            printf("FmtSize: %" PRIu32 "\n", fmt_size);
            printf("AudioFormat: %u\n", static_cast<unsigned>(audio_format));
            printf("NumChannels: %u\n", static_cast<unsigned>(num_channels));
            printf("SampleRate: %" PRIu32 "\n", sample_rate);
            printf("ByteRate: %" PRIu32 "\n", byte_rate);
            printf("BlockAlign: %u\n", static_cast<unsigned>(block_align));
            printf("BitsPerSample: %u\n", static_cast<unsigned>(bits_per_sample));
            printf("DataID: %.4s\n", data_id);
            printf("DataSize: %" PRIu32 "\n", data_size);
        }
    };

    bool createForWriting(const std::string& file_path,
                          const uint32_t sample_rate,
                          const uint32_t num_channels,
                          const uint32_t bits_per_sample) {
        file_path_ = file_path;
        file_stream_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!file_stream_.is_open()) {
            return false;
        }

        memcpy(header_.riff_id, "RIFF", 4);
        memcpy(header_.wave_id, "WAVE", 4);
        memcpy(header_.fmt_id, "fmt ", 4);
        memcpy(header_.data_id, "data", 4);

        header_.fmt_size = 16;
        header_.audio_format = 1;
        header_.num_channels = num_channels;
        header_.sample_rate = sample_rate;
        header_.bits_per_sample = bits_per_sample;

        uint32_t bytes_per_sample = bits_per_sample / 8;
        header_.byte_rate = sample_rate * num_channels * bytes_per_sample;
        header_.block_align = num_channels * bytes_per_sample;
        header_.data_size = 0;
        header_.riff_size = 36;

        header_.write(file_stream_);
        if (!file_stream_.good()) {
            file_stream_.close();
            return false;
        }

        is_header_valid_ = true;
        data_size_pos_ = file_stream_.tellp() - std::streamoff(4);

        return file_stream_.good();
    }

    bool openForReading(const std::string& file_path) {
        file_path_ = file_path;
        file_stream_.open(file_path_, std::ios::binary | std::ios::in);
        if (!file_stream_.is_open()) {
            return false;
        }

        header_.read(file_stream_);
        if (strncmp(header_.riff_id, "RIFF", 4) != 0 || strncmp(header_.wave_id, "WAVE", 4) != 0 ||
            strncmp(header_.fmt_id, "fmt ", 4) != 0 || strncmp(header_.data_id, "data", 4) != 0) {
            file_stream_.close();
            return false;
        }
        if (header_.fmt_size < 16 || (header_.audio_format != 1 && header_.audio_format != 3) ||
            header_.num_channels == 0 || header_.sample_rate == 0) {
            file_stream_.close();
            return false;
        }

        is_header_valid_ = true;
        return file_stream_.good();
    }

    size_t writeData(const char* data, const size_t size) {
        if (!file_stream_.is_open() || !is_header_valid_ || !data || size == 0) {
            return 0;
        }

        if (header_.data_size > UINT32_MAX - size) {
            return 0;
        }

        file_stream_.write(data, size);
        if (file_stream_.good()) {
            header_.data_size += static_cast<uint32_t>(size);
            header_.riff_size = 36 + header_.data_size;
            return size;
        }
        return 0;
    }

    void updateHeader() {
        if (file_stream_.is_open() && is_header_valid_) {
            const auto current_pos = file_stream_.tellp();

            file_stream_.seekp(4, std::ios::beg);
            file_stream_.write(reinterpret_cast<const char*>(&header_.riff_size), sizeof(header_.riff_size));

            file_stream_.seekp(data_size_pos_);
            file_stream_.write(reinterpret_cast<const char*>(&header_.data_size), sizeof(header_.data_size));

            file_stream_.flush();
            file_stream_.seekp(current_pos);
        }
    }

    size_t readData(char* data, const size_t size) {
        if (!file_stream_.is_open() || !is_header_valid_) {
            return 0;
        }

        file_stream_.read(data, size);
        return static_cast<size_t>(file_stream_.gcount());
    }

    void finalize() {
        if (file_stream_.is_open() && is_header_valid_) {
            updateHeader();
            file_stream_.close();
        }
    }

    void close() {
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
    }

    const std::string& getFilePath() const { return file_path_; }
    const Header& getHeader() const { return header_; }
    int32_t getSampleRate() const { return header_.sample_rate; }
    int32_t getNumChannels() const { return header_.num_channels; }
    uint32_t getBitsPerSample() const { return header_.bits_per_sample; }
    audio_format_t getAudioFormat() const {
        if (header_.audio_format == 1) {
            switch (header_.bits_per_sample) {
            case 8:
                return AUDIO_FORMAT_PCM_8_BIT;
            case 16:
                return AUDIO_FORMAT_PCM_16_BIT;
            case 24:
                return AUDIO_FORMAT_PCM_24_BIT_PACKED;
            case 32:
                return AUDIO_FORMAT_PCM_32_BIT;
            default:
                return AUDIO_FORMAT_INVALID;
            }
        } else if (header_.audio_format == 3) {
            if (header_.bits_per_sample == 32) {
                return AUDIO_FORMAT_PCM_FLOAT;
            }
            return AUDIO_FORMAT_INVALID;
        }
        return AUDIO_FORMAT_INVALID;
    }

private:
    Header header_{};
    std::string file_path_;
    std::fstream file_stream_;
    bool is_header_valid_{false};
    std::streampos data_size_pos_{};
};

/************************** BufferManager class ******************************/
class BufferManager {
public:
    explicit BufferManager(size_t buffer_size) { initializeBuffer(buffer_size); }
    ~BufferManager() noexcept = default;

    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;
    BufferManager(BufferManager&&) = delete;
    BufferManager& operator=(BufferManager&&) = delete;

    char* get() const { return buffer_.get(); }
    size_t getSize() const { return size_; }
    bool isValid() const { return buffer_ != nullptr && size_ > 0; }

private:
    void initializeBuffer(size_t requested_size) {
        static constexpr size_t kMinBufferSize = 480;
        static constexpr size_t kMaxBufferSize = 64 * 1024 * 1024;
        const size_t validated_size = std::clamp(requested_size, kMinBufferSize, kMaxBufferSize);

        try {
            buffer_ = std::make_unique<char[]>(validated_size);
            size_ = validated_size;
            printf("BufferManager created with buffer size: %zu\n", size_);
        } catch (const std::bad_alloc& e) {
            buffer_.reset();
            size_ = 0;
            printf("Error: Failed to allocate buffer of size %zu: %s\n", validated_size, e.what());
        }
    }

    std::unique_ptr<char[]> buffer_;
    size_t size_{0};
};

/************************** Audio Utility Functions ******************************/
class AudioUtils {
private:
    // Private constructor to prevent instantiation - this is a utility class
    AudioUtils() = delete;

public:
    // Convert audio_usage_t to audio_stream_type_t for legacy API compatibility
    // Based on Android official compatibility mapping table:
    // https://source.android.com/devices/audio/attributes
    static audio_stream_type_t usageToStreamType(audio_usage_t usage) {
        switch (usage) {
        // -> STREAM_MUSIC
        case AUDIO_USAGE_UNKNOWN:
        case AUDIO_USAGE_MEDIA:
        case AUDIO_USAGE_GAME:
        case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
        case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
            return AUDIO_STREAM_MUSIC;

        // -> STREAM_VOICE_CALL
        case AUDIO_USAGE_VOICE_COMMUNICATION:
        case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
            return AUDIO_STREAM_VOICE_CALL;

        // -> STREAM_ALARM
        case AUDIO_USAGE_ALARM:
            return AUDIO_STREAM_ALARM;

        // -> STREAM_RING
        case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
            return AUDIO_STREAM_RING;

        // -> STREAM_NOTIFICATION
        case AUDIO_USAGE_NOTIFICATION:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
        case AUDIO_USAGE_NOTIFICATION_EVENT:
            return AUDIO_STREAM_NOTIFICATION;

        // -> STREAM_ASSISTANT
        case AUDIO_USAGE_ASSISTANT:
        case AUDIO_USAGE_CALL_ASSISTANT:
            return AUDIO_STREAM_ASSISTANT;

        // -> STREAM_SYSTEM (direct mapping)
        case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
            return AUDIO_STREAM_SYSTEM;

        // -> STREAM_MUSIC (semantic mapping, virtual source for media processing)
        case AUDIO_USAGE_VIRTUAL_SOURCE:
            printf("Warning: VIRTUAL_SOURCE usage mapped to STREAM_MUSIC (virtual audio processing)\n");
            return AUDIO_STREAM_MUSIC;

        // -> STREAM_SYSTEM (semantic mapping, automotive related)
        case AUDIO_USAGE_EMERGENCY:
        case AUDIO_USAGE_SAFETY:
        case AUDIO_USAGE_VEHICLE_STATUS:
        case AUDIO_USAGE_ANNOUNCEMENT:
            // case AUDIO_USAGE_SPEAKER_CLEANUP:
            printf("Warning: Usage %d has no direct stream type mapping, using STREAM_SYSTEM\n", usage);
            return AUDIO_STREAM_SYSTEM;

        // Default case
        default:
            printf("Warning: Unknown audio usage %d, defaulting to STREAM_MUSIC\n", usage);
            return AUDIO_STREAM_MUSIC;
        }
    }

    // Convert audio_usage_t to audio_content_type_t for proper audio attributes
    // Based on Android official audio attributes mapping:
    // https://source.android.com/devices/audio/attributes
    static audio_content_type_t usageToContentType(audio_usage_t usage) {
        switch (usage) {
        // -> CONTENT_TYPE_MUSIC (media and entertainment)
        case AUDIO_USAGE_UNKNOWN:
        case AUDIO_USAGE_MEDIA:
        case AUDIO_USAGE_GAME:
            return AUDIO_CONTENT_TYPE_MUSIC;

        // -> CONTENT_TYPE_SPEECH (voice communication and assistant)
        case AUDIO_USAGE_VOICE_COMMUNICATION:
        case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
        case AUDIO_USAGE_ASSISTANT:
        case AUDIO_USAGE_CALL_ASSISTANT:
        case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
        case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
            return AUDIO_CONTENT_TYPE_SPEECH;

        // -> CONTENT_TYPE_SONIFICATION (system sounds and notifications)
        case AUDIO_USAGE_ALARM:
        case AUDIO_USAGE_NOTIFICATION:
        case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
        case AUDIO_USAGE_NOTIFICATION_EVENT:
        case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
            return AUDIO_CONTENT_TYPE_SONIFICATION;

        // -> CONTENT_TYPE_SPEECH (virtual source, typically for processing)
        case AUDIO_USAGE_VIRTUAL_SOURCE:
            return AUDIO_CONTENT_TYPE_SPEECH;

        // -> CONTENT_TYPE_SONIFICATION (automotive and emergency sounds)
        case AUDIO_USAGE_EMERGENCY:
        case AUDIO_USAGE_SAFETY:
        case AUDIO_USAGE_VEHICLE_STATUS:
        case AUDIO_USAGE_ANNOUNCEMENT:
            // case AUDIO_USAGE_SPEAKER_CLEANUP:
            return AUDIO_CONTENT_TYPE_SONIFICATION;

        // Default case
        default:
            printf("Warning: Unknown audio usage %d, defaulting to CONTENT_TYPE_MUSIC\n", usage);
            return AUDIO_CONTENT_TYPE_MUSIC;
        }
    }

    // Parse format option value to audio_format_t enum
    static audio_format_t parseFormatOption(const int v) {
        switch (v) {
        case 1:
            return AUDIO_FORMAT_PCM_16_BIT;
        case 2:
            return AUDIO_FORMAT_PCM_8_BIT;
        case 3:
            return AUDIO_FORMAT_PCM_32_BIT;
        case 4:
            return AUDIO_FORMAT_PCM_8_24_BIT;
        case 6:
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        default:
            printf("Error: format %d not found, using default format 16bit\n", v);
            return AUDIO_FORMAT_PCM_16_BIT;
        }
    }

    // Get current time formatted as YYYYMMDD_HH.MM.SS
    static std::string getFormatTime() {
        time_t current_time = time(nullptr);
        struct tm* now = localtime(&current_time);
        if (now != nullptr) {
            char buffer[32];
            strftime(buffer, sizeof(buffer), "%Y%m%d_%H.%M.%S", now);
            return std::string(buffer);
        } else {
            return std::string("00000000_00.00.00");
        }
    }

    // Get timestamp with millisecond precision for logging
    static std::string getTimestamp() {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm* now = localtime(&tv.tv_sec);
        if (now != nullptr) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", now->tm_hour, now->tm_min, now->tm_sec,
                     static_cast<int>(tv.tv_usec / 1000));
            return std::string(buffer);
        } else {
            return std::string("00:00:00.000");
        }
    }

    static std::string makeRecordFilePath(const int32_t sample_rate,
                                          const int32_t channel_count,
                                          const uint32_t bits_per_sample,
                                          const std::string& override_path) {
        if (!override_path.empty()) {
            return override_path;
        }
        const std::string format_time = AudioUtils::getFormatTime();
        char buffer[256];
        int bytes_written = snprintf(buffer, sizeof(buffer), "/data/record_%dHz_%dch_%dbit_%s.wav", sample_rate,
                                     channel_count, bits_per_sample, format_time.c_str());

        if (bytes_written >= 240 || bytes_written < 0) {
            snprintf(buffer, sizeof(buffer), "/data/audio_%s.wav", format_time.c_str());
            printf("Warning: File path too long, using shortened name\n");
        }

        return std::string(buffer);
    }
};

/************************** Signal Guard (RAII) ******************************/
class SignalGuard {
public:
    SignalGuard() {
        s_exit_requested_.store(false);
        signal(SIGINT, signalHandler);
    }
    ~SignalGuard() noexcept { signal(SIGINT, SIG_DFL); }

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
    SignalGuard(SignalGuard&&) = delete;
    SignalGuard& operator=(SignalGuard&&) = delete;

    bool isExitRequested() const { return s_exit_requested_.load(); }

private:
    static inline std::atomic<bool> s_exit_requested_{false};
    static void signalHandler(int sig) {
        if (sig == SIGINT) {
            s_exit_requested_.store(true);
        }
    }
};

/************************** Audio Configuration ******************************/
struct AudioConfig {
    int32_t sample_rate = 48000;
    int32_t channel_count = 2;
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;
    size_t min_frame_count = 0;

    audio_source_t input_source = AUDIO_SOURCE_MIC;
    audio_input_flags_t input_flag = AUDIO_INPUT_FLAG_NONE;
    int32_t duration_seconds = 0;
    std::string record_file_path = "";

    audio_usage_t usage = AUDIO_USAGE_MEDIA;
    audio_output_flags_t output_flag = AUDIO_OUTPUT_FLAG_NONE;
    std::string play_file_path = "/data/audio_test.wav";

    std::vector<int32_t> set_params{};
};

/************************** AudioMode Definitions ******************************/
enum class AudioMode { kInvalid = -1, kRecord = 0, kPlay = 1, kLoopback = 2, kSetParams = 100 };

/************************** Audio Parameter Manager ******************************/
namespace {
const android::String8 kParamOpenSource = android::String8("open_source");
const android::String8 kParamCloseSource = android::String8("close_source");
const android::String8 kParamChannelMask = android::String8("channel_mask");
} // namespace

class AudioParameterManager {
public:
    AudioParameterManager() = default;
    ~AudioParameterManager() noexcept = default;

    AudioParameterManager(const AudioParameterManager&) = delete;
    AudioParameterManager& operator=(const AudioParameterManager&) = delete;
    AudioParameterManager(AudioParameterManager&&) = delete;
    AudioParameterManager& operator=(AudioParameterManager&&) = delete;

    void setOpenSourceWithUsage(audio_usage_t usage) {
        setSystemParameter(kParamOpenSource, audioUsageToString(usage));
    }

    void setCloseSourceWithUsage(audio_usage_t usage) {
        setSystemParameter(kParamCloseSource, audioUsageToString(usage));
    }

    void setChannelMask(const android::sp<android::AudioTrack>& audio_track, audio_channel_mask_t channel_mask) {
        setAudioTrackParameter(audio_track, kParamChannelMask, android::String8::format("%d", channel_mask));
    }

private:
    void setSystemParameter(const android::String8& key, const android::String8& value) {
#if ENABLE_SET_PARAMS
        android::AudioParameter audio_param;
        audio_param.add(key, value);
        android::String8 param_string = audio_param.toString();
        android::AudioSystem::setParameters(param_string);
        printf("Set parameter: %s\n", param_string.c_str());
#endif
    }

    void setAudioTrackParameter(const android::sp<android::AudioTrack>& audio_track,
                                const android::String8& key,
                                const android::String8& value) {
#if ENABLE_SET_PARAMS
        android::AudioParameter audio_param;
        audio_param.add(key, value);
        android::String8 param_string = audio_param.toString();
        audio_track->setParameters(param_string);
        printf("Set parameter: %s\n", param_string.c_str());
#endif
    }

    android::String8 audioUsageToString(audio_usage_t usage) {
        static const std::unordered_map<audio_usage_t, const char*> usage_map = {
            {AUDIO_USAGE_UNKNOWN, "AUDIO_USAGE_UNKNOWN"},
            {AUDIO_USAGE_MEDIA, "AUDIO_USAGE_MEDIA"},
            {AUDIO_USAGE_VOICE_COMMUNICATION, "AUDIO_USAGE_VOICE_COMMUNICATION"},
            {AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING, "AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING"},
            {AUDIO_USAGE_ALARM, "AUDIO_USAGE_ALARM"},
            {AUDIO_USAGE_NOTIFICATION, "AUDIO_USAGE_NOTIFICATION"},
            {AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE, "AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE"},
            {AUDIO_USAGE_NOTIFICATION_EVENT, "AUDIO_USAGE_NOTIFICATION_EVENT"},
            {AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY, "AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY"},
            {AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE, "AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE"},
            {AUDIO_USAGE_ASSISTANCE_SONIFICATION, "AUDIO_USAGE_ASSISTANCE_SONIFICATION"},
            {AUDIO_USAGE_GAME, "AUDIO_USAGE_GAME"},
            {AUDIO_USAGE_VIRTUAL_SOURCE, "AUDIO_USAGE_VIRTUAL_SOURCE"},
            {AUDIO_USAGE_ASSISTANT, "AUDIO_USAGE_ASSISTANT"},
            {AUDIO_USAGE_CALL_ASSISTANT, "AUDIO_USAGE_CALL_ASSISTANT"},
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST"},
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT"},
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED"},
            {AUDIO_USAGE_EMERGENCY, "AUDIO_USAGE_EMERGENCY"},
            {AUDIO_USAGE_SAFETY, "AUDIO_USAGE_SAFETY"},
            {AUDIO_USAGE_VEHICLE_STATUS, "AUDIO_USAGE_VEHICLE_STATUS"},
            {AUDIO_USAGE_ANNOUNCEMENT, "AUDIO_USAGE_ANNOUNCEMENT"},
        };
        const char* usage_name = "AUDIO_USAGE_UNKNOWN";
        const auto it = usage_map.find(usage);
        if (it != usage_map.end()) {
            usage_name = it->second;
        }
        return android::String8::format("0:%s", usage_name);
    }
};

/************************** Audio Operation Base Class ******************************/
class AudioOperation {
public:
    explicit AudioOperation(const AudioConfig& config) : config_(config) {}
    virtual ~AudioOperation() noexcept = default;

    AudioOperation(const AudioOperation&) = delete;
    AudioOperation& operator=(const AudioOperation&) = delete;
    AudioOperation(AudioOperation&&) = delete;
    AudioOperation& operator=(AudioOperation&&) = delete;

    virtual int32_t execute() = 0;

protected:
    static constexpr uint32_t kMaxAudioDataSize = 2u * 1024u * 1024u * 1024u; // 2 GiB
    static constexpr uint32_t kProgressReportInterval = 10;                   // report progress every 10 seconds
    static constexpr uint32_t kLevelMeterInterval = 25;                       // Update level meter every 25 frames

    AudioConfig config_;
    AudioParameterManager audio_param_manager_;
    SignalGuard signal_guard_;
    uint32_t level_meter_counter_ = 0;
    uint64_t next_progress_report_ = 0;

    size_t calculateBufferSize() const {
        const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
        return (config_.min_frame_count * 2) * config_.channel_count * bytes_per_sample;
    }

    size_t calculateFrameCount() const {
        const size_t min_frames = static_cast<size_t>((config_.sample_rate * 10) / 1000);
        const size_t adjusted_min_frame_count = std::max(config_.min_frame_count, min_frames);
        return adjusted_min_frame_count * 2;
    }

    uint64_t calculateBytesPerSecond() const {
        const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
        return static_cast<uint64_t>(config_.sample_rate) * config_.channel_count * bytes_per_sample;
    }

    // Validate audio parameters for correctness
    bool validateAudioParameters() const {
        if (config_.sample_rate <= 0 || config_.channel_count <= 0) {
            printf("Error: Invalid sample rate or channel count\n");
            return false;
        }
        if (config_.format == AUDIO_FORMAT_INVALID) {
            printf("Error: Invalid audio format\n");
            return false;
        }
        return true;
    }

    android::content::AttributionSourceState createAttributionSource() const {
        android::content::AttributionSourceState attribution_source;
        attribution_source.packageName = std::string("Audio Test Client");
        attribution_source.token = android::sp<android::BBinder>::make();
        attribution_source.uid = getuid();
        attribution_source.pid = getpid();
        return attribution_source;
    }

    bool initializeAudioRecord(android::sp<android::AudioRecord>& audio_record) {
        audio_channel_mask_t channel_mask = audio_channel_in_mask_from_count(config_.channel_count);
        if (android::AudioRecord::getMinFrameCount(&config_.min_frame_count, config_.sample_rate, config_.format,
                                                   channel_mask) != android::NO_ERROR) {
            printf("Warning: Cannot get min frame count, using default value\n");
        }
        const size_t frame_count = calculateFrameCount();

        printf("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
               "frameCount=%zu\n",
               config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask,
               frame_count);
        ALOGI("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
              "frameCount=%zu",
              config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask,
              frame_count);

        android::content::AttributionSourceState attribution_source = createAttributionSource();
        audio_attributes_t attributes{};
        attributes.source = config_.input_source;
        audio_record = android::sp<android::AudioRecord>::make(attribution_source);
        if (audio_record->set(config_.input_source, config_.sample_rate, config_.format, channel_mask, frame_count,
                              nullptr,
#ifndef ANDROID_API_14_PLUS
                              nullptr,
#endif
                              0, false, AUDIO_SESSION_ALLOCATE, android::AudioRecord::TRANSFER_SYNC, config_.input_flag,
                              getuid(), getpid(), &attributes, AUDIO_PORT_HANDLE_NONE) != android::NO_ERROR) {
            printf("Error: Failed to initialize AudioRecord parameters\n");
            ALOGE("Failed to initialize AudioRecord parameters");
            return false;
        }

        if (audio_record->initCheck() != android::NO_ERROR) {
            printf("Error: AudioRecord initialization check failed\n");
            ALOGE("AudioRecord initialization check failed");
            return false;
        }

        printf("AudioRecord initialized successfully\n");
        return true;
    }

    bool initializeAudioTrack(android::sp<android::AudioTrack>& audio_track) {
        audio_channel_mask_t channel_mask = audio_channel_out_mask_from_count(config_.channel_count);

        audio_stream_type_t stream_type = AudioUtils::usageToStreamType(config_.usage);
        if (android::AudioTrack::getMinFrameCount(&config_.min_frame_count, stream_type, config_.sample_rate) !=
            android::NO_ERROR) {
            printf("Warning: Cannot get min frame count using streamType, using default value\n");
        }
        const size_t frame_count = calculateFrameCount();

        printf("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
               "frameCount=%zu\n",
               config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);
        ALOGI("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
              "frameCount=%zu",
              config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);

        android::content::AttributionSourceState attribution_source = createAttributionSource();
        audio_attributes_t attributes{};
        attributes.usage = config_.usage;
        attributes.content_type = AudioUtils::usageToContentType(config_.usage);
        audio_track = android::sp<android::AudioTrack>::make(attribution_source);
        if (audio_track->set(AUDIO_STREAM_DEFAULT, config_.sample_rate, config_.format, channel_mask, frame_count,
                             config_.output_flag, nullptr,
#ifndef ANDROID_API_14_PLUS
                             nullptr,
#endif
                             0, nullptr, false, AUDIO_SESSION_ALLOCATE, android::AudioTrack::TRANSFER_SYNC, nullptr,
                             attribution_source, &attributes, false, 1.0f,
                             AUDIO_PORT_HANDLE_NONE) != android::NO_ERROR) {
            printf("Error: Failed to initialize AudioTrack parameters\n");
            ALOGE("Failed to initialize AudioTrack parameters");
            return false;
        }

        if (audio_track->initCheck() != android::NO_ERROR) {
            printf("Error: AudioTrack initialization check failed\n");
            ALOGE("AudioTrack initialization check failed");
            return false;
        }

        printf("AudioTrack initialized successfully\n");
        return true;
    }

    template <typename T> bool startAudioComponent(const android::sp<T>& component) {
        if constexpr (std::is_same_v<T, android::AudioTrack>) {
            audio_param_manager_.setOpenSourceWithUsage(config_.usage);
        }

        printf("Starting audio component\n");
        ALOGI("Starting audio component");
        android::status_t start_result = component->start();
        if (start_result != android::NO_ERROR) {
            const char* component_name = std::is_same_v<T, android::AudioRecord> ? "AudioRecord" : "AudioTrack";
            printf("Error: %s start failed with status %d\n", component_name, start_result);
            ALOGE("%s start failed with status %d", component_name, start_result);
            return false;
        }
        return true;
    }

    template <typename T> void stopAudioComponent(const android::sp<T>& audio_component) {
        if (audio_component != nullptr) {
            printf("Stopping audio component\n");
            ALOGI("Stopping audio component");
            audio_component->stop();
            if constexpr (std::is_same_v<T, android::AudioTrack>) {
                audio_param_manager_.setCloseSourceWithUsage(config_.usage);
            }
        }
    }

    bool setupWavFileForRecording(WAVFile& wav_file) {
        size_t bytes_per_sample = audio_bytes_per_sample(config_.format);

        config_.record_file_path = AudioUtils::makeRecordFilePath(config_.sample_rate, config_.channel_count,
                                                                  bytes_per_sample * 8, config_.record_file_path);

        printf("Recording audio to file: %s\n", config_.record_file_path.c_str());
        if (!wav_file.createForWriting(config_.record_file_path, config_.sample_rate, config_.channel_count,
                                       bytes_per_sample * 8)) {
            printf("Error: Can't create record file: %s\n", config_.record_file_path.c_str());
            return false;
        }

        return true;
    }

    bool setupWavFileForPlayback(WAVFile& wav_file) {
        if (config_.play_file_path.empty() || access(config_.play_file_path.c_str(), F_OK) == -1) {
            printf("Error: File does not exist: %s\n", config_.play_file_path.c_str());
            return false;
        }

        if (!wav_file.openForReading(config_.play_file_path)) {
            printf("Error: Failed to open WAV file: %s\n", config_.play_file_path.c_str());
            return false;
        }

        config_.sample_rate = wav_file.getSampleRate();
        config_.channel_count = wav_file.getNumChannels();
        config_.format = wav_file.getAudioFormat();
        printf("audio file info: %s, sampleRate: %d, channelCount: %d, format: %d\n", config_.play_file_path.c_str(),
               config_.sample_rate, config_.channel_count, config_.format);

        return true;
    }

    template <typename T>
    bool reportProgress(const android::sp<T>& component,
                        const uint64_t total_bytes_processed,
                        const uint64_t bytes_per_second,
                        WAVFile* wav_file = nullptr) {
        if (component == nullptr) {
            return false;
        }

        if (total_bytes_processed >= next_progress_report_) {
            const char* operation_type_name = std::is_same_v<T, android::AudioRecord> ? "Recording" : "Playing";
            printf("%s ... , processed %.2f seconds, %.2f MB\n", operation_type_name,
                   static_cast<float>(total_bytes_processed) / bytes_per_second,
                   static_cast<float>(total_bytes_processed) / (1024u * 1024u));
            next_progress_report_ += bytes_per_second * kProgressReportInterval;

            if constexpr (std::is_same_v<T, android::AudioRecord>) {
                if (wav_file) {
                    wav_file->updateHeader();
                }
            }
            return true;
        }
        return false;
    }

    void updateLevelMeter(const char* buffer, size_t size) {
        if (++level_meter_counter_ % kLevelMeterInterval != 0) {
            return;
        }

        constexpr float kNorm16bit = 32768.0f;
        constexpr float kNorm32bit = 2147483648.0f;
        constexpr float kDbFloor = -60.0f;

        const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
        if (size == 0 || bytes_per_sample == 0) {
            printf("Error: Invalid input size or bytes_per_sample\n");
            return;
        }

        const size_t num_samples = size / bytes_per_sample;
        float peak_amplitude = 0.0f;
        if (bytes_per_sample == 2) {
            const int16_t* int16_data = reinterpret_cast<const int16_t*>(buffer);
            for (size_t i = 0; i < num_samples; ++i) {
                peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(int16_data[i])) / kNorm16bit);
            }
        } else if (bytes_per_sample == 4) {
            const int32_t* int32_data = reinterpret_cast<const int32_t*>(buffer);
            for (size_t i = 0; i < num_samples; ++i) {
                peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(int32_data[i])) / kNorm32bit);
            }
        } else {
            printf("Error: Unsupported audio format for level meter\n");
            return;
        }

        const float db_level =
            peak_amplitude > 0.0f ? std::max(20.0f * std::log10(peak_amplitude), kDbFloor) : kDbFloor;
        const std::string timestamp = AudioUtils::getTimestamp();
        printf("[%s] Audio Level: %.1f dB, bytes: %zu\n", timestamp.c_str(), db_level, size);
    }
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation : public AudioOperation {
public:
    explicit AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioRecordOperation() noexcept override = default;

    AudioRecordOperation(const AudioRecordOperation&) = delete;
    AudioRecordOperation& operator=(const AudioRecordOperation&) = delete;
    AudioRecordOperation(AudioRecordOperation&&) = delete;
    AudioRecordOperation& operator=(AudioRecordOperation&&) = delete;

    int32_t execute() override {
        WAVFile wav_file;
        android::sp<android::AudioRecord> audio_record;

        if (!setupWavFileForRecording(wav_file) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioRecord(audio_record)) {
            wav_file.close();
            return -1;
        }

        if (!startAudioComponent(audio_record)) {
            wav_file.close();
            return -1;
        }

        int32_t operation_result = recordLoop(audio_record, wav_file);

        stopAudioComponent(audio_record);
        wav_file.finalize();

        return operation_result;
    }

private:
    int32_t recordLoop(const android::sp<android::AudioRecord>& audio_record, WAVFile& wav_file) {
        BufferManager buffer_manager(calculateBufferSize());
        if (!buffer_manager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audio_buffer = buffer_manager.get();

        if (config_.duration_seconds > 0) {
            printf("Recording for %d seconds...\n", config_.duration_seconds);
        }

        printf("Recording in progress. Press Ctrl+C to stop\n");
        ALOGI("Recording in progress.");
        const uint64_t bytes_per_second = calculateBytesPerSecond();
        const uint64_t max_bytes_to_record =
            (config_.duration_seconds > 0)
                ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                           static_cast<uint64_t>(kMaxAudioDataSize))
                : static_cast<uint64_t>(kMaxAudioDataSize);
        next_progress_report_ = bytes_per_second * kProgressReportInterval;

        uint64_t total_bytes_read = 0;
        while (total_bytes_read < max_bytes_to_record && !signal_guard_.isExitRequested()) {
            const ssize_t bytes_read = audio_record->read(audio_buffer, calculateBufferSize());
            if (bytes_read < 0) {
                printf("Error: AudioRecord read failed: %zd\n", bytes_read);
                ALOGE("AudioRecord read failed: %zd", bytes_read);
                break;
            }
            if (bytes_read == 0) {
                continue;
            }
            total_bytes_read += static_cast<uint64_t>(bytes_read);

            updateLevelMeter(audio_buffer, static_cast<size_t>(bytes_read));

            if (wav_file.writeData(audio_buffer, static_cast<size_t>(bytes_read)) != static_cast<size_t>(bytes_read)) {
                printf("Error: Failed to save audio data to file\n");
                ALOGE("Failed to save audio data to file");
                break;
            }

            reportProgress(audio_record, total_bytes_read, bytes_per_second, &wav_file);
        }

        printf("Recording finished: Recorded %" PRIu64 " bytes, File saved: %s\n", total_bytes_read,
               wav_file.getFilePath().c_str());

        return 0;
    }
};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation : public AudioOperation {
public:
    explicit AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioPlayOperation() noexcept override = default;

    AudioPlayOperation(const AudioPlayOperation&) = delete;
    AudioPlayOperation& operator=(const AudioPlayOperation&) = delete;
    AudioPlayOperation(AudioPlayOperation&&) = delete;
    AudioPlayOperation& operator=(AudioPlayOperation&&) = delete;

    int32_t execute() override {
        WAVFile wav_file;
        android::sp<android::AudioTrack> audio_track;

        if (!setupWavFileForPlayback(wav_file) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioTrack(audio_track)) {
            wav_file.close();
            return -1;
        }

        if (!startAudioComponent(audio_track)) {
            wav_file.close();
            return -1;
        }

        int32_t operation_result = playLoop(audio_track, wav_file);

        stopAudioComponent(audio_track);
        wav_file.close();

        return operation_result;
    }

private:
    int32_t playLoop(const android::sp<android::AudioTrack>& audio_track, WAVFile& wav_file) {
        BufferManager buffer_manager(calculateBufferSize());
        if (!buffer_manager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audio_buffer = buffer_manager.get();

        printf("Playing in progress. Press Ctrl+C to stop\n");
        ALOGI("Playing in progress.");
        const uint64_t bytes_per_second = calculateBytesPerSecond();
        next_progress_report_ = bytes_per_second * kProgressReportInterval;
        uint64_t total_bytes_played = 0;
        while (!signal_guard_.isExitRequested()) {
            const size_t bytes_read = wav_file.readData(audio_buffer, calculateBufferSize());
            if (bytes_read == 0) {
                printf("End of file reached\n");
                break;
            }

            size_t bytes_written = 0;
            const size_t bytes_to_write = bytes_read;
            while (bytes_written < bytes_to_write && !signal_guard_.isExitRequested()) {
                const ssize_t written =
                    audio_track->write(audio_buffer + bytes_written, bytes_to_write - bytes_written);
                if (written < 0) {
                    printf("Error: AudioTrack write failed: %zd\n", written);
                    ALOGE("AudioTrack write failed: %zd", written);
                    return -1;
                }
                bytes_written += static_cast<size_t>(written);
            }
            total_bytes_played += static_cast<uint64_t>(bytes_written);

            updateLevelMeter(audio_buffer, bytes_read);

            reportProgress(audio_track, total_bytes_played, bytes_per_second);
        }
        printf("Playback finished: Total bytes played: %" PRIu64 "\n", total_bytes_played);

        return 0;
    }
};

/************************** Audio Loopback Operation ******************************/
class AudioLoopbackOperation : public AudioOperation {
public:
    explicit AudioLoopbackOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioLoopbackOperation() noexcept override = default;

    AudioLoopbackOperation(const AudioLoopbackOperation&) = delete;
    AudioLoopbackOperation& operator=(const AudioLoopbackOperation&) = delete;
    AudioLoopbackOperation(AudioLoopbackOperation&&) = delete;
    AudioLoopbackOperation& operator=(AudioLoopbackOperation&&) = delete;

    int32_t execute() override {
        WAVFile wav_file;
        android::sp<android::AudioRecord> audio_record;
        android::sp<android::AudioTrack> audio_track;

        if (!setupWavFileForRecording(wav_file) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioRecord(audio_record)) {
            wav_file.close();
            return -1;
        }

        if (!initializeAudioTrack(audio_track)) {
            wav_file.close();
            return -1;
        }

        if (!startAudioComponent(audio_record)) {
            wav_file.close();
            return -1;
        }
        if (!startAudioComponent(audio_track)) {
            stopAudioComponent(audio_record);
            wav_file.close();
            return -1;
        }

        int32_t operation_result = loopbackLoop(audio_record, audio_track, wav_file);

        stopAudioComponent(audio_record);
        stopAudioComponent(audio_track);
        wav_file.finalize();

        return operation_result;
    }

private:
    int32_t loopbackLoop(const android::sp<android::AudioRecord>& audio_record,
                         const android::sp<android::AudioTrack>& audio_track,
                         WAVFile& wav_file) {
        BufferManager buffer_manager(calculateBufferSize());
        if (!buffer_manager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audio_buffer = buffer_manager.get();

        if (config_.duration_seconds > 0) {
            printf("Duplex audio started: Recording for %d seconds...\n", config_.duration_seconds);
        }

        printf("Duplex audio in progress. Press Ctrl+C to stop\n");
        ALOGI("Duplex audio in progress.");
        const uint64_t bytes_per_second = calculateBytesPerSecond();
        const uint64_t max_bytes_to_record =
            (config_.duration_seconds > 0)
                ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                           static_cast<uint64_t>(kMaxAudioDataSize))
                : static_cast<uint64_t>(kMaxAudioDataSize);
        next_progress_report_ = bytes_per_second * kProgressReportInterval;

        uint64_t total_bytes_read = 0;
        uint64_t total_bytes_played = 0;
        bool loopback_error = false;
        while (total_bytes_read < max_bytes_to_record && !signal_guard_.isExitRequested() && !loopback_error) {
            const ssize_t bytes_read = audio_record->read(audio_buffer, calculateBufferSize());
            if (bytes_read < 0) {
                printf("Error: AudioRecord read failed: %zd\n", bytes_read);
                ALOGE("AudioRecord read failed: %zd", bytes_read);
                break;
            }
            if (bytes_read == 0) {
                continue;
            }
            total_bytes_read += static_cast<uint64_t>(bytes_read);

            updateLevelMeter(audio_buffer, static_cast<size_t>(bytes_read));

            if (wav_file.writeData(audio_buffer, static_cast<size_t>(bytes_read)) != static_cast<size_t>(bytes_read)) {
                printf("Error: Failed to save audio data to file\n");
            }

            reportProgress(audio_record, total_bytes_read, bytes_per_second, &wav_file);

            if (total_bytes_read >= max_bytes_to_record) {
                break;
            }

            size_t bytes_written = 0;
            const size_t bytes_to_write = static_cast<size_t>(bytes_read);
            while (bytes_written < bytes_to_write && !signal_guard_.isExitRequested()) {
                const ssize_t written =
                    audio_track->write(audio_buffer + bytes_written, bytes_to_write - bytes_written);
                if (written < 0) {
                    printf("Error: AudioTrack write failed: %zd\n", written);
                    ALOGE("AudioTrack write failed: %zd", written);
                    loopback_error = true;
                    break;
                }
                bytes_written += static_cast<size_t>(written);
            }
            total_bytes_played += static_cast<uint64_t>(bytes_written);
        }

        printf("Loopback audio completed: Total bytes read: %" PRIu64 ", Total bytes played: %" PRIu64
               ", File saved: %s\n",
               total_bytes_read, total_bytes_played, wav_file.getFilePath().c_str());

        return 0;
    }
};

/************************** Set Parameters Operation ******************************/
class SetParamsOperation : public AudioOperation {
public:
    explicit SetParamsOperation(const AudioConfig& config, const std::vector<int32_t>& params)
        : AudioOperation(config), target_params_(params) {}
    ~SetParamsOperation() noexcept override = default;

    SetParamsOperation(const SetParamsOperation&) = delete;
    SetParamsOperation& operator=(const SetParamsOperation&) = delete;
    SetParamsOperation(SetParamsOperation&&) = delete;
    SetParamsOperation& operator=(SetParamsOperation&&) = delete;

    int32_t execute() override {
        if (target_params_.empty()) {
            printf("Error: No parameters provided\n");
            return -1;
        }

        printf("SetParams operation started with %zu parameters\n", target_params_.size());
        for (size_t i = 0; i < target_params_.size(); ++i) {
            printf("  Parameter %zu: %d\n", i + 1, target_params_[i]);
        }

        int32_t operation_type = target_params_[0];
        switch (operation_type) {
        case 1:
            if (target_params_.size() >= 2) {
                int32_t usage_value = target_params_[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usage_value);
                printf("Setting open_source with usage: %d\n", usage);
                audio_param_manager_.setOpenSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for open_source\n");
            }
            break;
        case 2:
            if (target_params_.size() >= 2) {
                int32_t usage_value = target_params_[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usage_value);
                printf("Setting close_source with usage: %d\n", usage);
                audio_param_manager_.setCloseSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for close_source\n");
            }
            break;
        default:
            printf("Error: Unknown primary parameter %d (1=open_source, 2=close_source)\n", operation_type);
            return -1;
        }

        printf("SetParams operation completed\n");
        return 0;
    }

private:
    std::vector<int32_t> target_params_;
};

/************************** Audio Operation Factory ******************************/
class AudioOperationFactory {
private:
    // Private constructor to prevent instantiation - this is a factory class
    AudioOperationFactory() = delete;

public:
    static std::unique_ptr<AudioOperation> createOperation(AudioMode mode, const AudioConfig& config) {
        switch (mode) {
        case AudioMode::kRecord:
            return std::make_unique<AudioRecordOperation>(config);
        case AudioMode::kPlay:
            return std::make_unique<AudioPlayOperation>(config);
        case AudioMode::kLoopback:
            return std::make_unique<AudioLoopbackOperation>(config);
        case AudioMode::kSetParams:
            return std::make_unique<SetParamsOperation>(config, config.set_params);
        default:
            printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
            return nullptr;
        }
    }
};

/************************** Command Line Parser ******************************/
class CommandLineParser {
private:
    // Private constructor to prevent instantiation - this is a utility class
    CommandLineParser() = delete;

public:
    // Parse command line arguments and configure audio mode and parameters
    static void parseArguments(int32_t argc, char** argv, AudioMode& mode, AudioConfig& config) {
        int32_t opt = 0;
        while ((opt = getopt(argc, argv, "m:s:r:c:f:I:u:O:F:d:P:h:")) != -1) {
            switch (opt) {
            case 'm': // mode
                mode = static_cast<AudioMode>(atoi(optarg));
                break;
            case 's': // audio source
                config.input_source = static_cast<audio_source_t>(atoi(optarg));
                break;
            case 'r': // sample rate
                config.sample_rate = atoi(optarg);
                break;
            case 'c': // channel count
                config.channel_count = atoi(optarg);
                break;
            case 'f': // format (map friendly numbers to audio_format_t)
                config.format = AudioUtils::parseFormatOption(atoi(optarg));
                break;
            case 'I': // input flag
                config.input_flag = static_cast<audio_input_flags_t>(atoi(optarg));
                break;
            case 'd': // recording duration in seconds
                config.duration_seconds = atoi(optarg);
                break;
            case 'u': // audio usage
                config.usage = static_cast<audio_usage_t>(atoi(optarg));
                break;
            case 'O': // output flag
                config.output_flag = static_cast<audio_output_flags_t>(atoi(optarg));
                break;
            case 'F': // min frame count
                config.min_frame_count = atoi(optarg);
                break;
            case 'P': // audio file path (input for play, output for record/loopback)
                if (mode == AudioMode::kPlay) {
                    config.play_file_path = optarg;
                } else if ((mode == AudioMode::kRecord) || (mode == AudioMode::kLoopback)) {
                    config.record_file_path = optarg;
                }
                break;
            case 'h': // help for use
                showHelp();
                exit(0);
            default:
                showHelp();
                exit(-1);
            }
        }

        if (mode == AudioMode::kSetParams) {
            for (int32_t i = optind; i < argc; ++i) {
                std::string arg_str(argv[i]);
                size_t start = 0;
                size_t end = arg_str.find(',');
                while (end != std::string::npos) {
                    if (end > start) {
                        config.set_params.emplace_back(std::stoi(arg_str.substr(start, end - start)));
                    }
                    start = end + 1;
                    end = arg_str.find(',', start);
                }
                if (start < arg_str.length()) {
                    config.set_params.emplace_back(std::stoi(arg_str.substr(start)));
                }
            }
        } else {
            if (optind < argc) {
                if (mode == AudioMode::kPlay) {
                    config.play_file_path = argv[optind];
                } else if ((mode == AudioMode::kRecord) || (mode == AudioMode::kLoopback)) {
                    config.record_file_path = argv[optind];
                }
            }
        }
    }

    static void showHelp() {
        const char* help_text = R"(
Audio Test Client - Combined Record and Play Demo
Usage: audio_test_client -m{mode} [options] [audio_file]

Modes:
  -m0   Record mode
  -m1   Play mode
  -m2   Loopback mode (record and play simultaneously, echo test)
  -m100 Set params mode (set audio parameters without playback/recording)

Record Options:
  -s{inputSource}     Set audio source
                       0: AUDIO_SOURCE_DEFAULT
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
  -r{sampleRate}      Set sample rate (e.g., 8000, 16000, 48000)
  -c{channelCount}    Set channel count (1, 2, 4, 6, 8, 12, 16)
  -f{format}          Set audio format
                       0: AUDIO_FORMAT_DEFAULT (Default audio format)
                       1: AUDIO_FORMAT_PCM_16_BIT (16-bit PCM)
                       2: AUDIO_FORMAT_PCM_8_BIT (8-bit PCM)
                       3: AUDIO_FORMAT_PCM_32_BIT (32-bit PCM)
                       4: AUDIO_FORMAT_PCM_8_24_BIT (8-bit PCM with 24-bit padding)
                       6: AUDIO_FORMAT_PCM_24_BIT_PACKED (24-bit packed PCM)
  -I{inputFlag}       Set audio input flag
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
  -d{duration}        Set recording duration(s) (0 = unlimited)

Play Options:
  -u{usage}           Set audio usage
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
                       // 1004: AUDIO_USAGE_SPEAKER_CLEANUP (Speaker cleanup)
                       Note: Content type is automatically set based on usage type
  -O{outputFlag}      Set audio output flag
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

Common Options:
  -F{minFrameCount}   Set play/record min frame count (default: system selected)
  -P{filePath}        Audio file path (input for play, output for record/loopback)
  -h                  Show this help message

Set Params Options:
  Parameters format: audio_test_client -m100 param1[,param2[,param3...]]
    param1            First parameter (required)
                       1: open_source
                       2: close_source
    param2            Second parameter (audio usage)
                       1: AUDIO_USAGE_MEDIA
                       2: AUDIO_USAGE_VOICE_COMMUNICATION
                       ... (see usage)
    param3+           Additional parameters (reserved for future use)

For more details, please refer to system/media/audio/include/system/audio-hal-enums.h

Examples:
  Record: audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20
  Play:   audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav
  Loopback: audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
  SetParams: audio_test_client -m100 1,1
)";
        puts(help_text);
    }
};

int main(int argc, char** argv) {
    AudioMode mode = AudioMode::kInvalid;
    AudioConfig config;

    printf("Audio Test Client %s Start...\n", AUDIO_TEST_CLIENT_VERSION);
    CommandLineParser::parseArguments(argc, argv, mode, config);

    std::unique_ptr<AudioOperation> operation = AudioOperationFactory::createOperation(mode, config);
    if (!operation) {
        CommandLineParser::showHelp();
        return -1;
    }

    return operation->execute();
}
