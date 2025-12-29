#include <algorithm>
#include <atomic>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <unordered_map>

#include <binder/Binder.h>
#include <media/AudioParameter.h>
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include <utils/Log.h>
#include <utils/String8.h>

#define LOG_TAG "audio_test_client"

#define AUDIO_TEST_CLIENT_VERSION "2.1.0"
#define SET_PARAMS_ENABLE 1

using namespace android;
using android::content::AttributionSourceState;

/************************** WAV File Management ******************************/
class WAVFile {
public:
    WAVFile() = default;
    ~WAVFile() { close(); }

    struct Header {
        // WAV header size is 44 bytes
        char riffID[4];         // "RIFF"
        uint32_t riffSize;      // 36 + (numSamples * numChannels * bytesPerSample)
        char waveID[4];         // "WAVE"
        char fmtID[4];          // "fmt "
        uint32_t fmtSize;       // 16 for PCM; 18 for IEEE float
        uint16_t audioFormat;   // 1 for PCM; 3 for IEEE float
        uint16_t numChannels;   // 1 for mono; 2 for stereo
        uint32_t sampleRate;    // sample rate
        uint32_t byteRate;      // sampleRate * numChannels * bytesPerSample
        uint16_t blockAlign;    // numChannels * bytesPerSample
        uint16_t bitsPerSample; // 8 for 8-bit; 16 for 16-bit; 32 for 32-bit
        char dataID[4];         // "data"
        uint32_t dataSize;      // numSamples * numChannels * bytesPerSample

        void write(std::ostream& out) const {
            out.write(riffID, 4);
            out.write(reinterpret_cast<const char*>(&riffSize), 4);
            out.write(waveID, 4);
            out.write(fmtID, 4);
            out.write(reinterpret_cast<const char*>(&fmtSize), 4);
            out.write(reinterpret_cast<const char*>(&audioFormat), 2);
            out.write(reinterpret_cast<const char*>(&numChannels), 2);
            out.write(reinterpret_cast<const char*>(&sampleRate), 4);
            out.write(reinterpret_cast<const char*>(&byteRate), 4);
            out.write(reinterpret_cast<const char*>(&blockAlign), 2);
            out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
            out.write(dataID, 4);
            out.write(reinterpret_cast<const char*>(&dataSize), 4);
        }

        void read(std::istream& in) {
            in.read(riffID, 4);
            in.read(reinterpret_cast<char*>(&riffSize), 4);
            in.read(waveID, 4);
            in.read(fmtID, 4);
            in.read(reinterpret_cast<char*>(&fmtSize), 4);
            in.read(reinterpret_cast<char*>(&audioFormat), 2);
            in.read(reinterpret_cast<char*>(&numChannels), 2);
            in.read(reinterpret_cast<char*>(&sampleRate), 4);
            in.read(reinterpret_cast<char*>(&byteRate), 4);
            in.read(reinterpret_cast<char*>(&blockAlign), 2);
            in.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            in.read(dataID, 4);
            in.read(reinterpret_cast<char*>(&dataSize), 4);
        }

        void print() const {
            printf("RiffID: %.4s\n", riffID);
            printf("RiffSize: %u\n", (unsigned)riffSize);
            printf("WaveID: %.4s\n", waveID);
            printf("FmtID: %.4s\n", fmtID);
            printf("FmtSize: %u\n", (unsigned)fmtSize);
            printf("AudioFormat: %u\n", (unsigned)audioFormat);
            printf("NumChannels: %u\n", (unsigned)numChannels);
            printf("SampleRate: %u\n", (unsigned)sampleRate);
            printf("ByteRate: %u\n", (unsigned)byteRate);
            printf("BlockAlign: %u\n", (unsigned)blockAlign);
            printf("BitsPerSample: %u\n", (unsigned)bitsPerSample);
            printf("DataID: %.4s\n", dataID);
            printf("DataSize: %u\n", (unsigned)dataSize);
        }
    };

    bool createForWriting(const std::string& filePath,
                          const uint32_t sampleRate,
                          const uint32_t numChannels,
                          const uint32_t bitsPerSample) {
        filePath_ = filePath;
        fileStream_.open(filePath_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!fileStream_.is_open()) {
            return false;
        }

        // Initialize header
        memcpy(header_.riffID, "RIFF", 4);
        memcpy(header_.waveID, "WAVE", 4);
        memcpy(header_.fmtID, "fmt ", 4);
        memcpy(header_.dataID, "data", 4);

        header_.fmtSize = 16;
        header_.audioFormat = 1; // PCM
        header_.numChannels = numChannels;
        header_.sampleRate = sampleRate;
        header_.bitsPerSample = bitsPerSample;

        uint32_t bytesPerSample = bitsPerSample / 8;
        header_.byteRate = sampleRate * numChannels * bytesPerSample;
        header_.blockAlign = numChannels * bytesPerSample;
        header_.dataSize = 0;  // Will be updated as data is written
        header_.riffSize = 36; // Header size (36) + dataSize (0 initially)

        // Write initial header with placeholder values
        header_.write(fileStream_);
        isHeaderValid_ = true;
        dataSizePos_ = fileStream_.tellp() - std::streamoff(4); // Position of dataSize field

        return fileStream_.good();
    }

    bool openForReading(const std::string& filePath) {
        filePath_ = filePath;
        fileStream_.open(filePath_, std::ios::binary | std::ios::in);
        if (!fileStream_.is_open()) {
            return false;
        }

        header_.read(fileStream_);
        // Basic WAV header validation
        if (strncmp(header_.riffID, "RIFF", 4) != 0 || strncmp(header_.waveID, "WAVE", 4) != 0 ||
            strncmp(header_.fmtID, "fmt ", 4) != 0 || strncmp(header_.dataID, "data", 4) != 0) {
            return false;
        }
        if (header_.fmtSize < 16 || (header_.audioFormat != 1 && header_.audioFormat != 3) ||
            header_.numChannels == 0 || header_.sampleRate == 0) {
            return false;
        }

        isHeaderValid_ = true;
        return fileStream_.good();
    }

