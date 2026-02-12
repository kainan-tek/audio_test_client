#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstring>
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
#include <type_traits>
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

#define AUDIO_TEST_CLIENT_VERSION "2.2.0"
#define ENABLE_SET_PARAMS 0

using namespace android;
using android::content::AttributionSourceState;

/************************** WAV File Management ******************************/
class WAVFile {
public:
    WAVFile() = default;
    ~WAVFile() { close(); }

    // Disable copy operations to prevent issues with file resources
    WAVFile(const WAVFile&) = delete;
    WAVFile& operator=(const WAVFile&) = delete;

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

        // Read WAV header from input stream
        void read(std::istream& in) {
            // Basic RIFF/WAVE header
            in.read(riffID, 4);
            in.read(reinterpret_cast<char*>(&riffSize), 4);
            in.read(waveID, 4);

            // Read first chunk (expecting 'fmt ')
            in.read(fmtID, 4);
            in.read(reinterpret_cast<char*>(&fmtSize), 4);

            // Read standard fmt fields (16 bytes)
            in.read(reinterpret_cast<char*>(&audioFormat), 2);
            in.read(reinterpret_cast<char*>(&numChannels), 2);
            in.read(reinterpret_cast<char*>(&sampleRate), 4);
            in.read(reinterpret_cast<char*>(&byteRate), 4);
            in.read(reinterpret_cast<char*>(&blockAlign), 2);
            in.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            in.read(dataID, 4);
            in.read(reinterpret_cast<char*>(&dataSize), 4);
        }

        // Print WAV header information to console
        void print() const {
            printf("RiffID: %.4s\n", riffID);
            printf("RiffSize: %" PRIu32 "\n", riffSize);
            printf("WaveID: %.4s\n", waveID);
            printf("FmtID: %.4s\n", fmtID);
            printf("FmtSize: %" PRIu32 "\n", fmtSize);
            printf("AudioFormat: %u\n", (unsigned)audioFormat);
            printf("NumChannels: %u\n", (unsigned)numChannels);
            printf("SampleRate: %" PRIu32 "\n", sampleRate);
            printf("ByteRate: %" PRIu32 "\n", byteRate);
            printf("BlockAlign: %u\n", (unsigned)blockAlign);
            printf("BitsPerSample: %u\n", (unsigned)bitsPerSample);
            printf("DataID: %.4s\n", dataID);
            printf("DataSize: %" PRIu32 "\n", dataSize);
        }
    };

    // Create WAV file for writing with specified audio parameters
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
        if (!fileStream_.good()) {
            fileStream_.close(); // Close file if header write failed
            return false;
        }

        isHeaderValid_ = true;
        dataSizePos_ = fileStream_.tellp() - std::streamoff(4); // Position of dataSize field

        return fileStream_.good();
    }

    // Open WAV file for reading
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
            fileStream_.close(); // Close file before returning
            return false;
        }
        if (header_.fmtSize < 16 || (header_.audioFormat != 1 && header_.audioFormat != 3) ||
            header_.numChannels == 0 || header_.sampleRate == 0) {
            fileStream_.close(); // Close file before returning
            return false;
        }

        isHeaderValid_ = true;
        return fileStream_.good();
    }

    // Write audio data to WAV file
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

    // Update WAV header with final file sizes
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

    size_t readData(char* data, const size_t size) {
        if (!fileStream_.is_open() || !isHeaderValid_) {
            return 0;
        }

        fileStream_.read(data, size);
        return static_cast<size_t>(fileStream_.gcount());
    }

    // Finalize WAV file by updating header and closing file
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
        // WAV fmt audioFormat: 1 = PCM (integer), 3 = IEEE float
        if (header_.audioFormat == 1) {
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
        } else if (header_.audioFormat == 3) {
            // IEEE float WAV -- typically 32-bit float samples
            if (header_.bitsPerSample == 32) {
                return AUDIO_FORMAT_PCM_FLOAT;
            }
            return AUDIO_FORMAT_INVALID;
        }
        return AUDIO_FORMAT_INVALID;
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
    // Constructor with buffer size
    explicit BufferManager(size_t bufferSize) { initializeBuffer(bufferSize); }
    ~BufferManager() = default;

    // Disable copy operations to prevent issues with buffer resources
    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;

    // Get pointer to buffer memory
    char* get() { return buffer.get(); }
    // Get buffer size in bytes
    size_t getSize() const { return size; }
    // Check if buffer is properly allocated
    bool isValid() const { return buffer != nullptr && size > 0; }

