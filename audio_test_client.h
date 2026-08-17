// Copyright 2026 Audio Test Client Authors
// SPDX-License-Identifier: GPL-3.0-only
//
// Audio Test Client - A professional Android system-level audio testing tool.
// Class declarations for audio recording, playback, loopback, and parameter config.

#ifndef AUDIO_TEST_CLIENT_H_
#define AUDIO_TEST_CLIENT_H_

// C system headers
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// C++ standard library headers
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
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
#define AUDIO_TEST_CLIENT_VERSION "3.6.0"

static constexpr bool kEnableSetParams = false;

/************************** Audio File Format Enum ******************************/
enum class AudioFileFormat {
    kWav,
    kRawPcm,
};

/************************** Audio File Base ******************************/
class AudioFileBase {
public:
    virtual ~AudioFileBase() noexcept;

    AudioFileBase(const AudioFileBase&) = delete;
    AudioFileBase& operator=(const AudioFileBase&) = delete;
    AudioFileBase(AudioFileBase&&) = delete;
    AudioFileBase& operator=(AudioFileBase&&) = delete;

    virtual bool createForWriting(const std::string& file_path,
                                  int32_t sample_rate,
                                  int32_t num_channels,
                                  uint32_t bits_per_sample) = 0;
    virtual bool openForReading(const std::string& file_path) = 0;
    virtual size_t writeData(const char* data, size_t size) = 0;
    virtual size_t readData(char* data, size_t size);
    virtual void updateHeader() {}
    virtual void finalize() { close(); }
    virtual void close();

    virtual const std::string& getFilePath() const;
    virtual int32_t getSampleRate() const;
    virtual int32_t getNumChannels() const;
    virtual audio_format_t getAudioFormat() const;

protected:
    AudioFileBase() = default;

    std::string file_path_;
    std::fstream file_stream_;
    int32_t sample_rate_ = 48000;
    int32_t num_channels_ = 2;
    uint32_t bits_per_sample_ = 16;
    bool is_valid_ = false;
};

/************************** WAV File Implementation ******************************/
class WavFile final : public AudioFileBase {
public:
    WavFile() = default;
    ~WavFile() noexcept override = default;

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

        bool write(std::ostream& out) const;
        bool read(std::istream& in);
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
    size_t readData(char* data, size_t size) override;
    void updateHeader() override;
    void finalize() override;
    audio_format_t getAudioFormat() const override;

private:
    Header header_{};
    size_t remaining_data_ = 0;
};

/************************** Raw PCM File Implementation ******************************/
class RawPcmFile final : public AudioFileBase {
public:
    RawPcmFile() = default;
    ~RawPcmFile() noexcept override = default;

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
};

/************************** Audio Utility Functions ******************************/
class AudioUtils {
private:
    AudioUtils() = delete;

    struct UsageInfo {
        audio_stream_type_t stream_type;
        audio_content_type_t content_type;
    };

    static UsageInfo getUsageInfo(audio_usage_t usage);

public:
    static audio_stream_type_t usageToStreamType(audio_usage_t usage);
    static audio_content_type_t usageToContentType(audio_usage_t usage);
    static audio_format_t parseFormatOption(int v);
    static audio_format_t bitsPerSampleToAudioFormat(uint32_t bits_per_sample);
    static std::string getFormatTime();
    static std::string getTimestamp();
    static std::string makeRecordFilePath(const int32_t sample_rate,
                                          const int32_t channel_count,
                                          const uint32_t bits_per_sample,
                                          const std::string& override_path,
                                          AudioFileFormat format = AudioFileFormat::kWav);
    static std::vector<int32_t> parseIntList(const std::string& str);
    static std::vector<char> createAudioBuffer(size_t requested_size);
    static std::unique_ptr<AudioFileBase> createAudioFile(AudioFileFormat format);
    static AudioFileFormat detectFileFormat(const std::string& file_path);
    static std::string getAudioFileExtension(AudioFileFormat format);
};

/************************** Signal Guard (RAII) ******************************/
class SignalGuard final {
public:
    SignalGuard();
    ~SignalGuard() noexcept;

    SignalGuard(const SignalGuard&) = delete;
    SignalGuard& operator=(const SignalGuard&) = delete;
    SignalGuard(SignalGuard&&) = delete;
    SignalGuard& operator=(SignalGuard&&) = delete;

    static bool isExitRequested();

private:
    static volatile sig_atomic_t s_exit_requested_;
    static void signalHandler(int sig);
};

/************************** AudioMode Definitions ******************************/
enum class AudioMode { kInvalid = -1, kHelp = -2, kRecord = 0, kPlay = 1, kLoopback = 2, kSetParams = 100 };

/************************** Audio Configuration ******************************/
struct AudioConfig {
    int32_t sample_rate = 48000;
    int32_t channel_count = 2;
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;
    size_t frame_count = 0;

    audio_source_t input_source = AUDIO_SOURCE_MIC;
    audio_input_flags_t input_flag = AUDIO_INPUT_FLAG_NONE;
    int32_t duration_seconds = 0;
    std::string record_file_path;
    AudioFileFormat record_file_format = AudioFileFormat::kWav;

    audio_usage_t usage = AUDIO_USAGE_MEDIA;
    audio_output_flags_t output_flag = AUDIO_OUTPUT_FLAG_NONE;
    std::string play_file_path = "/data/audio_test.wav";

    std::vector<int32_t> set_params;
};

/************************** Audio Parameter Manager ******************************/
class AudioParameterManager final {
public:
    AudioParameterManager() = default;
    ~AudioParameterManager() noexcept = default;