    size_t writeData(const char* data, const size_t size) {
        if (!fileStream_.is_open() || !isHeaderValid_ || !data || size == 0) {
            return 0;
        }

        // Prevent 32-bit overflow of WAV header sizes
        if (header_.dataSize > UINT32_MAX - size) {
            return 0;
        }

        fileStream_.write(data, size);
        if (fileStream_.good()) {
            // Update header sizes
            header_.dataSize += static_cast<uint32_t>(size);
            header_.riffSize = 36 + header_.dataSize;
            return size;
        }
        return 0;
    }

    void updateHeader() {
        if (fileStream_.is_open() && isHeaderValid_) {
            const auto currentPos = fileStream_.tellp();

            // Update RIFF chunk size
            fileStream_.seekp(4, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char*>(&header_.riffSize), sizeof(header_.riffSize));

            // Update data chunk size
            fileStream_.seekp(dataSizePos_);
            fileStream_.write(reinterpret_cast<const char*>(&header_.dataSize), sizeof(header_.dataSize));

            fileStream_.flush();
            fileStream_.seekp(currentPos);
        }
    }

    size_t readData(char* data, const size_t size) const {
        if (!fileStream_.is_open() || !isHeaderValid_) {
            return 0;
        }

        fileStream_.read(data, size);
        return fileStream_.gcount();
    }

    void finalize() {
        if (fileStream_.is_open() && isHeaderValid_) {
            updateHeader();
            fileStream_.close();
        }
    }

    void close() {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
    }

    const std::string& getFilePath() const { return filePath_; }
    const Header& getHeader() const { return header_; }
    int32_t getSampleRate() const { return header_.sampleRate; }
    int32_t getNumChannels() const { return header_.numChannels; }
    uint32_t getBitsPerSample() const { return header_.bitsPerSample; }
    audio_format_t getAudioFormat() const {
        if (header_.audioFormat != 1) {
            return AUDIO_FORMAT_INVALID;
        }
        switch (header_.bitsPerSample) {
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
    }

private:
    Header header_{};
    std::string filePath_;
    mutable std::fstream fileStream_;
    bool isHeaderValid_{false};
    std::streampos dataSizePos_{};
};

/************************** BufferManager class ******************************/
class BufferManager {
public:
    BufferManager(size_t bufferSize) {
        // Check for reasonable buffer size limits
        const size_t MAX_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB max
        const size_t MIN_BUFFER_SIZE = 480;              // Minimum reasonable buffer size

        bufferSize = std::clamp(bufferSize, MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);
        printf("Buffer size: %zu\n", bufferSize);

        try {
            buffer = std::make_unique<char[]>(bufferSize);
            size = bufferSize;
        } catch (const std::bad_alloc& e) {
            size = 0;
            printf("Error: Failed to allocate buffer of size %zu: %s\n", bufferSize, e.what());
        }
    }

    ~BufferManager() = default;

    char* get() { return buffer.get(); }
    size_t getSize() const { return size; }
    bool isValid() const { return buffer != nullptr && size > 0; }

private:
    std::unique_ptr<char[]> buffer;
    size_t size{0};
};

/************************** Audio Utility Functions ******************************/
class AudioUtils {
public:
    // Map -f option values to audio_format_t
    static audio_format_t parseFormatOption(const int v) {
        static const std::unordered_map<int, audio_format_t> formatMap = {
            {1, AUDIO_FORMAT_PCM_16_BIT},   {2, AUDIO_FORMAT_PCM_8_BIT},         {3, AUDIO_FORMAT_PCM_32_BIT},
            {4, AUDIO_FORMAT_PCM_8_24_BIT}, {6, AUDIO_FORMAT_PCM_24_BIT_PACKED},
        };
        const auto it = formatMap.find(v);
        return (it != formatMap.end()) ? it->second : AUDIO_FORMAT_PCM_16_BIT;
    }

    // Function to get the current time in a specific format
    static void getFormatTime(char* formatTime) {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        strftime(formatTime, 32, "%Y%m%d_%H.%M.%S", now);
    }

    // Function to get timestamp for logging (with millisecond precision)
    static void getTimestamp(char* timestamp) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);

        struct tm* now = localtime(&tv.tv_sec);
        snprintf(timestamp, 16, "%02d:%02d:%02d.%03d", now->tm_hour, now->tm_min, now->tm_sec,
                 static_cast<int>(tv.tv_usec / 1000));
    }

    // Build default record file path with timestamp unless an override is provided
    static std::string makeRecordFilePath(const int32_t sampleRate,
                                          const int32_t channelCount,
                                          const uint32_t bitsPerSample,
                                          const std::string& overridePath) {
        if (!overridePath.empty()) {
            return overridePath;
        }
        char audioFile[256] = {0};
        char formatTime[32] = {0};
        AudioUtils::getFormatTime(formatTime);
        // 在Android系统上，/sdcard/Android/data目录通常只需要标准存储权限
        snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%ubit_%s.wav", sampleRate, channelCount,
                 bitsPerSample, formatTime);
        return std::string(audioFile);
    }
};

/************************** Global Variables ******************************/
// Global exit flag for signal handling
static std::atomic<bool> g_exitRequested(false);

/************************** Audio Configuration ******************************/
struct AudioConfig {
    // Common parameters
    int32_t sampleRate = 48000;
    int32_t channelCount = 2;
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;
    size_t minFrameCount = 0; // will be calculated