private:
    // Initialize buffer with size validation and error handling
    void initializeBuffer(size_t requestedSize) {
        const size_t MIN_BUFFER_SIZE = 480;              // Minimum reasonable buffer size
        const size_t MAX_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB max
        const size_t validatedSize = std::clamp(requestedSize, MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);

        try {
            buffer = std::make_unique<char[]>(validatedSize);
            size = validatedSize;
            printf("BufferManager created with buffer size: %zu\n", size);
        } catch (const std::bad_alloc& e) {
            buffer.reset();
            size = 0;
            printf("Error: Failed to allocate buffer of size %zu: %s\n", validatedSize, e.what());
        }
    }

    std::unique_ptr<char[]> buffer;
    size_t size{0};
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
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
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

    // Generate WAV file path with timestamp or use provided override path
    static std::string makeRecordFilePath(const int32_t sampleRate,
                                          const int32_t channelCount,
                                          const uint32_t bitsPerSample,
                                          const std::string& overridePath) {
        if (!overridePath.empty()) {
            return overridePath;
        }
        // Use snprintf for efficient string formatting
        const std::string formatTime = AudioUtils::getFormatTime();
        char buffer[256];
        int bytesWritten = snprintf(buffer, sizeof(buffer), "/data/record_%dHz_%dch_%dbit_%s.wav", sampleRate,
                                    channelCount, bitsPerSample, formatTime.c_str());

        // Ensure path length does not exceed filesystem limits
        if (bytesWritten >= 240 || bytesWritten < 0) {
            // Path too long, use simplified format
            snprintf(buffer, sizeof(buffer), "/data/audio_%s.wav", formatTime.c_str());
            printf("Warning: File path too long, using shortened name\n");
        }

        return std::string(buffer);
    }
};

/************************** Global Variables ******************************/
// Global exit flag for signal handling
static std::atomic<bool> sExitRequested(false);

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
    audio_output_flags_t outputFlag = AUDIO_OUTPUT_FLAG_NONE;
    std::string playFilePath = "/data/audio_test.wav";

    // Set params parameters
    std::vector<int32_t> setParams{};
};

/************************** AudioMode Definitions ******************************/
enum AudioMode { MODE_INVALID = -1, MODE_RECORD = 0, MODE_PLAY = 1, MODE_LOOPBACK = 2, MODE_SET_PARAMS = 100 };

/************************** Audio Parameter Manager ******************************/
static const String8 PARAM_OPEN_SOURCE = String8("open_source");   // Open source parameter name
static const String8 PARAM_CLOSE_SOURCE = String8("close_source"); // Close source parameter name
static const String8 PARAM_CHANNEL_MASK = String8("channel_mask"); // Channel mask parameter name

class AudioParameterManager {
public:
    explicit AudioParameterManager(const AudioConfig& config) : mConfig(config) {}
    ~AudioParameterManager() = default;

    // Disable copy operations to prevent issues with configuration references
    AudioParameterManager(const AudioParameterManager&) = delete;
    AudioParameterManager& operator=(const AudioParameterManager&) = delete;

    // Set open source parameter with specific audio usage
    void setOpenSourceWithUsage(audio_usage_t usage) {
        setSystemParameter(PARAM_OPEN_SOURCE, audioUsageToString(usage));
    }

    // Set close source parameter with specific audio usage
    void setCloseSourceWithUsage(audio_usage_t usage) {
        setSystemParameter(PARAM_CLOSE_SOURCE, audioUsageToString(usage));
    }

    // Set channel mask parameter for AudioTrack
    void setChannelMask(const sp<AudioTrack>& audioTrack, audio_channel_mask_t channelMask) {
        setAudioTrackParameter(audioTrack, PARAM_CHANNEL_MASK, String8::format("%d", channelMask));
    }

private:
    AudioConfig mConfig;

    // Unified interface for AudioSystem::setParameters
    void setSystemParameter(const String8& key, const String8& value) {
#if ENABLE_SET_PARAMS
        AudioParameter audioParam;
        audioParam.add(key, value);
        String8 paramString = audioParam.toString();
        AudioSystem::setParameters(paramString);
        printf("Set parameter: %s\n", paramString.c_str());
#endif
    }

