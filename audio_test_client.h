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
// This header file contains class declarations for audio recording, playback,
// loopback testing, and system parameter configuration.

#ifndef AUDIO_TEST_CLIENT_H_
#define AUDIO_TEST_CLIENT_H_

// C system headers
#include <fcntl.h>
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
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
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
#define AUDIO_TEST_CLIENT_VERSION "3.0.0"

static constexpr bool kEnableSetParams = false;

/************************** Audio File Format Enum ******************************/
enum class AudioFileFormat {
    kWav,
    kRawPcm,
};

/************************** Audio File Interface ******************************/
class AudioFileInterface {
public:
    virtual ~AudioFileInterface() = default;

    AudioFileInterface(const AudioFileInterface&) = delete;
    AudioFileInterface& operator=(const AudioFileInterface&) = delete;
    AudioFileInterface(AudioFileInterface&&) = delete;
    AudioFileInterface& operator=(AudioFileInterface&&) = delete;

    virtual bool createForWriting(const std::string& file_path,
                                  int32_t sample_rate,
                                  int32_t num_channels,
                                  uint32_t bits_per_sample) = 0;
    virtual bool openForReading(const std::string& file_path) = 0;
    virtual size_t writeData(const char* data, size_t size) = 0;
    virtual size_t readData(char* data, size_t size) = 0;
    virtual void updateHeader() {}
    virtual void finalize() { close(); }
    virtual void close() = 0;

    virtual const std::string& getFilePath() const = 0;
    virtual int32_t getSampleRate() const = 0;
    virtual int32_t getNumChannels() const = 0;
    virtual uint32_t getBitsPerSample() const = 0;
    virtual audio_format_t getAudioFormat() const = 0;
    virtual bool isOpen() const = 0;

protected:
    AudioFileInterface() = default;
};

/************************** WAV File Implementation ******************************/
class WavFile : public AudioFileInterface {
public:
    WavFile() = default;
    ~WavFile() noexcept override;

    WavFile(const WavFile&) = delete;
    WavFile& operator=(const WavFile&) = delete;
    WavFile(WavFile&&) = delete;
    WavFile& operator=(WavFile&&) = delete;

#pragma pack(push, 1)
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

        void write(std::ostream& out) const;
        void read(std::istream& in);
        void print() const;
    };
#pragma pack(pop)

    static_assert(sizeof(Header) == 44, "WAV Header must be exactly 44 bytes for standard WAV format");

    bool createForWriting(const std::string& file_path,
                          int32_t sample_rate,
                          int32_t num_channels,
                          uint32_t bits_per_sample) override;
    bool openForReading(const std::string& file_path) override;
    size_t writeData(const char* data, size_t size) override;
    void updateHeader() override;
    size_t readData(char* data, size_t size) override;
    void finalize() override;
    void close() override;

    const std::string& getFilePath() const override;
    const Header& getHeader() const;
    int32_t getSampleRate() const override;
    int32_t getNumChannels() const override;
    uint32_t getBitsPerSample() const override;
    bool isOpen() const override;
    audio_format_t getAudioFormat() const override;

private:
    Header header_{};
    std::string file_path_;
    std::fstream file_stream_;
    bool is_header_valid_{false};
    std::streampos data_size_pos_{};
};

/************************** Raw PCM File Implementation ******************************/
class RawPcmFile : public AudioFileInterface {
public:
    RawPcmFile() = default;
    ~RawPcmFile() noexcept override;

    RawPcmFile(const RawPcmFile&) = delete;
    RawPcmFile& operator=(const RawPcmFile&) = delete;
    RawPcmFile(RawPcmFile&&) = delete;
    RawPcmFile& operator=(RawPcmFile&&) = delete;

    bool createForWriting(const std::string& file_path,
                          int32_t sample_rate,
                          int32_t num_channels,
                          uint32_t bits_per_sample) override;
    bool openForReading(const std::string& file_path) override;
    size_t writeData(const char* data, size_t size) override;
    size_t readData(char* data, size_t size) override;
    void close() override;

    const std::string& getFilePath() const override;
    int32_t getSampleRate() const override;
    int32_t getNumChannels() const override;
    uint32_t getBitsPerSample() const override;
    bool isOpen() const override;
    audio_format_t getAudioFormat() const override;

    void setAudioParameters(int32_t sample_rate, int32_t num_channels, uint32_t bits_per_sample);

private:
    std::string file_path_;
    std::fstream file_stream_;
    int32_t sample_rate_{48000};
    int32_t num_channels_{2};
    uint32_t bits_per_sample_{16};
};

/************************** Audio File Factory ******************************/
class AudioFileFactory {
public:
    AudioFileFactory() = delete;

    static std::unique_ptr<AudioFileInterface> create(AudioFileFormat format);
    static AudioFileFormat detectFormatFromPath(const std::string& file_path);
    static std::string getDefaultExtension(AudioFileFormat format);
};