    // Recording parameters
    audio_source_t inputSource = AUDIO_SOURCE_MIC;
    audio_input_flags_t inputFlag = AUDIO_INPUT_FLAG_NONE;
    int32_t durationSeconds = 0;     // 0 = unlimited
    std::string recordFilePath = ""; // will be generated if empty

    // Playback parameters
    audio_usage_t usage = AUDIO_USAGE_MEDIA;
    audio_content_type_t contentType = AUDIO_CONTENT_TYPE_UNKNOWN;
    audio_output_flags_t outputFlag = AUDIO_OUTPUT_FLAG_NONE;
    std::string playFilePath = "/data/audio_test.wav";

    // Set params parameters
    std::vector<int32_t> setParams = {};
};

/************************** AudioMode Definitions ******************************/
enum AudioMode { MODE_INVALID = -1, MODE_RECORD = 0, MODE_PLAY = 1, MODE_DUPLEX = 2, MODE_SET_PARAMS = 100 };

/************************** Audio Parameter Manager ******************************/
static const String8 PARAM_OPEN_SOURCE = String8("open_source");   // open_source参数名
static const String8 PARAM_CLOSE_SOURCE = String8("close_source"); // close_source参数名
static const String8 PARAM_CHANNEL_MASK = String8("channel_mask"); // channel_mask参数名

class AudioParameterManager {
public:
    explicit AudioParameterManager(const AudioConfig& config) : mConfig(config) {}
    virtual ~AudioParameterManager() = default;

    // Helper function to convert audio_usage_t enum to string
    String8 audioUsageToString(audio_usage_t usage);

    // Set open source parameter with specific audio usage
    void setOpenSourceWithUsage(audio_usage_t usage);

    // Set close source parameter with specific audio usage
    void setCloseSourceWithUsage(audio_usage_t usage);

    // Set channel mask parameter for AudioTrack
    void setChannelMask(const sp<AudioTrack>& audioTrack, audio_channel_mask_t channelMask);

private:
    AudioConfig mConfig;

    // Unified interface for AudioSystem::setParameters
    void setSystemParameter(const String8& key, const String8& value);

    // Unified interface for audioTrack->setParameters
    void setAudioTrackParameter(const sp<AudioTrack>& audioTrack, const String8& key, const String8& value);
};

String8 AudioParameterManager::audioUsageToString(audio_usage_t usage) {
    static const std::unordered_map<audio_usage_t, const char*> usageMap = {
        {AUDIO_USAGE_UNKNOWN, "AUDIO_USAGE_UNKNOWN"},
        {AUDIO_USAGE_MEDIA, "AUDIO_USAGE_MEDIA"},
        {AUDIO_USAGE_VOICE_COMMUNICATION, "AUDIO_USAGE_VOICE_COMMUNICATION"},
        {AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING, "AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING"},
        {AUDIO_USAGE_ALARM, "AUDIO_USAGE_ALARM"},
        {AUDIO_USAGE_NOTIFICATION, "AUDIO_USAGE_NOTIFICATION"},
        {AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE, "AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE"},
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST"},
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT"},
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED"},
        {AUDIO_USAGE_NOTIFICATION_EVENT, "AUDIO_USAGE_NOTIFICATION_EVENT"},
        {AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY, "AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY"},
        {AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE, "AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE"},
        {AUDIO_USAGE_ASSISTANCE_SONIFICATION, "AUDIO_USAGE_ASSISTANCE_SONIFICATION"},
        {AUDIO_USAGE_GAME, "AUDIO_USAGE_GAME"},
        {AUDIO_USAGE_ASSISTANT, "AUDIO_USAGE_ASSISTANT"},
    };
    const char* usageName = "AUDIO_USAGE_UNKNOWN";
    const auto it = usageMap.find(usage);
    if (it != usageMap.end()) {
        usageName = it->second;
    }
    return String8::format("0:%s", usageName);
}

void AudioParameterManager::setOpenSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(PARAM_OPEN_SOURCE, audioUsageToString(usage));
}

void AudioParameterManager::setCloseSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(PARAM_CLOSE_SOURCE, audioUsageToString(usage));
}

void AudioParameterManager::setChannelMask(const sp<AudioTrack>& audioTrack, const audio_channel_mask_t channelMask) {
    setAudioTrackParameter(audioTrack, PARAM_CHANNEL_MASK, String8::format("%d", channelMask));
}

void AudioParameterManager::setSystemParameter(const String8& key, const String8& value) {
#if SET_PARAMS_ENABLE
    AudioParameter audioParam;
    audioParam.add(key, value);
    String8 paramString = audioParam.toString();
    AudioSystem::setParameters(paramString);
    printf("Set parameter: %s\n", paramString.c_str());
#endif
}

void AudioParameterManager::setAudioTrackParameter(const sp<AudioTrack>& audioTrack,
                                                   const String8& key,
                                                   const String8& value) {
#if SET_PARAMS_ENABLE
    AudioParameter audioParam;
    audioParam.add(key, value);
    String8 paramString = audioParam.toString();
    audioTrack->setParameters(paramString);
    printf("Set parameter: %s\n", paramString.c_str());
#endif
}

/************************** Audio Operation Base Class ******************************/
class AudioOperation {
public:
    explicit AudioOperation(const AudioConfig& config) : mConfig(config), mParamManager(config) {
        setupSignalHandler();
    }
    virtual ~AudioOperation() = default;
    virtual int32_t execute() = 0;

protected:
    AudioConfig mConfig;
    AudioParameterManager mParamManager;
    static constexpr uint32_t kMaxAudioDataSize = 2u * 1024u * 1024u * 1024u; // 2 GiB
    static constexpr uint32_t kProgressReportInterval = 10;                   // report progress every 10 seconds
    static constexpr uint32_t kLevelMeterInterval = 30;                       // Update level meter every 30 frames
    uint32_t mLevelMeterCounter = 0;                                          // For level meter updates