    // Unified interface for audioTrack->setParameters
    void setAudioTrackParameter(const sp<AudioTrack>& audioTrack, const String8& key, const String8& value) {
#if ENABLE_SET_PARAMS
        AudioParameter audioParam;
        audioParam.add(key, value);
        String8 paramString = audioParam.toString();
        audioTrack->setParameters(paramString);
        printf("Set parameter: %s\n", paramString.c_str());
#endif
    }

    // Convert audio_usage_t enum to string representation
    String8 audioUsageToString(audio_usage_t usage) {
        static const std::unordered_map<audio_usage_t, const char*> usageMap = {
            // Basic usage types (Android 16 AUDIO_USAGE_LIST_NO_SYS_DEF)
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

            // System usage types (available when not AUDIO_NO_SYSTEM_DECLARATIONS)
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST"},
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT"},
            {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED"},

            // High-value usage types (Android Automotive and special scenarios)
            {AUDIO_USAGE_EMERGENCY, "AUDIO_USAGE_EMERGENCY"},
            {AUDIO_USAGE_SAFETY, "AUDIO_USAGE_SAFETY"},
            {AUDIO_USAGE_VEHICLE_STATUS, "AUDIO_USAGE_VEHICLE_STATUS"},
            {AUDIO_USAGE_ANNOUNCEMENT, "AUDIO_USAGE_ANNOUNCEMENT"},
            // {AUDIO_USAGE_SPEAKER_CLEANUP, "AUDIO_USAGE_SPEAKER_CLEANUP"},
        };
        const char* usageName = "AUDIO_USAGE_UNKNOWN";
        const auto it = usageMap.find(usage);
        if (it != usageMap.end()) {
            usageName = it->second;
        }
        return String8::format("0:%s", usageName);
    }
};

/************************** Audio Operation Base Class ******************************/
class AudioOperation {
public:
    explicit AudioOperation(const AudioConfig& config) : mConfig(config), mAudioParamManager(config) {
        setupSignalHandler();
    }
    virtual ~AudioOperation() = default;
    // Disable copy operations to prevent issues with configuration and audio parameters
    AudioOperation(const AudioOperation&) = delete;
    AudioOperation& operator=(const AudioOperation&) = delete;

    virtual int32_t execute() = 0;

protected:
    static constexpr uint32_t kMaxAudioDataSize = 2u * 1024u * 1024u * 1024u; // 2 GiB
    static constexpr uint32_t kProgressReportInterval = 10;                   // report progress every 10 seconds
    static constexpr uint32_t kLevelMeterInterval = 25;                       // Update level meter every 30 frames

    AudioConfig mConfig;
    AudioParameterManager mAudioParamManager;
    uint32_t mLevelMeterCounter = 0;  // For level meter updates
    uint64_t mNextProgressReport = 0; // For progress reporting

    // Calculate required buffer size based on audio configuration
    size_t calculateBufferSize() const {
        const size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        return (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
    }

    // Calculate frame count with minimum buffer considerations
    size_t calculateFrameCount() const {
        const size_t minFrames = static_cast<size_t>((mConfig.sampleRate * 10) / 1000);
        const size_t adjustedMinFrameCount = std::max(mConfig.minFrameCount, minFrames);
        return adjustedMinFrameCount * 2;
    }

    // Calculate bytes per second based on audio configuration
    uint64_t calculateBytesPerSecond() const {
        const size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        return static_cast<uint64_t>(mConfig.sampleRate) * mConfig.channelCount * bytesPerSample;
    }

    // Validate audio parameters for correctness
    bool validateAudioParameters() const {
        if (mConfig.sampleRate <= 0 || mConfig.channelCount <= 0) {
            printf("Error: Invalid sample rate or channel count\n");
            return false;
        }
        if (mConfig.format == AUDIO_FORMAT_INVALID) {
            printf("Error: Invalid audio format\n");
            return false;
        }
        return true;
    }

    // Create attribution source for audio operations
    AttributionSourceState createAttributionSource() {
        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();
        attributionSource.uid = getuid();
        attributionSource.pid = getpid();
        return attributionSource;
    }

    // Initialize AudioRecord with audio configuration
    bool initializeAudioRecord(sp<AudioRecord>& audioRecord) {
        audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(mConfig.channelCount);
        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMask) !=
            NO_ERROR) {
            printf("Warning: Cannot get min frame count, using default value\n");
        }
        const size_t frameCount = calculateFrameCount();