/************************** BufferManager class ******************************/
class BufferManager {
public:
    explicit BufferManager(size_t buffer_size);
    ~BufferManager() noexcept = default;

    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;
    BufferManager(BufferManager&&) = delete;
    BufferManager& operator=(BufferManager&&) = delete;

    char* get() const;
    size_t getSize() const;
    bool isValid() const;

private:
    void initializeBuffer(size_t requested_size);

    std::unique_ptr<char[]> buffer_;
    size_t size_{0};
};

/************************** Audio Utility Functions ******************************/
class AudioUtils {
private:
    AudioUtils() = delete;

public:
    static audio_stream_type_t usageToStreamType(audio_usage_t usage);
    static audio_content_type_t usageToContentType(audio_usage_t usage);
    static audio_format_t parseFormatOption(int v);
    static std::string getFormatTime();
    static std::string getTimestamp();
    static std::string makeRecordFilePath(const int32_t sample_rate,
                                          const int32_t channel_count,
                                          const uint32_t bits_per_sample,
                                          const std::string& override_path,
                                          AudioFileFormat format = AudioFileFormat::kWav);
};

/************************** Signal Guard (RAII) ******************************/
class SignalGuard {
public:
    SignalGuard();
    ~SignalGuard() noexcept;

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
    SignalGuard(SignalGuard&&) = delete;
    SignalGuard& operator=(SignalGuard&&) = delete;

    bool isExitRequested() const;

private:
    static inline std::atomic<bool> s_exit_requested_{false};
    static void signalHandler(int sig);
};

/************************** AudioMode Definitions ******************************/
enum class AudioMode { kInvalid = -1, kRecord = 0, kPlay = 1, kLoopback = 2, kSetParams = 100 };

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
    AudioFileFormat record_file_format = AudioFileFormat::kWav;

    audio_usage_t usage = AUDIO_USAGE_MEDIA;
    audio_output_flags_t output_flag = AUDIO_OUTPUT_FLAG_NONE;
    std::string play_file_path = "/data/audio_test.wav";

    std::vector<int32_t> set_params{};
};

/************************** Audio Parameter Manager ******************************/
class AudioParameterManager {
public:
    AudioParameterManager() = default;
    ~AudioParameterManager() noexcept = default;

    AudioParameterManager(const AudioParameterManager&) = delete;
    AudioParameterManager& operator=(const AudioParameterManager&) = delete;
    AudioParameterManager(AudioParameterManager&&) = delete;
    AudioParameterManager& operator=(AudioParameterManager&&) = delete;

    void setOpenSourceWithUsage(audio_usage_t usage);
    void setCloseSourceWithUsage(audio_usage_t usage);
    void setChannelMask(const android::sp<android::AudioTrack>& audio_track, audio_channel_mask_t channel_mask);

private:
    static const android::String8 kParamOpenSource;
    static const android::String8 kParamCloseSource;
    static const android::String8 kParamChannelMask;

    void setSystemParameter(const android::String8& key, const android::String8& value);
    void setAudioTrackParameter(const android::sp<android::AudioTrack>& audio_track,
                                const android::String8& key,
                                const android::String8& value);
    android::String8 audioUsageToString(audio_usage_t usage);
};

/************************** Thread Safe Buffer Queue ******************************/
class ThreadSafeBufferQueue {
public:
    explicit ThreadSafeBufferQueue(size_t max_buffers = 16) : max_buffers_(max_buffers), stopped_(false) {}
    ~ThreadSafeBufferQueue() = default;

    ThreadSafeBufferQueue(const ThreadSafeBufferQueue&) = delete;
    ThreadSafeBufferQueue& operator=(const ThreadSafeBufferQueue&) = delete;
    ThreadSafeBufferQueue(ThreadSafeBufferQueue&&) = delete;
    ThreadSafeBufferQueue& operator=(ThreadSafeBufferQueue&&) = delete;

    void push(std::vector<char>&& buffer);
    bool pop(std::vector<char>& buffer);
    void stop();
    void reset();
    size_t size() const;

private:
    std::queue<std::vector<char>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    size_t max_buffers_;
    bool stopped_;
};

/************************** Audio Operation Base Class ******************************/
class AudioOperation {
public:
    explicit AudioOperation(const AudioConfig& config);
    virtual ~AudioOperation() noexcept = default;

    AudioOperation(const AudioOperation&) = delete;
    AudioOperation& operator=(const AudioOperation&) = delete;
    AudioOperation(AudioOperation&&) = delete;
    AudioOperation& operator=(AudioOperation&&) = delete;

    virtual int32_t execute() = 0;

protected:
    static constexpr uint32_t kMaxAudioDataSize = 2u * 1024u * 1024u * 1024u;
    static constexpr uint32_t kProgressReportInterval = 10;
    static constexpr uint32_t kLevelMeterInterval = 25;

    AudioConfig config_;
    AudioParameterManager audio_param_manager_;
    SignalGuard signal_guard_;
    uint32_t level_meter_counter_ = 0;
    uint64_t next_progress_report_ = 0;