    // Enum for audio component types to avoid string comparisons
    enum class AudioComponentType { AUDIO_RECORD, AUDIO_TRACK };

    // Common success logging helper (console only)
    void logInfo(const char* message, const char* details = nullptr) const {
        if (details) {
            printf("%s %s\n", message, details);
        } else {
            printf("%s\n", message);
        }
    }

    // Common warning logging helper (console only)
    void logWarning(const char* message, const char* details = nullptr) const {
        if (details) {
            printf("Warning: %s %s\n", message, details);
            ALOGW("%s %s", message, details);
        } else {
            printf("Warning: %s\n", message);
            ALOGW("%s", message);
        }
    }

    // Common error logging helper
    void logError(const char* message, const char* details = nullptr) const {
        if (details) {
            printf("Error: %s %s\n", message, details);
            ALOGE("%s %s", message, details);
        } else {
            printf("Error: %s\n", message);
            ALOGE("%s", message);
        }
    }

    // Common buffer size calculation helper
    size_t calculateBufferSize() const {
        const size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        return (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
    }

    // Common frame count calculation helper
    size_t calculateFrameCount() const {
        const size_t minFrames = static_cast<size_t>((mConfig.sampleRate * 10) / 1000);
        const size_t adjustedMinFrameCount = std::max(mConfig.minFrameCount, minFrames);
        return adjustedMinFrameCount * 2;
    }

    // Common bytes per second calculation helper
    uint64_t calculateBytesPerSecond() const {
        const size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        return static_cast<uint64_t>(mConfig.sampleRate) * mConfig.channelCount * bytesPerSample;
    }

    // Common validation function for audio parameters
    bool validateAudioParameters() {
        if (mConfig.sampleRate <= 0) {
            logError("Invalid sample rate specified");
            return false;
        }

        if (mConfig.channelCount <= 0 || mConfig.channelCount > 32) {
            logError("Invalid channel count specified");
            return false;
        }

        if (audio_bytes_per_sample(mConfig.format) == 0) {
            logError("Invalid audio format specified");
            return false;
        }

        return true;
    }

    // Common attribution source creation helper
    AttributionSourceState createAttributionSource() {
        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();
        attributionSource.uid = getuid();
        attributionSource.pid = getpid();
        return attributionSource;
    }

    // Common AudioRecord initialization helper
    bool
    initializeAudioRecordHelper(sp<AudioRecord>& audioRecord, audio_channel_mask_t channelMask, size_t frameCount) {
        AttributionSourceState attributionSource = createAttributionSource();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.source = mConfig.inputSource;

        logInfo("Initialize AudioRecord",
                String8::format(
                    "source: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: 0x%x, frameCount: %zu",
                    mConfig.inputSource, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask,
                    frameCount)
                    .c_str());

        audioRecord = sp<AudioRecord>::make(attributionSource);
        if (audioRecord->set(mConfig.inputSource, // input source
                             mConfig.sampleRate,  // sample rate
                             mConfig.format,      // audio format
                             channelMask,         // channel mask
                             frameCount,          // frame count
                             nullptr,             // callback/IAudioRecordCallback. use TRANSFER_CALLBACK
#ifndef _INTERFACE_V2_
                             nullptr, // user mCallbackData
#endif
                             0,                          // notificationFrames
                             false,                      // threadCanCallJava
                             AUDIO_SESSION_ALLOCATE,     // sessionId
                             AudioRecord::TRANSFER_SYNC, // transferType
                             mConfig.inputFlag,          // inputFlag
                             getuid(),                   // uid
                             getpid(),                   // pid
                             &attributes,                // audioAttributes
                             AUDIO_PORT_HANDLE_NONE      // selectedDeviceId
                             ) != NO_ERROR) {
            logError("Failed to set AudioRecord parameters");
            return false;
        }

        if (audioRecord->initCheck() != NO_ERROR) {
            logError("AudioRecord initialization check failed");
            return false;
        }

        return true;
    }

    // Common AudioTrack initialization helper
    bool initializeAudioTrackHelper(sp<AudioTrack>& audioTrack, audio_channel_mask_t channelMask, size_t frameCount) {
        AttributionSourceState attributionSource = createAttributionSource();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.usage = mConfig.usage;
        attributes.content_type = mConfig.contentType;

        logInfo("Initialize AudioTrack",
                String8::format(
                    "usage: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: 0x%x, frameCount: %zu",
                    mConfig.usage, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount)
                    .c_str());

        audioTrack = sp<AudioTrack>::make(attributionSource);
        if (audioTrack->set(AUDIO_STREAM_DEFAULT, // streamType
                            mConfig.sampleRate,   // sampleRate
                            mConfig.format,       // audioFormat
                            channelMask,          // channelMask
                            frameCount,           // frameCount
                            mConfig.outputFlag,   // outputFlag
                            nullptr,              // callback/IAudioTrackCallback. use TRANSFER_CALLBACK
#ifndef _INTERFACE_V2_
                            nullptr, // userCallbackData
#endif
                            0,                         // notificationFrames
                            nullptr,                   // sharedBuffer, use TRANSFER_SHARED
                            false,                     // threadCanCallJava
                            AUDIO_SESSION_ALLOCATE,    // sessionId
                            AudioTrack::TRANSFER_SYNC, // transferType
                            nullptr,                   // offloadInfo
                            attributionSource,         // attributionSource
                            &attributes,               // pAttributes
                            false,                     // doNotReconnect
                            1.0f,                      // maxRequiredSpeed
                            AUDIO_PORT_HANDLE_NONE     // selectedDeviceId
                            ) != NO_ERROR) {
            logError("Failed to set AudioTrack parameters");
            return false;
        }

        if (audioTrack->initCheck() != NO_ERROR) {
            logError("AudioTrack initialization check failed");
            return false;
        }

        return true;
    }

    // Generic audio component start function that works with both AudioRecord and AudioTrack
    template <typename AudioComponent>
    bool startAudioComponent(const sp<AudioComponent>& component, AudioComponentType componentType) {
        const char* componentName = (componentType == AudioComponentType::AUDIO_RECORD) ? "AudioRecord" : "AudioTrack";
        logInfo("Starting", componentName);

        // set params before AudioTrack.start()
        if (componentType == AudioComponentType::AUDIO_TRACK) {
            mParamManager.setOpenSourceWithUsage(mConfig.usage);
        }

        status_t startResult = component->start();
        if (startResult != NO_ERROR) {
            logError("Audio component start failed",
                     String8::format("%s with status %d", componentName, startResult).c_str());
            return false;
        }
        return true;
    }

    // Common WAV file setup function
    bool setupWavFileHelper(WAVFile& wavFile, bool isRecordMode) {
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);

        if (isRecordMode) {
            mConfig.recordFilePath = AudioUtils::makeRecordFilePath(mConfig.sampleRate, mConfig.channelCount,
                                                                    bytesPerSample * 8, mConfig.recordFilePath);

            logInfo("Recording audio to file", mConfig.recordFilePath.c_str());

            if (!wavFile.createForWriting(mConfig.recordFilePath, mConfig.sampleRate, mConfig.channelCount,
                                          bytesPerSample * 8)) {
                logError("Can't create record file", mConfig.recordFilePath.c_str());
                return false;
            }
        } else {
            if (mConfig.playFilePath.empty() || access(mConfig.playFilePath.c_str(), F_OK) == -1) {
                logError("File does not exist", mConfig.playFilePath.c_str());
                return false;
            }

            // Open WAV file for reading
            if (!wavFile.openForReading(mConfig.playFilePath)) {
                logError("Failed to open WAV file", mConfig.playFilePath.c_str());
                return false;
            }

            mConfig.sampleRate = wavFile.getSampleRate();
            mConfig.channelCount = wavFile.getNumChannels();
            mConfig.format = wavFile.getAudioFormat();
            logInfo("Playback audio file",
                    String8::format("%s, sampleRate: %d, channelCount: %d, format: %d", mConfig.playFilePath.c_str(),
                                    mConfig.sampleRate, mConfig.channelCount, mConfig.format)
                        .c_str());
        }

        return true;
    }