        printf("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
               "frameCount=%zu\n",
               mConfig.inputSource, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);
        ALOGI("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
              "frameCount=%zu",
              mConfig.inputSource, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);

        AttributionSourceState attributionSource = createAttributionSource();
        audio_attributes_t attributes{};
        attributes.source = mConfig.inputSource;
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
            printf("Error: Failed to initialize AudioRecord parameters\n");
            ALOGE("Failed to initialize AudioRecord parameters");
            return false;
        }

        if (audioRecord->initCheck() != NO_ERROR) {
            printf("Error: AudioRecord initialization check failed\n");
            ALOGE("AudioRecord initialization check failed");
            return false;
        }

        printf("AudioRecord initialized successfully\n");
        return true;
    }

    // Initialize AudioTrack with audio configuration
    bool initializeAudioTrack(sp<AudioTrack>& audioTrack) {
        audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(mConfig.channelCount);

        // Get minimum frame count using AudioTrack static method with streamType
        // Since we use audio_attributes_t, we need to convert usage to streamType
        audio_stream_type_t streamType = AudioUtils::usageToStreamType(mConfig.usage);
        if (AudioTrack::getMinFrameCount(&mConfig.minFrameCount, streamType, mConfig.sampleRate) != NO_ERROR) {
            printf("Warning: Cannot get min frame count using streamType, using default value\n");
        }
        const size_t frameCount = calculateFrameCount();

        printf("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
               "frameCount=%zu\n",
               mConfig.usage, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);
        ALOGI("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
              "frameCount=%zu",
              mConfig.usage, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);

        AttributionSourceState attributionSource = createAttributionSource();
        audio_attributes_t attributes{};
        attributes.usage = mConfig.usage;
        attributes.content_type = AudioUtils::usageToContentType(mConfig.usage);
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
            printf("Error: Failed to initialize AudioTrack parameters\n");
            ALOGE("Failed to initialize AudioTrack parameters");
            return false;
        }

        if (audioTrack->initCheck() != NO_ERROR) {
            printf("Error: AudioTrack initialization check failed\n");
            ALOGE("AudioTrack initialization check failed");
            return false;
        }

        printf("AudioTrack initialized successfully\n");
        return true;
    }

    // Start audio component (AudioRecord or AudioTrack) with parameter setup
    template <typename T> bool startAudioComponent(const sp<T>& component) {
        // set params before AudioTrack.start()
        if constexpr (std::is_same_v<T, AudioTrack>) {
            mAudioParamManager.setOpenSourceWithUsage(mConfig.usage);
        }

        printf("Starting audio component\n");
        ALOGI("Starting audio component");
        status_t startResult = component->start();
        if (startResult != NO_ERROR) {
            const char* componentName = std::is_same_v<T, AudioRecord> ? "AudioRecord" : "AudioTrack";
            printf("Error: %s start failed with status %d\n", componentName, startResult);
            ALOGE("%s start failed with status %d", componentName, startResult);
            return false;
        }
        return true;
    }

    // Stop audio component and clean up parameters
    template <typename T> void stopAudioComponent(const sp<T>& audioComponent) {
        if (audioComponent != nullptr) {
            printf("Stopping audio component\n");
            ALOGI("Stopping audio component");
            audioComponent->stop();
            if constexpr (std::is_same_v<T, AudioTrack>) {
                mAudioParamManager.setCloseSourceWithUsage(mConfig.usage);
            }
        }
    }

    // Setup WAV file for audio recording with configuration
    bool setupWavFileForRecording(WAVFile& wavFile) {
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);

        mConfig.recordFilePath = AudioUtils::makeRecordFilePath(mConfig.sampleRate, mConfig.channelCount,
                                                                bytesPerSample * 8, mConfig.recordFilePath);

        printf("Recording audio to file: %s\n", mConfig.recordFilePath.c_str());
        if (!wavFile.createForWriting(mConfig.recordFilePath, mConfig.sampleRate, mConfig.channelCount,
                                      bytesPerSample * 8)) {
            printf("Error: Can't create record file: %s\n", mConfig.recordFilePath.c_str());
            return false;
        }

        return true;
    }

    // Setup WAV file for audio playback and extract audio parameters
    bool setupWavFileForPlayback(WAVFile& wavFile) {
        if (mConfig.playFilePath.empty() || access(mConfig.playFilePath.c_str(), F_OK) == -1) {
            printf("Error: File does not exist: %s\n", mConfig.playFilePath.c_str());
            return false;
        }

        // Open WAV file for reading
        if (!wavFile.openForReading(mConfig.playFilePath)) {
            printf("Error: Failed to open WAV file: %s\n", mConfig.playFilePath.c_str());
            return false;
        }

        mConfig.sampleRate = wavFile.getSampleRate();
        mConfig.channelCount = wavFile.getNumChannels();
        mConfig.format = wavFile.getAudioFormat();
        printf("audio file info: %s, sampleRate: %d, channelCount: %d, format: %d\n", mConfig.playFilePath.c_str(),
               mConfig.sampleRate, mConfig.channelCount, mConfig.format);

        return true;
    }

    // Report progress during audio recording or playback
    template <typename T>
    bool reportProgress(const sp<T>& component,
                        const uint64_t totalBytesProcessed,
                        const uint64_t bytesPerSecond,
                        WAVFile* wavFile = nullptr) {
        if (component == nullptr) {
            return false;
        }

        if (totalBytesProcessed >= mNextProgressReport) {
            const char* operationTypeName = std::is_same_v<T, AudioRecord> ? "Recording" : "Playing";
            printf("%s ... , processed %.2f seconds, %.2f MB\n", operationTypeName,
                   static_cast<float>(totalBytesProcessed) / bytesPerSecond,
                   static_cast<float>(totalBytesProcessed) / (1024u * 1024u));
            mNextProgressReport += bytesPerSecond * kProgressReportInterval;

            if constexpr (std::is_same_v<T, AudioRecord>) {
                if (wavFile) {
                    wavFile->updateHeader();
                }
            }
            return true;
        }
        return false;
    }

    // Handle SIGINT signal (Ctrl+C) for graceful shutdown
    static void signalHandler(int signal) {
        if (signal == SIGINT) {
            sExitRequested.store(true);
        }
    }

    // Setup SIGINT signal handler for graceful termination
    void setupSignalHandler() { signal(SIGINT, signalHandler); }

    // Update audio level meter with low CPU usage
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
        const std::string timestamp = AudioUtils::getTimestamp();
        printf("[%s] Audio Level: %.1f dB, bytes: %zu\n", timestamp.c_str(), dbLevel, size);
    }
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation : public AudioOperation {
public:
    // Constructor for audio recording operation
    explicit AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioRecordOperation() override = default;

    // Disable copy operations (inherited from AudioOperation)
    AudioRecordOperation(const AudioRecordOperation&) = delete;
    AudioRecordOperation& operator=(const AudioRecordOperation&) = delete;

    // Execute audio recording operation
    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioRecord> audioRecord;

        if (!setupWavFileForRecording(wavFile) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioRecord(audioRecord)) {
            wavFile.close();
            return -1;
        }

        if (!startAudioComponent(audioRecord)) {
            wavFile.close();
            return -1;
        }

        // Main recording loop
        int32_t operationResult = recordLoop(audioRecord, wavFile);

        // Cleanup
        stopAudioComponent(audioRecord);
        wavFile.finalize();

        return operationResult;
    }