    AudioParameterManager(const AudioParameterManager&) = delete;
    AudioParameterManager& operator=(const AudioParameterManager&) = delete;
    AudioParameterManager(AudioParameterManager&&) = delete;
    AudioParameterManager& operator=(AudioParameterManager&&) = delete;

    void setOpenSourceWithUsage(audio_usage_t usage);
    void setCloseSourceWithUsage(audio_usage_t usage);

private:
    static const android::String8 kParamOpenSource;
    static const android::String8 kParamCloseSource;

    void setSystemParameter(const android::String8& key, const android::String8& value);
    android::String8 audioUsageToString(audio_usage_t usage);
};

/************************** Thread Safe Buffer Queue ******************************/
class ThreadSafeBufferQueue final {
public:
    explicit ThreadSafeBufferQueue(size_t max_buffers = 16) : max_buffers_(max_buffers), stopped_(false) {}
    ~ThreadSafeBufferQueue() = default;

    ThreadSafeBufferQueue(const ThreadSafeBufferQueue&) = delete;
    ThreadSafeBufferQueue& operator=(const ThreadSafeBufferQueue&) = delete;
    ThreadSafeBufferQueue(ThreadSafeBufferQueue&&) = delete;
    ThreadSafeBufferQueue& operator=(ThreadSafeBufferQueue&&) = delete;

    bool push(std::vector<char>&& buffer);
    bool pop(std::vector<char>& buffer);
    void stop();

private:
    std::queue<std::vector<char>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    size_t max_buffers_ = 16;
    bool stopped_ = false;
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
    // 4GB cap: WAV riff_size is uint32_t; also bounds RawPCM to prevent unbounded disk growth.
    static constexpr uint64_t kMaxAudioDataSize = static_cast<uint64_t>(UINT32_MAX) - 36;
    static constexpr uint32_t kProgressReportInterval = 10;
    static constexpr uint32_t kLevelMeterInterval = 25;

    AudioConfig config_;
    AudioParameterManager audio_param_manager_;
    std::atomic<uint32_t> level_meter_counter_{0};

    size_t calculateBufferSize(size_t actual_frame_count) const;
    size_t resolveFrameCount(bool is_fast_path, size_t min_frame_count) const;
    uint64_t calculateBytesPerSecond() const;
    bool validateAudioParameters() const;
    android::content::AttributionSourceState createAttributionSource() const;
    bool initializeAudioRecord(android::sp<android::AudioRecord>& audio_record);
    bool initializeAudioTrack(android::sp<android::AudioTrack>& audio_track);

    bool startAudioRecord(const android::sp<android::AudioRecord>& audio_record);
    bool startAudioTrack(const android::sp<android::AudioTrack>& audio_track);
    void stopAudioRecord(const android::sp<android::AudioRecord>& audio_record);
    void stopAudioTrack(const android::sp<android::AudioTrack>& audio_track);

    bool setupAudioFileForRecording(std::unique_ptr<AudioFileBase>& audio_file);
    bool setupAudioFileForPlayback(std::unique_ptr<AudioFileBase>& audio_file);

    bool reportProgress(const char* action,
                        uint64_t total_bytes_processed,
                        uint64_t bytes_per_second,
                        std::chrono::steady_clock::time_point& last_time);
    void updateLevelMeter(const char* buffer, size_t size);
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation final : public AudioOperation {
public:
    explicit AudioRecordOperation(const AudioConfig& config);
    ~AudioRecordOperation() noexcept override = default;

    AudioRecordOperation(const AudioRecordOperation&) = delete;
    AudioRecordOperation& operator=(const AudioRecordOperation&) = delete;
    AudioRecordOperation(AudioRecordOperation&&) = delete;
    AudioRecordOperation& operator=(AudioRecordOperation&&) = delete;

    int32_t execute() override;

private:
    int32_t recordLoop(const android::sp<android::AudioRecord>& audio_record, AudioFileBase& audio_file);
};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation final : public AudioOperation {
public:
    explicit AudioPlayOperation(const AudioConfig& config);
    ~AudioPlayOperation() noexcept override = default;

    AudioPlayOperation(const AudioPlayOperation&) = delete;
    AudioPlayOperation& operator=(const AudioPlayOperation&) = delete;
    AudioPlayOperation(AudioPlayOperation&&) = delete;
    AudioPlayOperation& operator=(AudioPlayOperation&&) = delete;

    int32_t execute() override;

private:
    int32_t playLoop(const android::sp<android::AudioTrack>& audio_track, AudioFileBase& audio_file);
};

/************************** Audio Loopback Operation ******************************/
class AudioLoopbackOperation final : public AudioOperation {
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
    std::atomic<bool> play_error_{false};
    std::atomic<uint64_t> total_bytes_played_{0};

    int32_t runLoopback(const android::sp<android::AudioRecord>& audio_record,
                        const android::sp<android::AudioTrack>& audio_track,
                        AudioFileBase& audio_file);
    void playThread(const android::sp<android::AudioTrack>& audio_track);
};

/************************** Set Parameters Operation ******************************/
class SetParamsOperation final : public AudioOperation {
public:
    explicit SetParamsOperation(const AudioConfig& config);
    ~SetParamsOperation() noexcept override = default;

    SetParamsOperation(const SetParamsOperation&) = delete;
    SetParamsOperation& operator=(const SetParamsOperation&) = delete;
    SetParamsOperation(SetParamsOperation&&) = delete;
    SetParamsOperation& operator=(SetParamsOperation&&) = delete;

    int32_t execute() override;
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