    // Common progress reporting function for both recording and playback
    bool reportProgress(const uint64_t totalBytesProcessed,
                        const uint64_t bytesPerSecond,
                        AudioComponentType operationType,
                        WAVFile* wavFile = nullptr) {
        static uint64_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
        const char* operationTypeName = (operationType == AudioComponentType::AUDIO_RECORD) ? "Recording" : "Playing";

        if (totalBytesProcessed >= nextProgressReport) {
            printf("%s ... , processed %.2f seconds, %.2f MB\n", operationTypeName,
                   static_cast<float>(totalBytesProcessed) / bytesPerSecond,
                   static_cast<float>(totalBytesProcessed) / (1024u * 1024u));
            nextProgressReport += bytesPerSecond * kProgressReportInterval;

            // Update file header if it's a recording operation and wavFile is provided
            if (wavFile && operationType == AudioComponentType::AUDIO_RECORD) {
                wavFile->updateHeader();
            }
            return true;
        }
        return false;
    }

    // Signal handler for SIGINT (Ctrl+C)
    static void signalHandler(int signal) {
        if (signal == SIGINT) {
            g_exitRequested.store(true);
        }
    }

    // Setup signal handler
    void setupSignalHandler() { signal(SIGINT, signalHandler); }

    // Simple peak level meter implementation with low CPU usage
    void updateLevelMeter(const char* buffer, size_t size) {
        // Only update level meter every kLevelMeterInterval frames
        if (++mLevelMeterCounter % kLevelMeterInterval != 0) {
            return;
        }

        constexpr float NORM_16BIT = 32768.0f;
        constexpr float NORM_32BIT = 2147483648.0f;
        constexpr float DB_FLOOR = -60.0f;

        const size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        if (size == 0 || bytesPerSample == 0) {
            printf("Error: Invalid input size or bytesPerSample\n");
            return;
        }

        const size_t numSamples = size / bytesPerSample;
        float peakAmplitude = 0.0f;
        // Process 16-bit and 32-bit integer audio
        if (bytesPerSample == 2) {
            const int16_t* int16Data = reinterpret_cast<const int16_t*>(buffer);
            for (size_t i = 0; i < numSamples; ++i) {
                peakAmplitude = std::max(peakAmplitude, static_cast<float>(std::abs(int16Data[i])) / NORM_16BIT);
            }
        } else if (bytesPerSample == 4) {
            const int32_t* int32Data = reinterpret_cast<const int32_t*>(buffer);
            for (size_t i = 0; i < numSamples; ++i) {
                peakAmplitude = std::max(peakAmplitude, static_cast<float>(std::abs(int32Data[i])) / NORM_32BIT);
            }
        } else {
            printf("Error: Unsupported audio format for level meter\n");
            return;
        }

        // Convert to dB scale (with floor at -60dB)
        const float dbLevel = peakAmplitude > 0.0f ? std::max(20.0f * std::log10(peakAmplitude), DB_FLOOR) : DB_FLOOR;
        char timestamp[16] = {0};
        AudioUtils::getTimestamp(timestamp);
        printf("[%s] Audio Level: %.1f dB, bytes: %zu\n", timestamp, dbLevel, size);
    }
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation : public AudioOperation {
public:
    explicit AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioRecord> audioRecord;

        // Validate parameters and initialize AudioRecord/WAVFile
        if (!setupWavFileHelper(wavFile, true) || !validateAudioParameters() || !initializeAudioRecord(audioRecord)) {
            logError("Failed to setup WAV file or initialize AudioRecord");
            return -1;
        }

        // Start recording
        if (!startRecording(audioRecord)) {
            wavFile.close();
            return -1;
        }

        // Main recording loop
        int32_t result = recordLoop(audioRecord, wavFile);

        // Cleanup
        if (audioRecord != nullptr) {
            audioRecord->stop();
        }
        wavFile.finalize();

        return result;
    }

private:
    bool initializeAudioRecord(sp<AudioRecord>& audioRecord) {
        audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(mConfig.channelCount);

        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMask) !=
            NO_ERROR) {
            logWarning("Cannot get min frame count, using default value", nullptr);
        }

        const size_t frameCount = calculateFrameCount();
        return initializeAudioRecordHelper(audioRecord, channelMask, frameCount);
    }

    bool startRecording(const sp<AudioRecord>& audioRecord) {
        return startAudioComponent(audioRecord, AudioComponentType::AUDIO_RECORD);
    }

    int32_t recordLoop(const sp<AudioRecord>& audioRecord, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* const buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            logInfo("Recording", String8::format("for %d seconds...", mConfig.durationSeconds).c_str());
        } else {
            logInfo("Recording started", "Press Ctrl+C to stop");
        }

        const uint64_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint64_t>(mConfig.durationSeconds) * calculateBytesPerSecond(),
                           static_cast<uint64_t>(kMaxAudioDataSize))
                : static_cast<uint64_t>(kMaxAudioDataSize);

        uint64_t totalBytesRead = 0;
        while (totalBytesRead < maxBytesToRecord && !g_exitRequested) {
            const ssize_t bytesRead = audioRecord->read(buffer, calculateBufferSize());
            if (bytesRead < 0) {
                logError("AudioRecord read failed", String8::format("%zd", bytesRead).c_str());
                break;
            }
            if (bytesRead == 0) {
                continue;
            }
            totalBytesRead += static_cast<uint64_t>(bytesRead);

            // Update level meter
            updateLevelMeter(buffer, static_cast<size_t>(bytesRead));

            // Write data to WAV file
            if (wavFile.writeData(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
                logError("Failed to save audio data to file");
                break;
            }

            // Report progress
            reportProgress(totalBytesRead, calculateBytesPerSecond(), AudioComponentType::AUDIO_RECORD, &wavFile);
        }

        logInfo("Recording finished",
                String8::format("Recorded %lu bytes, File saved: %s", totalBytesRead, wavFile.getFilePath().c_str())
                    .c_str());

        return 0;
    }
};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation : public AudioOperation {
public:
    explicit AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioTrack> audioTrack;

        if (!setupWavFileHelper(wavFile, false) || !validateAudioParameters() || !initializeAudioTrack(audioTrack)) {
            logError("Failed to setup WAV file or initialize AudioTrack");
            return -1;
        }

        // Start playback
        if (!startPlaying(audioTrack)) {
            return -1;
        }

        // Main playback loop
        int32_t result = playLoop(audioTrack, wavFile);

        // Cleanup
        if (audioTrack != nullptr) {
            audioTrack->stop();
            mParamManager.setCloseSourceWithUsage(mConfig.usage); // set params after AudioTrack.stop()
        }
        wavFile.close();

        return result;
    }