private:
    // Main recording loop that handles audio data collection
    int32_t recordLoop(const sp<AudioRecord>& audioRecord, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audioBuffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            printf("Recording for %d seconds...\n", mConfig.durationSeconds);
        }

        printf("Recording in progress. Press Ctrl+C to stop\n");
        ALOGI("Recording in progress.");
        const uint64_t bytesPerSecond = calculateBytesPerSecond();
        const uint64_t maxBytesToRecord =
            (mConfig.durationSeconds > 0) ? std::min(static_cast<uint64_t>(mConfig.durationSeconds) * bytesPerSecond,
                                                     static_cast<uint64_t>(kMaxAudioDataSize))
                                          : static_cast<uint64_t>(kMaxAudioDataSize);
        mNextProgressReport = bytesPerSecond * kProgressReportInterval;

        uint64_t totalBytesRead = 0;
        while (totalBytesRead < maxBytesToRecord && !sExitRequested) {
            const ssize_t bytesRead = audioRecord->read(audioBuffer, calculateBufferSize());
            if (bytesRead < 0) {
                printf("Error: AudioRecord read failed: %zd\n", bytesRead);
                ALOGE("AudioRecord read failed: %zd", bytesRead);
                break;
            }
            if (bytesRead == 0) {
                continue;
            }
            totalBytesRead += static_cast<uint64_t>(bytesRead);

            // Update level meter
            updateLevelMeter(audioBuffer, static_cast<size_t>(bytesRead));

            // Write data to WAV file
            if (wavFile.writeData(audioBuffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
                printf("Error: Failed to save audio data to file\n");
                ALOGE("Failed to save audio data to file");
                break;
            }

            // Report progress
            reportProgress(audioRecord, totalBytesRead, calculateBytesPerSecond(), &wavFile);
        }

        printf("Recording finished: Recorded %" PRIu64 " bytes, File saved: %s\n", totalBytesRead,
               wavFile.getFilePath().c_str());

        return 0;
    }
};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation : public AudioOperation {
public:
    // Constructor for audio playback operation
    explicit AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioPlayOperation() override = default;

    // Disable copy operations (inherited from AudioOperation)
    AudioPlayOperation(const AudioPlayOperation&) = delete;
    AudioPlayOperation& operator=(const AudioPlayOperation&) = delete;

    // Execute audio playback operation
    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioTrack> audioTrack;

        if (!setupWavFileForPlayback(wavFile) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioTrack(audioTrack)) {
            wavFile.close();
            return -1;
        }

        // Start playback
        if (!startAudioComponent(audioTrack)) {
            wavFile.close();
            return -1;
        }

        // Main playback loop
        int32_t operationResult = playLoop(audioTrack, wavFile);

        // Cleanup
        stopAudioComponent(audioTrack);
        wavFile.close();

        return operationResult;
    }