    size_t calculateBufferSize() const;
    size_t calculateFrameCount() const;
    uint64_t calculateBytesPerSecond() const;
    bool validateAudioParameters() const;
    android::content::AttributionSourceState createAttributionSource() const;
    bool initializeAudioRecord(android::sp<android::AudioRecord>& audio_record);
    bool initializeAudioTrack(android::sp<android::AudioTrack>& audio_track);

    bool startAudioRecord(const android::sp<android::AudioRecord>& audio_record);
    bool startAudioTrack(const android::sp<android::AudioTrack>& audio_track);
    void stopAudioRecord(const android::sp<android::AudioRecord>& audio_record);
    void stopAudioTrack(const android::sp<android::AudioTrack>& audio_track);

    bool setupAudioFileForRecording(std::unique_ptr<AudioFileInterface>& audio_file);
    bool setupAudioFileForPlayback(std::unique_ptr<AudioFileInterface>& audio_file);

    bool reportRecordProgress(const android::sp<android::AudioRecord>& audio_record,
                              uint64_t total_bytes_processed,
                              uint64_t bytes_per_second,
                              AudioFileInterface* audio_file);
    bool reportPlayProgress(const android::sp<android::AudioTrack>& audio_track,
                            uint64_t total_bytes_processed,
                            uint64_t bytes_per_second);

    void updateLevelMeter(const char* buffer, size_t size);
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation : public AudioOperation {
public:
    explicit AudioRecordOperation(const AudioConfig& config);
    ~AudioRecordOperation() noexcept override = default;

    AudioRecordOperation(const AudioRecordOperation&) = delete;
    AudioRecordOperation& operator=(const AudioRecordOperation&) = delete;
    AudioRecordOperation(AudioRecordOperation&&) = delete;
    AudioRecordOperation& operator=(AudioRecordOperation&&) = delete;

    int32_t execute() override;

private:
    int32_t recordLoop(const android::sp<android::AudioRecord>& audio_record, AudioFileInterface& audio_file);
};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation : public AudioOperation {
public:
    explicit AudioPlayOperation(const AudioConfig& config);
    ~AudioPlayOperation() noexcept override = default;

    AudioPlayOperation(const AudioPlayOperation&) = delete;
    AudioPlayOperation& operator=(const AudioPlayOperation&) = delete;
    AudioPlayOperation(AudioPlayOperation&&) = delete;
    AudioPlayOperation& operator=(AudioPlayOperation&&) = delete;

    int32_t execute() override;

private:
    int32_t playLoop(const android::sp<android::AudioTrack>& audio_track, AudioFileInterface& audio_file);
};

/************************** Audio Loopback Operation ******************************/
class AudioLoopbackOperation : public AudioOperation {
public:
    explicit AudioLoopbackOperation(const AudioConfig& config);
    ~AudioLoopbackOperation() noexcept override = default;

    AudioLoopbackOperation(const AudioLoopbackOperation&) = delete;
    AudioLoopbackOperation& operator=(const AudioLoopbackOperation&) = delete;
    AudioLoopbackOperation(AudioLoopbackOperation&&) = delete;
    AudioLoopbackOperation& operator=(AudioLoopbackOperation&&) = delete;

    int32_t execute() override;

private:
    ThreadSafeBufferQueue buffer_queue_;
    std::atomic<bool> record_error_{false};
    std::atomic<bool> play_error_{false};
    std::atomic<uint64_t> total_bytes_recorded_{0};
    std::atomic<uint64_t> total_bytes_played_{0};

    int32_t loopbackLoopDualThread(const android::sp<android::AudioRecord>& audio_record,
                                   const android::sp<android::AudioTrack>& audio_track,
                                   AudioFileInterface& audio_file);
    void recordThread(const android::sp<android::AudioRecord>& audio_record,
                      size_t buffer_size,
                      uint64_t max_bytes,
                      AudioFileInterface* audio_file);
    void playThread(const android::sp<android::AudioTrack>& audio_track, size_t buffer_size);
};

/************************** Set Parameters Operation ******************************/
class SetParamsOperation : public AudioOperation {
public:
    explicit SetParamsOperation(const AudioConfig& config, const std::vector<int32_t>& params);
    ~SetParamsOperation() noexcept override = default;

    SetParamsOperation(const SetParamsOperation&) = delete;
    SetParamsOperation& operator=(const SetParamsOperation&) = delete;
    SetParamsOperation(SetParamsOperation&&) = delete;
    SetParamsOperation& operator=(SetParamsOperation&&) = delete;

    int32_t execute() override;

private:
    std::vector<int32_t> target_params_;
};

/************************** Audio Operation Factory ******************************/
class AudioOperationFactory {
private:
    AudioOperationFactory() = delete;

public:
    static std::unique_ptr<AudioOperation> createOperation(AudioMode mode, const AudioConfig& config);
};

/************************** Command Line Parser ******************************/
class CommandLineParser {
private:
    CommandLineParser() = delete;

public:
    static void parseArguments(int argc, char** argv, AudioMode& mode, AudioConfig& config);
    static void showHelp();
};

#endif  // AUDIO_TEST_CLIENT_H_