private:
    bool initializeAudioTrack(sp<AudioTrack>& audioTrack) {
        audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(mConfig.channelCount);
        const size_t frameCount = calculateFrameCount();
        logInfo("AudioTrack initialized",
                String8::format("minFrameCount: %zu, frameCount: %zu", mConfig.minFrameCount, frameCount).c_str());

        return initializeAudioTrackHelper(audioTrack, channelMask, frameCount);
    }

    bool startPlaying(const sp<AudioTrack>& audioTrack) {
        return startAudioComponent(audioTrack, AudioComponentType::AUDIO_TRACK);
    }

    int32_t playLoop(const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* const buffer = bufferManager.get();

        logInfo("Playing audio from", mConfig.playFilePath.c_str());
        uint64_t totalBytesPlayed = 0;
        while (!g_exitRequested) {
            const size_t bytesRead = wavFile.readData(buffer, calculateBufferSize());
            if (bytesRead == 0) {
                logInfo("End of file reached", nullptr);
                break;
            }

            size_t bytesWritten = 0;
            const size_t bytesToWrite = bytesRead;
            while (bytesWritten < bytesToWrite) {
                const ssize_t written = audioTrack->write(buffer + bytesWritten, bytesToWrite - bytesWritten);
                if (written < 0) {
                    logError("AudioTrack write failed", String8::format("%zd", written).c_str());
                    return -1;
                }
                bytesWritten += static_cast<size_t>(written);
            }
            // Update total bytes played
            totalBytesPlayed += static_cast<uint64_t>(bytesWritten);

            // Update level meter
            updateLevelMeter(buffer, bytesRead);

            // Report progress
            reportProgress(totalBytesPlayed, calculateBytesPerSecond(), AudioComponentType::AUDIO_TRACK);
        }

        logInfo("Playback finished", String8::format("Total bytes played: %lu", totalBytesPlayed).c_str());

        return 0;
    }
};

/************************** Audio Duplex Operation ******************************/
class AudioDuplexOperation : public AudioOperation {
public:
    explicit AudioDuplexOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioRecord> audioRecord;
        sp<AudioTrack> audioTrack;

        // Validate parameters and initialize audio components
        if (!setupWavFileHelper(wavFile, true) || !validateAudioParameters() ||
            !initializeAudioComponents(audioRecord, audioTrack)) {
            logError("Failed to initialize audio components or setup WAV file");
            return -1;
        }