private:
    // Main playback loop that handles audio data playback
    int32_t playLoop(const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audioBuffer = bufferManager.get();

        printf("Playing in progress. Press Ctrl+C to stop\n");
        ALOGI("Playing in progress.");
        const uint64_t bytesPerSecond = calculateBytesPerSecond();
        mNextProgressReport = bytesPerSecond * kProgressReportInterval;
        uint64_t totalBytesPlayed = 0;
        while (!sExitRequested) {
            const size_t bytesRead = wavFile.readData(audioBuffer, calculateBufferSize());
            if (bytesRead == 0) {
                printf("End of file reached\n");
                break;
            }

            size_t bytesWritten = 0;
            const size_t bytesToWrite = bytesRead;
            while (bytesWritten < bytesToWrite && !sExitRequested) {
                const ssize_t written = audioTrack->write(audioBuffer + bytesWritten, bytesToWrite - bytesWritten);
                if (written < 0) {
                    printf("Error: AudioTrack write failed: %zd\n", written);
                    ALOGE("AudioTrack write failed: %zd", written);
                    return -1;
                }
                bytesWritten += static_cast<size_t>(written);
            }
            // Update total bytes played
            totalBytesPlayed += static_cast<uint64_t>(bytesWritten);

            // Update level meter
            updateLevelMeter(audioBuffer, bytesRead);

            // Report progress
            reportProgress(audioTrack, totalBytesPlayed, calculateBytesPerSecond());
        }
        printf("Playback finished: Total bytes played: %" PRIu64 "\n", totalBytesPlayed);

        return 0;
    }
};

/************************** Audio Loopback Operation ******************************/
class AudioLoopbackOperation : public AudioOperation {
public:
    // Constructor for audio loopback operation (record + playback)
    explicit AudioLoopbackOperation(const AudioConfig& config) : AudioOperation(config) {}
    ~AudioLoopbackOperation() override = default;

    // Disable copy operations (inherited from AudioOperation)
    AudioLoopbackOperation(const AudioLoopbackOperation&) = delete;
    AudioLoopbackOperation& operator=(const AudioLoopbackOperation&) = delete;

    // Execute audio loopback operation (simultaneous recording and playback)
    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioRecord> audioRecord;
        sp<AudioTrack> audioTrack;

        if (!setupWavFileForRecording(wavFile) || !validateAudioParameters()) {
            printf("Error: Failed to setup WAV file or validate audio parameters\n");
            return -1;
        }

        if (!initializeAudioRecord(audioRecord)) {
            wavFile.close();
            return -1;
        }

        if (!initializeAudioTrack(audioTrack)) {
            wavFile.close();
            return -1;
        }

        // Start recording and playback
        if (!startAudioComponent(audioRecord)) {
            wavFile.close();
            return -1;
        }
        if (!startAudioComponent(audioTrack)) {
            stopAudioComponent(audioRecord);
            wavFile.close();
            return -1;
        }

        // Main loopback loop
        int32_t operationResult = loopbackLoop(audioRecord, audioTrack, wavFile);

        // Cleanup
        stopAudioComponent(audioRecord);
        stopAudioComponent(audioTrack);
        wavFile.finalize();

        return operationResult;
    }

private:
    // Main loopback loop for simultaneous recording and playback
    int32_t loopbackLoop(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        // Setup buffer
        BufferManager bufferManager(calculateBufferSize());
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            return -1;
        }
        char* const audioBuffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            printf("Duplex audio started: Recording for %d seconds...\n", mConfig.durationSeconds);
        }

        printf("Duplex audio in progress. Press Ctrl+C to stop\n");
        ALOGI("Duplex audio in progress.");
        const uint64_t bytesPerSecond = calculateBytesPerSecond();
        const uint64_t maxBytesToRecord =
            (mConfig.durationSeconds > 0) ? std::min(static_cast<uint64_t>(mConfig.durationSeconds) * bytesPerSecond,
                                                     static_cast<uint64_t>(kMaxAudioDataSize))
                                          : static_cast<uint64_t>(kMaxAudioDataSize);
        mNextProgressReport = bytesPerSecond * kProgressReportInterval;

        uint64_t totalBytesRead = 0;
        uint64_t totalBytesPlayed = 0;
        bool duplexError = false; // Track if any error occurred during duplex operation
        while (totalBytesRead < maxBytesToRecord && !sExitRequested && !duplexError) {
            const ssize_t bytesRead = audioRecord->read(audioBuffer, calculateBufferSize());
            if (bytesRead < 0) {
                printf("Error: AudioRecord read failed: %zd\n", bytesRead);
                ALOGE("AudioRecord read failed: %zd", bytesRead);
                break;
            }
            if (bytesRead == 0) {
                continue;
            }
            totalBytesRead += static_cast<uint64_t>(bytesRead);

            // Update level meter for recording
            updateLevelMeter(audioBuffer, static_cast<size_t>(bytesRead));

            // Write to WAV file
            if (wavFile.writeData(audioBuffer, static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
                printf("Error: Failed to save audio data to file\n");
                // break; // Continue playing if save failed
            }

            // Report progress for recording
            reportProgress(audioRecord, totalBytesRead, calculateBytesPerSecond(), &wavFile);

            // Check recording finish
            if (totalBytesRead >= maxBytesToRecord) {
                break;
            }

            size_t bytesWritten = 0;
            const size_t bytesToWrite = static_cast<size_t>(bytesRead);
            while (bytesWritten < bytesToWrite && !sExitRequested) {
                const ssize_t written = audioTrack->write(audioBuffer + bytesWritten, bytesToWrite - bytesWritten);
                if (written < 0) {
                    printf("Error: AudioTrack write failed: %zd\n", written);
                    ALOGE("AudioTrack write failed: %zd", written);
                    duplexError = true;
                    break;
                }
                bytesWritten += static_cast<size_t>(written);
            }
            totalBytesPlayed += static_cast<uint64_t>(bytesWritten);
        }

        printf("Loopback audio completed: Total bytes read: %" PRIu64 ", Total bytes played: %" PRIu64
               ", File saved: %s\n",
               totalBytesRead, totalBytesPlayed, wavFile.getFilePath().c_str());

        return 0;
    }
};

/************************** Set Parameters Operation ******************************/
class SetParamsOperation : public AudioOperation {
public:
    // Constructor for parameter setting operation
    explicit SetParamsOperation(const AudioConfig& config, const std::vector<int32_t>& params)
        : AudioOperation(config), mTargetParameters(params) {}
    ~SetParamsOperation() override = default;

    // Disable copy operations (inherited from AudioOperation)
    SetParamsOperation(const SetParamsOperation&) = delete;
    SetParamsOperation& operator=(const SetParamsOperation&) = delete;