        // Start recording and playback
        if (!startAudioComponents(audioRecord, audioTrack)) {
            wavFile.close();
            return -1;
        }

        // Main duplex loop
        int32_t result = duplexLoop(audioRecord, audioTrack, wavFile);

        // Cleanup
        if (audioRecord != nullptr) {
            audioRecord->stop();
        }
        if (audioTrack != nullptr) {
            audioTrack->stop();
            mParamManager.setCloseSourceWithUsage(mConfig.usage); // set params after AudioTrack.stop()
        }
        wavFile.finalize();

        return result;
    }

private:
    bool initializeAudioComponents(sp<AudioRecord>& audioRecord, sp<AudioTrack>& audioTrack) {
        audio_channel_mask_t channelMaskIn = audio_channel_in_mask_from_count(mConfig.channelCount);
        audio_channel_mask_t channelMaskOut = audio_channel_out_mask_from_count(mConfig.channelCount);

        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMaskIn) !=
            NO_ERROR) {
            logWarning("Cannot get min frame count, using default value", nullptr);
        }

        const size_t frameCount = calculateFrameCount();
        if (!initializeAudioRecordHelper(audioRecord, channelMaskIn, frameCount) ||
            !initializeAudioTrackHelper(audioTrack, channelMaskOut, frameCount)) {
            return false;
        }
        return true;
    }

    bool startAudioComponents(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack) {
        if (!startAudioComponent(audioRecord, AudioComponentType::AUDIO_RECORD)) {
            return false;
        }

        if (!startAudioComponent(audioTrack, AudioComponentType::AUDIO_TRACK)) {
            if (audioRecord != nullptr) {
                audioRecord->stop();
            }
            return false;
        }

        return true;
    }

    int32_t duplexLoop(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* const buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            logInfo("Duplex audio started",
                    String8::format("Recording for %d seconds...", mConfig.durationSeconds).c_str());
        } else {
            logInfo("Duplex audio started", "Press Ctrl+C to stop");
        }

        const uint64_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint64_t>(mConfig.durationSeconds) * calculateBytesPerSecond(),
                           static_cast<uint64_t>(kMaxAudioDataSize))
                : static_cast<uint64_t>(kMaxAudioDataSize);

        uint64_t totalBytesRead = 0;
        uint64_t totalBytesPlayed = 0;
        while (totalBytesRead < maxBytesToRecord && !g_exitRequested) {
            const ssize_t bytesRead = audioRecord->read(buffer, calculateBufferSize());
            if (bytesRead < 0) {
                logError("AudioRecord read failed", String8::format("%zd", bytesRead).c_str());
                break;
            }
            if (bytesRead == 0) {
                continue;
            }
            totalBytesRead += static_cast<uint64_t>(bytesRead);

            // Update level meter for recording
            updateLevelMeter(buffer, static_cast<size_t>(bytesRead));

            // Write to WAV file
            if (wavFile.writeData(buffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
                logError("Failed to save audio data to file");
                // break; // Continue playing if save failed
            }

            // Report progress for recording
            reportProgress(totalBytesRead, calculateBytesPerSecond(), AudioComponentType::AUDIO_RECORD, &wavFile);

            // Check recording finish
            if (totalBytesRead >= maxBytesToRecord) {
                break;
            }

            size_t bytesWritten = 0;
            const size_t bytesToWrite = static_cast<size_t>(bytesRead);
            while (bytesWritten < bytesToWrite) {
                const ssize_t written = audioTrack->write(buffer + bytesWritten, bytesToWrite - bytesWritten);
                if (written < 0) {
                    logError("AudioTrack write failed", String8::format("%zd", written).c_str());
                    goto duplex_end;
                }
                bytesWritten += static_cast<size_t>(written);
            }
            totalBytesPlayed += static_cast<uint64_t>(bytesWritten);
        }

    duplex_end:
        logInfo("Duplex audio completed",
                String8::format("Total bytes read: %lu, Total bytes played: %lu, File saved: %s", totalBytesRead,
                                totalBytesPlayed, wavFile.getFilePath().c_str())
                    .c_str());

        return 0;
    }
};

/************************** Set Parameters Operation ******************************/
class SetParamsOperation : public AudioOperation {
public:
    explicit SetParamsOperation(const AudioConfig& config, const std::vector<int32_t>& params)
        : AudioOperation(config), mParams(params) {}

    int32_t execute() override {
        printf("SetParams operation started with %zu parameters\n", mParams.size());

        for (size_t i = 0; i < mParams.size(); ++i) {
            printf("  Parameter %zu: %d\n", i + 1, mParams[i]);
        }

        if (mParams.empty()) {
            printf("Error: No parameters provided\n");
            return -1;
        }

        int32_t sourceType = mParams[0];
        if (mParams.size() < 2) {
            printf("Error: Missing audio usage parameter\n");
            return -1;
        }
        int32_t usageValue = mParams[1];
        audio_usage_t usage = static_cast<audio_usage_t>(usageValue);

        switch (sourceType) {
        case 1:
            printf("Setting open_source with usage: %d\n", usage);
            mParamManager.setOpenSourceWithUsage(usage);
            break;
        case 2:
            printf("Setting close_source with usage: %d\n", usage);
            mParamManager.setCloseSourceWithUsage(usage);
            break;
        default:
            printf("Error: Unknown primary parameter %d (1=open_source, 2=close_source)\n", sourceType);
            return -1;
        }

        printf("SetParams operation completed\n");
        return 0;
    }

    std::vector<int32_t> mParams;
};