    // Execute parameter setting operation
    int32_t execute() override {
        if (mTargetParameters.empty()) {
            printf("Error: No parameters provided\n");
            return -1;
        }

        printf("SetParams operation started with %zu parameters\n", mTargetParameters.size());
        for (size_t i = 0; i < mTargetParameters.size(); ++i) {
            printf("  Parameter %zu: %d\n", i + 1, mTargetParameters[i]);
        }

        int32_t sourceType = mTargetParameters[0];
        switch (sourceType) {
        case 1: // open_source
            if (mTargetParameters.size() >= 2) {
                int32_t usageValue = mTargetParameters[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usageValue);
                printf("Setting open_source with usage: %d\n", usage);
                mAudioParamManager.setOpenSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for open_source\n");
            }
            break;
        case 2: // close_source
            if (mTargetParameters.size() >= 2) {
                int32_t usageValue = mTargetParameters[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usageValue);
                printf("Setting close_source with usage: %d\n", usage);
                mAudioParamManager.setCloseSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for close_source\n");
            }
            break;
        default:
            printf("Error: Unknown primary parameter %d (1=open_source, 2=close_source)\n", sourceType);
            return -1;
        }

        printf("SetParams operation completed\n");
        return 0;
    }

private:
    std::vector<int32_t> mTargetParameters;
};

/************************** Audio Operation Factory ******************************/
class AudioOperationFactory {
private:
    // Private constructor to prevent instantiation - this is a factory class
    AudioOperationFactory() = delete;

public:
    // Factory method to create appropriate audio operation based on mode
    static std::unique_ptr<AudioOperation> createOperation(AudioMode mode, const AudioConfig& config) {
        switch (mode) {
        case MODE_RECORD:
            return std::make_unique<AudioRecordOperation>(config);
        case MODE_PLAY:
            return std::make_unique<AudioPlayOperation>(config);
        case MODE_LOOPBACK:
            return std::make_unique<AudioLoopbackOperation>(config);
        case MODE_SET_PARAMS:
            return std::make_unique<SetParamsOperation>(config, config.setParams);
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
            case 'I': // input flag
                config.inputFlag = static_cast<audio_input_flags_t>(atoi(optarg));
                break;
            case 'd': // recording duration in seconds
                config.durationSeconds = atoi(optarg);
                break;
            case 'u': // audio usage
                config.usage = static_cast<audio_usage_t>(atoi(optarg));
                break;
            case 'O': // output flag
                config.outputFlag = static_cast<audio_output_flags_t>(atoi(optarg));
                break;
            case 'F': // min frame count
                config.minFrameCount = atoi(optarg);
                break;
            case 'P': // audio file path (input for play, output for record/loopback)
                if (mode == MODE_PLAY) {
                    config.playFilePath = optarg;
                } else if ((mode == MODE_RECORD) || (mode == MODE_LOOPBACK)) {
                    config.recordFilePath = optarg;
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

        // Parse setParams from remaining command line arguments if mode is MODE_SET_PARAMS
        if (mode == MODE_SET_PARAMS) {
            for (int32_t i = optind; i < argc; ++i) {
                std::string argStr(argv[i]);
                size_t start = 0;
                size_t end = argStr.find(',');
                while (end != std::string::npos) {
                    if (end > start) {
                        config.setParams.push_back(std::stoi(argStr.substr(start, end - start)));
                    }
                    start = end + 1;
                    end = argStr.find(',', start);
                }
                // Handle the last token
                if (start < argStr.length()) {
                    config.setParams.push_back(std::stoi(argStr.substr(start)));
                }
            }
        } else {
            // Get audio file path from remaining argument for other modes
            if (optind < argc) {
                if (mode == MODE_PLAY) {
                    config.playFilePath = argv[optind];
                } else if ((mode == MODE_RECORD) || (mode == MODE_LOOPBACK)) {
                    config.recordFilePath = argv[optind];
                }
            }
        }
    }

    // Display comprehensive help information for command line usage
    static void showHelp() {
        const char* helpText = R"(
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
        puts(helpText);
    }
};

/************************** Main function ******************************/
// Main entry point for audio test client application
int32_t main(int32_t argc, char** argv) {
    AudioMode mode = MODE_INVALID;
    AudioConfig config;

    printf("Audio Test Client %s Start...\n", AUDIO_TEST_CLIENT_VERSION);
    // Parse command line arguments
    CommandLineParser::parseArguments(argc, argv, mode, config);

    // Create the appropriate audio operation using factory
    std::unique_ptr<AudioOperation> operation = AudioOperationFactory::createOperation(mode, config);
    if (!operation) {
        CommandLineParser::showHelp();
        return -1;
    }

    // Execute the audio operation
    return operation->execute();
}