/************************** Audio Operation Factory ******************************/
class AudioOperationFactory {
public:
    static std::unique_ptr<AudioOperation> createOperation(AudioMode mode, const AudioConfig& config) {
        switch (mode) {
        case MODE_RECORD:
            printf("Creating RECORD operation\n");
            return std::make_unique<AudioRecordOperation>(config);

        case MODE_PLAY:
            printf("Creating PLAY operation\n");
            return std::make_unique<AudioPlayOperation>(config);

        case MODE_DUPLEX:
            printf("Creating DUPLEX operation\n");
            return std::make_unique<AudioDuplexOperation>(config);

        case MODE_SET_PARAMS:
            printf("Creating SET_PARAMS operation\n");
            return std::make_unique<SetParamsOperation>(config, config.setParams);

        default:
            printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
            return nullptr;
        }
    }
};

/************************** Command Line Parser ******************************/
class CommandLineParser {
public:
    static void parseArguments(int32_t argc, char** argv, AudioMode& mode, AudioConfig& config) {
        int32_t opt = 0;
        int32_t xParam = -1; // -x parameter for set params mode
        int32_t yParam = -1; // -y parameter for set params mode
        while ((opt = getopt(argc, argv, "m:s:r:c:f:F:u:C:O:z:d:h:x:y:")) != -1) {
            switch (opt) {
            case 'm': // mode
                mode = static_cast<AudioMode>(atoi(optarg));
                break;
            case 's': // audio source
                config.inputSource = static_cast<audio_source_t>(atoi(optarg));
                break;
            case 'r': // sample rate
                config.sampleRate = atoi(optarg);
                break;
            case 'c': // channel count
                config.channelCount = atoi(optarg);
                break;
            case 'f': // format (map friendly numbers to audio_format_t)
                config.format = AudioUtils::parseFormatOption(atoi(optarg));
                break;
            case 'F': // input flag
                config.inputFlag = static_cast<audio_input_flags_t>(atoi(optarg));
                break;
            case 'd': // recording duration in seconds
                config.durationSeconds = atoi(optarg);
                break;
            case 'u': // audio usage
                config.usage = static_cast<audio_usage_t>(atoi(optarg));
                break;
            case 'C': // audio content type
                config.contentType = static_cast<audio_content_type_t>(atoi(optarg));
                break;
            case 'O': // output flag
                config.outputFlag = static_cast<audio_output_flags_t>(atoi(optarg));
                break;
            case 'z': // min frame count
                config.minFrameCount = atoi(optarg);
                break;
            case 'x': // first parameter for set params mode (1=open_source, 2=close_source)
                if (mode == MODE_SET_PARAMS) {
                    xParam = atoi(optarg);
                } else {
                    printf("Warning: -x option only valid in set params mode (-m 100)\n");
                }
                break;
            case 'y': // second parameter for set params mode (audio usage value)
                if (mode == MODE_SET_PARAMS) {
                    yParam = atoi(optarg);
                } else {
                    printf("Warning: -y option only valid in set params mode (-m 100)\n");
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

        // Combine x and y parameters into setParams vector
        if (mode == MODE_SET_PARAMS && xParam >= 1) {
            config.setParams.push_back(xParam);
            if (yParam >= 1) {
                config.setParams.push_back(yParam);
            }
        }

        // Get audio file path from remaining argument
        if (optind < argc) {
            if (mode == MODE_PLAY) {
                config.playFilePath = argv[optind];
            } else if ((mode == MODE_RECORD) || (mode == MODE_DUPLEX)) {
                config.recordFilePath = argv[optind];
            }
        }
    }

    static void showHelp() {
        const char* helpText = R"(
Audio Test Client - Combined Record and Play Demo
Usage: audio_test_client -m{mode} [options] [audio_file]

Modes:
  -m0   Record mode
  -m1   Play mode
  -m2   Duplex mode (record and play simultaneously)
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
  -F{inputFlag}       Set audio input flag
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
  -z{minFrameCount}   Set min frame count (default: system selected)
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
                       1004: AUDIO_USAGE_SPEAKER_CLEANUP (Speaker cleanup)
  -C{contentType}     Set content type
                       0: AUDIO_CONTENT_TYPE_UNKNOWN (Unknown content type)
                       1: AUDIO_CONTENT_TYPE_SPEECH (Speech)
                       2: AUDIO_CONTENT_TYPE_MUSIC (Music)
                       3: AUDIO_CONTENT_TYPE_MOVIE (Movie)
                       4: AUDIO_CONTENT_TYPE_SONIFICATION (Sonification)
                       1997: AUDIO_CONTENT_TYPE_ULTRASOUND (Ultrasound)
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
  -z{minFrameCount}   Set min frame count (default: system selected)

Set Params Options:
  -x{param1}          First parameter
                       1: open_source
                       2: close_source
  -y{param2}          Second parameter (audio usage)
                       1: AUDIO_USAGE_MEDIA
                       2: AUDIO_USAGE_VOICE_COMMUNICATION
                       ... (see usage)

For more details, please refer to system/media/audio/include/system/audio-hal-enums.h

General Options:
  -h                  Show this help message

Examples:
  Record: audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z960 -d20
  Play:   audio_test_client -m1 -u1 -C0 -O4 -z960 /data/audio_test.wav
  Duplex: audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u1 -C0 -O4 -z960 -d20
  SetParams: audio_test_client -m100 -x1 -y1
)";
        puts(helpText);
    }
};

/************************** Main function ******************************/
int32_t main(int32_t argc, char** argv) {
    AudioMode mode = MODE_INVALID;
    AudioConfig config;

    printf("Audio Test Client %s Start...\n", AUDIO_TEST_CLIENT_VERSION);
    // Parse command line arguments
    CommandLineParser::parseArguments(argc, argv, mode, config);

    // Create the appropriate audio operation using factory
    std::unique_ptr<AudioOperation> operation = AudioOperationFactory::createOperation(mode, config);

    // Check if operation was created successfully
    if (!operation) {
        CommandLineParser::showHelp();
        return -1;
    }

    // Execute the audio operation
    return operation->execute();
}
