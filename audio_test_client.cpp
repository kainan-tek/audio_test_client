#include <atomic>
#include <binder/Binder.h>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utils/Log.h>

#define LOG_TAG "audio_test_client"

using namespace android;
using android::content::AttributionSourceState;

/* max data size for reading */
static constexpr uint32_t MAX_AUDIO_DATA_SIZE = 2u * 1024u * 1024u * 1024u; // 2 GiB

/************************** WAV File Management ******************************/
class WAVFile {
public:
    WAVFile() : isHeaderValid_(false) {
        memset(&header_, 0, sizeof(Header));
        static_assert(sizeof(Header) == 44, "WAV header size must be 44 bytes");
    }
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

    bool
    createForWriting(const std::string& filePath, uint32_t sampleRate, uint32_t numChannels, uint32_t bitsPerSample) {
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

    size_t writeData(const char* data, size_t size) {
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
            // Save current position
            std::streampos currentPos = fileStream_.tellp();

            // Update RIFF chunk size
            fileStream_.seekp(4, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char*>(&header_.riffSize), sizeof(header_.riffSize));

            // Update data chunk size
            fileStream_.seekp(dataSizePos_, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char*>(&header_.dataSize), sizeof(header_.dataSize));

            // Flush to ensure data is written to disk
            fileStream_.flush();

            // Return to previous position
            fileStream_.seekp(currentPos);
        }
    }

    size_t readData(char* data, size_t size) {
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
        if ((header_.audioFormat == 1) && (header_.bitsPerSample == 8)) {
            return AUDIO_FORMAT_PCM_8_BIT;
        } else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 16)) {
            return AUDIO_FORMAT_PCM_16_BIT;
        } else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 24)) {
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        } else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 32)) {
            return AUDIO_FORMAT_PCM_32_BIT;
        } else if ((header_.audioFormat == 3) && (header_.bitsPerSample == 32)) {
            return AUDIO_FORMAT_PCM_FLOAT;
        } else {
            return AUDIO_FORMAT_INVALID;
        }
    }

private:
    Header header_;
    std::string filePath_;
    mutable std::fstream fileStream_;
    bool isHeaderValid_;
    std::streampos dataSizePos_; // Position of dataSize field for updates
};

/************************** BufferManager class ******************************/
class BufferManager {
public:
    BufferManager(size_t bufferSize) : size(0) {
        // Check for reasonable buffer size limits
        const size_t MAX_BUFFER_SIZE = 64 * 1024 * 1024; // 64MB max
        const size_t MIN_BUFFER_SIZE = 480;              // Minimum reasonable buffer size

        if (bufferSize < MIN_BUFFER_SIZE) {
            printf("Warning: Buffer size %zu is very small, using minimum %zu\n", bufferSize, MIN_BUFFER_SIZE);
            bufferSize = MIN_BUFFER_SIZE;
        }

        if (bufferSize > MAX_BUFFER_SIZE) {
            printf("Warning: Buffer size %zu is too large, limiting to %zu\n", bufferSize, MAX_BUFFER_SIZE);
            bufferSize = MAX_BUFFER_SIZE;
        }

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
    size_t size;
};

/************************** Audio Utility Functions ******************************/
class AudioUtils {
public:
    // Map -f option values to audio_format_t
    static audio_format_t parseFormatOption(int v) {
        switch (v) {
        case 1:
            return AUDIO_FORMAT_PCM_16_BIT;
        case 2:
            return AUDIO_FORMAT_PCM_8_BIT;
        case 3:
            return AUDIO_FORMAT_PCM_32_BIT;
        case 4:
            return AUDIO_FORMAT_PCM_8_24_BIT;
        case 5:
            return AUDIO_FORMAT_PCM_FLOAT;
        case 6:
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        default:
            return AUDIO_FORMAT_PCM_16_BIT; // Default to PCM 16-bit
        }
    }

    // Function to get the current time in a specific format
    static void getFormatTime(char* formatTime) {
        time_t t = time(nullptr);
        struct tm* now = localtime(&t);
        strftime(formatTime, 32, "%Y%m%d_%H.%M.%S", now);
    }

    // Build default record file path with timestamp unless an override is provided
    static std::string makeRecordFilePath(int32_t sampleRate,
                                          int32_t channelCount,
                                          uint32_t bitsPerSample,
                                          const std::string& overridePath) {
        if (!overridePath.empty()) {
            return overridePath;
        }
        char audioFile[256] = {0};
        char formatTime[32] = {0};
        AudioUtils::getFormatTime(formatTime);
        snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%ubit_%s.wav", sampleRate, channelCount,
                 bitsPerSample, formatTime);
        return std::string(audioFile);
    }
};

/************************** Audio Configuration ******************************/
struct AudioConfig {
    // Common parameters
    int32_t sampleRate = 48000;
    int32_t channelCount = 1;
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;
    size_t minFrameCount = 0;

    // Recording parameters
    audio_source_t inputSource = AUDIO_SOURCE_MIC;
    audio_input_flags_t inputFlag = AUDIO_INPUT_FLAG_NONE;
    int32_t durationSeconds = 0; // 0 = unlimited
    std::string recordFilePath = "";

    // Playback parameters
    audio_usage_t usage = AUDIO_USAGE_MEDIA;
    audio_content_type_t contentType = AUDIO_CONTENT_TYPE_UNKNOWN;
    audio_output_flags_t outputFlag = AUDIO_OUTPUT_FLAG_NONE;
    std::string playFilePath = "/data/audio_test.wav";
};

/************************** AudioMode Definitions ******************************/
enum AudioMode {
    MODE_INVALID = -1, // invalid mode
    MODE_RECORD = 0,   // record only
    MODE_PLAY = 1,     // play only
    MODE_DUPLEX = 2    // record and play simultaneously
};

/************************** Audio Operation Base Class ******************************/
class AudioOperation {
public:
    explicit AudioOperation(const AudioConfig& config) : mConfig(config) {}
    virtual ~AudioOperation() = default;
    virtual int32_t execute() = 0;

protected:
    AudioConfig mConfig;
    static constexpr uint32_t kRetryDelayUs = 2000;           // 2ms delay for retry
    static constexpr uint32_t kMaxRetries = 3;                // max retries
    static constexpr uint32_t kProgressReportInterval = 10;   // report progress every 10 seconds
    static constexpr uint32_t kLevelMeterIntervalFrames = 20; // Update level meter every 20 frames
    static constexpr int kLevelMeterScaleLength = 30;         // Length of level meter display

    // Common validation function for audio parameters
    bool validateAudioParameters() {
        if (mConfig.sampleRate <= 0) {
            printf("Error: Invalid sample rate specified\n");
            ALOGE("Invalid sample rate specified");
            return false;
        }

        if (mConfig.channelCount <= 0 || mConfig.channelCount > 32) {
            printf("Error: Invalid channel count specified\n");
            ALOGE("Invalid channel count specified");
            return false;
        }

        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        if (bytesPerSample == 0) {
            printf("Error: Invalid audio format specified\n");
            ALOGE("Invalid audio format specified");
            return false;
        }

        return true;
    }

    // Common AudioRecord initialization helper
    bool
    initializeAudioRecordHelper(sp<AudioRecord>& audioRecord, audio_channel_mask_t channelMask, size_t frameCount) {
        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.source = mConfig.inputSource;

        printf("Initialize AudioRecord, source: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: %d, "
               "frameCount: %zu\n",
               mConfig.inputSource, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);
        ALOGD("Initialize AudioRecord, source: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: %d, "
              "frameCount: %zu",
              mConfig.inputSource, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);

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
            printf("Error: Failed to set AudioRecord parameters\n");
            ALOGE("Failed to set AudioRecord parameters");
            return false;
        }

        if (audioRecord->initCheck() != NO_ERROR) {
            printf("Error: AudioRecord initialization check failed\n");
            ALOGE("AudioRecord initialization check failed");
            return false;
        }

        return true;
    }

    // Common AudioTrack initialization helper
    bool initializeAudioTrackHelper(sp<AudioTrack>& audioTrack, audio_channel_mask_t channelMask, size_t frameCount) {
        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();
        attributionSource.uid = getuid();
        attributionSource.pid = getpid();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.usage = mConfig.usage;
        attributes.content_type = mConfig.contentType;

        printf("Initialize AudioTrack, usage: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: %d, "
               "frameCount: %zu\n",
               mConfig.usage, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);
        ALOGD("Initialize AudioTrack, usage: %d, sampleRate: %d, channelCount: %d, format: %d, channelMask: %d, "
              "frameCount: %zu",
              mConfig.usage, mConfig.sampleRate, mConfig.channelCount, mConfig.format, channelMask, frameCount);

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
            printf("Error: Failed to set AudioTrack parameters\n");
            ALOGE("Failed to set AudioTrack parameters");
            return false;
        }

        if (audioTrack->initCheck() != NO_ERROR) {
            printf("Error: AudioTrack initialization check failed\n");
            ALOGE("AudioTrack initialization check failed");
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

            printf("Recording audio to file: %s\n", mConfig.recordFilePath.c_str());
            ALOGD("Recording audio to file: %s", mConfig.recordFilePath.c_str());

            if (!wavFile.createForWriting(mConfig.recordFilePath, mConfig.sampleRate, mConfig.channelCount,
                                          bytesPerSample * 8)) {
                printf("Error: Can't create record file %s\n", mConfig.recordFilePath.c_str());
                ALOGE("Can't create record file %s", mConfig.recordFilePath.c_str());
                return false;
            }
        } else {
            if (mConfig.playFilePath.empty() || access(mConfig.playFilePath.c_str(), F_OK) == -1) {
                printf("Error: File does not exist: %s\n", mConfig.playFilePath.c_str());
                ALOGE("File does not exist: %s", mConfig.playFilePath.c_str());
                return false;
            }

            // Open WAV file for reading
            if (!wavFile.openForReading(mConfig.playFilePath)) {
                printf("Error: Failed to open WAV file: %s\n", mConfig.playFilePath.c_str());
                ALOGE("Failed to open WAV file: %s", mConfig.playFilePath.c_str());
                return false;
            }

            mConfig.sampleRate = wavFile.getSampleRate();
            mConfig.channelCount = wavFile.getNumChannels();
            mConfig.format = wavFile.getAudioFormat();
            printf("Playback audio file: %s, sampleRate: %d, channelCount: %d, format: %d\n",
                   mConfig.playFilePath.c_str(), mConfig.sampleRate, mConfig.channelCount, mConfig.format);
            ALOGD("Playback audio file: %s, sampleRate: %d, channelCount: %d, format: %d", mConfig.playFilePath.c_str(),
                  mConfig.sampleRate, mConfig.channelCount, mConfig.format);
        }

        return true;
    }

    // Common progress reporting function
    bool reportProgress(uint32_t totalBytesProcessed,
                        uint32_t bytesPerSecond,
                        uint32_t& nextProgressReport,
                        const char* operationType,
                        WAVFile* wavFile = nullptr) {
        if (totalBytesProcessed >= nextProgressReport) {
            float secondsProcessed = static_cast<float>(totalBytesProcessed) / bytesPerSecond;
            float mbProcessed = static_cast<float>(totalBytesProcessed) / (1024u * 1024u);
            printf("%s ... , processed %.2f seconds, %.2f MB\n", operationType, secondsProcessed, mbProcessed);
            // ALOGD("%s ... , processed %.2f seconds, %.2f MB", operationType, secondsProcessed, mbProcessed);
            nextProgressReport += bytesPerSecond * kProgressReportInterval;

            // Update file header if it's a recording operation and wavFile is provided
            if (strcmp(operationType, "Recording") == 0 && wavFile != nullptr) {
                wavFile->updateHeader();
            }
            return true;
        }
        return false;
    }

    // Simple peak level meter implementation with low CPU usage
    void updateLevelMeter(const char* buffer, size_t bufferSize, size_t bytesPerSample) {
        // Skip level meter updates to reduce CPU usage
        static uint32_t frameCount = 0;
        if (++frameCount < kLevelMeterIntervalFrames) {
            return;
        }
        frameCount = 0;

        // Find peak amplitude in buffer (simple and efficient method)
        float peakAmplitude = 0.0f;
        const int16_t* sampleData = reinterpret_cast<const int16_t*>(buffer);
        size_t numSamples = bufferSize / bytesPerSample;

        // Process 16-bit and 32-bit integer audio
        if (bytesPerSample == 2) {
            for (size_t i = 0; i < numSamples; ++i) {
                float amplitude = static_cast<float>(abs(sampleData[i])) / 32768.0f; // Normalize to [0, 1]
                if (amplitude > peakAmplitude) {
                    peakAmplitude = amplitude;
                }
            }
        } else if (bytesPerSample == 4) {
            const int32_t* int32Data = reinterpret_cast<const int32_t*>(buffer);
            for (size_t i = 0; i < numSamples; ++i) {
                float amplitude = static_cast<float>(llabs(int32Data[i])) / 2147483648.0f; // Normalize to [0, 1]
                if (amplitude > peakAmplitude) {
                    peakAmplitude = amplitude;
                }
            }
        }

        // Convert to dB scale (with floor at -60dB)
        float dbLevel = (peakAmplitude > 0.0f) ? 20.0f * log10(peakAmplitude) : -60.0f;
        dbLevel = std::max(dbLevel, -60.0f);

        // Display level meter
        int levelLength = static_cast<int>((dbLevel + 60.0f) / 60.0f * kLevelMeterScaleLength);
        levelLength = std::max(0, std::min(levelLength, kLevelMeterScaleLength));

        // Clear previous level meter line
        printf("\rLevel: [");
        for (int i = 0; i < levelLength; ++i) {
            printf("|");
        }
        for (int i = levelLength; i < kLevelMeterScaleLength; ++i) {
            printf(" ");
        }
        printf("] %.1f dB", dbLevel);
        fflush(stdout);
        // ALOGI("Audio Level: %.1f dB", dbLevel);
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
            printf("Error: Failed to setup WAV file or initialize AudioRecord\n");
            ALOGE("Failed to setup WAV file or initialize AudioRecord");
            return -1;
        }

        // Register signal handler
        setupSignalHandler(&wavFile);

        // Start recording
        if (!startRecording(audioRecord)) {
            wavFile.close();
            return -1;
        }

        // Main recording loop
        int32_t result = recordLoop(audioRecord, wavFile);

        // Cleanup
        audioRecord->stop();
        wavFile.finalize();

        return result;
    }

private:
    static std::atomic<WAVFile*> g_wavFile;

    bool initializeAudioRecord(sp<AudioRecord>& audioRecord) {
        audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(mConfig.channelCount);

        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMask) !=
            NO_ERROR) {
            printf("Warning: Cannot get min frame count, using default value\n");
            ALOGW("Cannot get min frame count, using default value");
        }

        if (mConfig.minFrameCount < static_cast<size_t>((mConfig.sampleRate * 10) / 1000)) {
            mConfig.minFrameCount = static_cast<size_t>((mConfig.sampleRate * 10) / 1000);
            printf("Warning: Reset minFrameCount to %zu\n", mConfig.minFrameCount);
            ALOGW("Reset minFrameCount to %zu", mConfig.minFrameCount);
        }

        size_t frameCount = mConfig.minFrameCount * 2;
        return initializeAudioRecordHelper(audioRecord, channelMask, frameCount);
    }

    void setupSignalHandler(WAVFile* wavFile) {
        g_wavFile.store(wavFile);
        signal(SIGINT, signalHandler);
    }

    bool startRecording(const sp<AudioRecord>& audioRecord) {
        printf("Starting AudioRecord\n");
        ALOGD("Starting AudioRecord");
        status_t startResult = audioRecord->start();
        if (startResult != NO_ERROR) {
            printf("Error: AudioRecord start failed with status %d\n", startResult);
            ALOGE("AudioRecord start failed with status %d", startResult);
            return false;
        }
        return true;
    }

    int32_t recordLoop(const sp<AudioRecord>& audioRecord, WAVFile& wavFile) {
        ssize_t bytesRead = 0;
        uint32_t totalBytesRead = 0;
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        size_t bufferSize = (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
        uint32_t bytesPerSecond = mConfig.sampleRate * mConfig.channelCount * bytesPerSample;

        // Setup buffer
        BufferManager bufferManager(bufferSize);
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            ALOGE("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            printf("Recording started. Recording for %d seconds...\n", mConfig.durationSeconds);
            ALOGD("Recording started. Recording for %d seconds...", mConfig.durationSeconds);
        } else {
            printf("Recording started. Press Ctrl+C to stop.\n");
            ALOGD("Recording started. Press Ctrl+C to stop.");
        }

        uint32_t retryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
        uint32_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint32_t>(mConfig.durationSeconds) * bytesPerSecond, MAX_AUDIO_DATA_SIZE)
                : MAX_AUDIO_DATA_SIZE;

        printf("Set maxBytesToRecord to %u bytes\n", maxBytesToRecord);
        ALOGD("Set maxBytesToRecord to %u bytes", maxBytesToRecord);

        while (totalBytesRead < maxBytesToRecord) {
            // Read data from AudioRecord
            bytesRead = audioRecord->read(buffer, bufferSize);
            if (bytesRead < 0) {
                printf("Warning: AudioRecord read returned error %zd, retrying...\n", bytesRead);
                ALOGW("AudioRecord read returned error %zd, retrying...", bytesRead);
                retryCount++;
                if (retryCount >= kMaxRetries) {
                    printf("Error: AudioRecord read failed after maximum retries\n");
                    ALOGE("AudioRecord read failed after maximum retries");
                    break;
                }
                usleep(kRetryDelayUs);
                continue;
            } else if (bytesRead == 0) {
                usleep(kRetryDelayUs);
                continue;
            }

            // Reset retry count
            retryCount = 0;

            // Update total bytes read
            totalBytesRead += static_cast<uint32_t>(bytesRead);

            // Update level meter
            updateLevelMeter(buffer, static_cast<size_t>(bytesRead), bytesPerSample);

            // Write data to WAV file
            size_t bytesWritten = wavFile.writeData(buffer, static_cast<size_t>(bytesRead));
            if (bytesWritten != static_cast<size_t>(bytesRead)) {
                printf("Error: Failed to write to output file\n");
                ALOGE("Failed to write to output file");
                break;
            }

            // Report progress
            reportProgress(totalBytesRead, bytesPerSecond, nextProgressReport, "Recording", &wavFile);
        }

        printf("Recording finished. Recorded %u bytes, File saved: %s\n", totalBytesRead,
               wavFile.getFilePath().c_str());
        ALOGD("Recording finished. Recorded %u bytes, File saved: %s", totalBytesRead, wavFile.getFilePath().c_str());

        return 0;
    }

    // Signal handler for SIGINT (Ctrl+C)
    static void signalHandler(int signal) {
        if (signal == SIGINT) {
            printf("\nReceived SIGINT (Ctrl+C), finalizing recording...\n");
            WAVFile* wavFile = g_wavFile.exchange(nullptr);
            if (wavFile != nullptr) {
                wavFile->finalize(); // Finalize the WAV file
            }
            exit(0);
        }
    }
};

// Initialize static member
std::atomic<WAVFile*> AudioRecordOperation::g_wavFile{nullptr};

/************************** Audio Play Operation ******************************/
class AudioPlayOperation : public AudioOperation {
public:
    explicit AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        WAVFile wavFile;
        sp<AudioTrack> audioTrack;

        if (!setupWavFileHelper(wavFile, false) || !validateAudioParameters() || !initializeAudioTrack(audioTrack)) {
            printf("Error: Failed to setup WAV file or initialize AudioTrack\n");
            ALOGE("Failed to setup WAV file or initialize AudioTrack");
            return -1;
        }

        // Start playback
        if (!startPlayback(audioTrack)) {
            return -1;
        }

        // Main playback loop
        int32_t result = playLoop(audioTrack, wavFile);

        // Cleanup
        audioTrack->stop();
        wavFile.close();

        return result;
    }

private:
    bool initializeAudioTrack(sp<AudioTrack>& audioTrack) {
        audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(mConfig.channelCount);

        if (mConfig.minFrameCount < static_cast<size_t>((mConfig.sampleRate * 10) / 1000)) {
            mConfig.minFrameCount = static_cast<size_t>((mConfig.sampleRate * 10) / 1000);
        }

        size_t frameCount = mConfig.minFrameCount * 2;
        printf("AudioTrack minFrameCount: %zu, using frameCount: %zu\n", mConfig.minFrameCount, frameCount);
        ALOGD("AudioTrack minFrameCount: %zu, using frameCount: %zu", mConfig.minFrameCount, frameCount);

        return initializeAudioTrackHelper(audioTrack, channelMask, frameCount);
    }

    bool startPlayback(const sp<AudioTrack>& audioTrack) {
        printf("Starting AudioTrack\n");
        ALOGD("Starting AudioTrack");
        status_t startResult = audioTrack->start();
        if (startResult != NO_ERROR) {
            printf("Error: AudioTrack start failed with status %d\n", startResult);
            ALOGE("AudioTrack start failed with status %d", startResult);
            return false;
        }
        return true;
    }

    int32_t playLoop(const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        size_t bytesRead = 0;
        ssize_t bytesWritten = 0;
        uint32_t totalBytesPlayed = 0;
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        size_t bufferSize = (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
        uint32_t bytesPerSecond = mConfig.sampleRate * mConfig.channelCount * bytesPerSample;

        // Setup buffer
        BufferManager bufferManager(bufferSize);
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            ALOGE("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        printf("Playback started. Playing audio from: %s\n", mConfig.playFilePath.c_str());
        ALOGD("Playback started. Playing audio from: %s", mConfig.playFilePath.c_str());

        uint32_t retryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;

        while (true) {
            bytesRead = wavFile.readData(buffer, bufferSize);
            if (bytesRead == 0) {
                printf("End of file reached\n");
                ALOGD("End of file reached");
                break;
            }

            // Write buffer to AudioTrack with retry logic
            bytesWritten = 0;
            retryCount = 0;
            while (static_cast<size_t>(bytesWritten) < bytesRead && retryCount < kMaxRetries) {
                ssize_t written =
                    audioTrack->write(buffer + bytesWritten, bytesRead - static_cast<size_t>(bytesWritten));
                if (written < 0) {
                    printf("Warning: AudioTrack write failed with error %zd, retrying...\n", written);
                    ALOGW("AudioTrack write failed with error %zd, retrying...", written);
                    retryCount++;
                    usleep(kRetryDelayUs);
                    continue;
                }
                bytesWritten += written;
                retryCount = 0; // Reset retry count on successful write
            }

            if (retryCount >= kMaxRetries) {
                printf("Error: AudioTrack write failed after maximum retries\n");
                ALOGE("AudioTrack write failed after maximum retries");
                break;
            }

            // Update total bytes played
            totalBytesPlayed += static_cast<uint32_t>(bytesRead);

            // Update level meter
            updateLevelMeter(buffer, bytesRead, bytesPerSample);

            // Report progress
            reportProgress(totalBytesPlayed, bytesPerSecond, nextProgressReport, "Playing");
        }

        printf("Playback finished. Total bytes played: %u\n", totalBytesPlayed);
        ALOGD("Playback finished. Total bytes played: %u", totalBytesPlayed);

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
            printf("Error: Failed to initialize audio components or setup WAV file\n");
            ALOGE("Failed to initialize audio components or setup WAV file");
            return -1;
        }

        // Register signal handler
        setupSignalHandler(&wavFile);

        // Start recording and playback
        if (!startAudioComponents(audioRecord, audioTrack)) {
            wavFile.close();
            return -1;
        }

        // Main duplex loop
        int32_t result = duplexLoop(audioRecord, audioTrack, wavFile);

        // Cleanup
        audioRecord->stop();
        audioTrack->stop();
        wavFile.finalize();

        return result;
    }

private:
    static std::atomic<WAVFile*> g_wavFile;

    bool initializeAudioComponents(sp<AudioRecord>& audioRecord, sp<AudioTrack>& audioTrack) {
        audio_channel_mask_t channelMaskIn = audio_channel_in_mask_from_count(mConfig.channelCount);
        audio_channel_mask_t channelMaskOut = audio_channel_out_mask_from_count(mConfig.channelCount);

        // Get minimum frame count from AudioRecord
        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMaskIn) !=
            NO_ERROR) {
            printf("Warning: Cannot get min frame count, using default value\n");
            ALOGW("Cannot get min frame count, using default value");
        }

        if (mConfig.minFrameCount < static_cast<size_t>((mConfig.sampleRate * 10) / 1000)) {
            mConfig.minFrameCount = static_cast<size_t>((mConfig.sampleRate * 10) / 1000);
        }
        size_t frameCount = mConfig.minFrameCount * 2;

        // Initialize AudioRecord
        if (!initializeAudioRecordHelper(audioRecord, channelMaskIn, frameCount) ||
            !initializeAudioTrackHelper(audioTrack, channelMaskOut, frameCount)) {
            return false;
        }

        return true;
    }

    void setupSignalHandler(WAVFile* wavFile) {
        g_wavFile.store(wavFile);
        signal(SIGINT, signalHandler);
    }

    bool startAudioComponents(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack) {
        printf("Starting AudioRecord\n");
        ALOGD("Starting AudioRecord");
        status_t recordStartResult = audioRecord->start();
        if (recordStartResult != NO_ERROR) {
            printf("Error: AudioRecord start failed with status %d\n", recordStartResult);
            ALOGE("AudioRecord start failed with status %d", recordStartResult);
            return false;
        }

        printf("Starting AudioTrack\n");
        ALOGD("Starting AudioTrack");
        status_t playStartResult = audioTrack->start();
        if (playStartResult != NO_ERROR) {
            printf("Error: AudioTrack start failed with status %d\n", playStartResult);
            ALOGE("AudioTrack start failed with status %d", playStartResult);
            audioRecord->stop();
            return false;
        }

        return true;
    }

    int32_t duplexLoop(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack, WAVFile& wavFile) {
        ssize_t bytesRead = 0;
        ssize_t bytesWritten = 0;
        uint32_t totalBytesRead = 0;
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        size_t bufferSize = (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
        uint32_t bytesPerSecond = mConfig.sampleRate * mConfig.channelCount * bytesPerSample;

        // Setup buffer
        BufferManager bufferManager(bufferSize);
        if (!bufferManager.isValid()) {
            printf("Error: Failed to create valid buffer manager\n");
            ALOGE("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            printf("Duplex audio started. Recording for %d seconds...\n", mConfig.durationSeconds);
            ALOGD("Duplex audio started. Recording for %d seconds...", mConfig.durationSeconds);
        } else {
            printf("Duplex audio started. Press Ctrl+C to stop.\n");
            ALOGD("Duplex audio started. Press Ctrl+C to stop.");
        }

        uint32_t recordRetryCount = 0;
        uint32_t playRetryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
        uint32_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint32_t>(mConfig.durationSeconds) * bytesPerSecond, MAX_AUDIO_DATA_SIZE)
                : MAX_AUDIO_DATA_SIZE;

        printf("Set maxBytesToRecord to %u bytes\n", maxBytesToRecord);
        ALOGD("Set maxBytesToRecord to %u bytes", maxBytesToRecord);

        bool recording = true;
        bool playing = true;

        while (recording && playing && totalBytesRead < maxBytesToRecord) {
            // Read from AudioRecord
            bytesRead = audioRecord->read(buffer, bufferSize);
            if (bytesRead < 0) {
                printf("Warning: AudioRecord read returned error %zd, retrying...\n", bytesRead);
                ALOGW("AudioRecord read returned error %zd, retrying...", bytesRead);
                recordRetryCount++;
                if (recordRetryCount >= kMaxRetries) {
                    printf("Error: AudioRecord read failed after maximum retries\n");
                    ALOGE("AudioRecord read failed after maximum retries");
                    recording = false;
                    break;
                }
                usleep(kRetryDelayUs);
                continue;
            } else if (bytesRead == 0) {
                usleep(kRetryDelayUs);
                continue;
            }

            // Reset retry count
            recordRetryCount = 0;

            // Update total bytes read
            totalBytesRead += static_cast<uint32_t>(bytesRead);

            // Update level meter
            updateLevelMeter(buffer, static_cast<size_t>(bytesRead), bytesPerSample);

            // Write to WAV file
            size_t bytesWrittenToFile = wavFile.writeData(buffer, static_cast<size_t>(bytesRead));
            if (bytesWrittenToFile != static_cast<size_t>(bytesRead)) {
                printf("Error: Failed to write to output file\n");
                ALOGE("Failed to write to output file");
                recording = false;
                playing = false;
                break;
            }

            // Report progress
            reportProgress(totalBytesRead, bytesPerSecond, nextProgressReport, "Recording", &wavFile);

            // Check recording finish
            if (totalBytesRead >= maxBytesToRecord) {
                recording = false;
            }

            // Write to AudioTrack with retry logic
            bytesWritten = 0;
            playRetryCount = 0;
            while (static_cast<size_t>(bytesWritten) < static_cast<size_t>(bytesRead) && playing) {
                ssize_t written = audioTrack->write(buffer + bytesWritten,
                                                    static_cast<size_t>(bytesRead) - static_cast<size_t>(bytesWritten));
                if (written < 0) {
                    printf("Warning: AudioTrack write failed with error %zd, retrying...\n", written);
                    ALOGW("AudioTrack write failed with error %zd, retrying...", written);
                    playRetryCount++;
                    if (playRetryCount >= kMaxRetries) {
                        printf("Error: AudioTrack write failed after maximum retries\n");
                        ALOGE("AudioTrack write failed after maximum retries");
                        playing = false;
                        break;
                    }
                    usleep(kRetryDelayUs);
                    continue;
                }
                bytesWritten += written;
                playRetryCount = 0; // Reset retry count on successful write
            }
        }

        printf("Duplex audio completed. Total bytes read: %u, File saved: %s\n", totalBytesRead,
               wavFile.getFilePath().c_str());
        ALOGD("Duplex audio completed. Total bytes read: %u, File saved: %s", totalBytesRead,
              wavFile.getFilePath().c_str());

        return 0;
    }

    // Signal handler for SIGINT (Ctrl+C)
    static void signalHandler(int signal) {
        if (signal == SIGINT) {
            printf("\nReceived SIGINT (Ctrl+C), finalizing recording...\n");
            WAVFile* wavFile = g_wavFile.exchange(nullptr);
            if (wavFile != nullptr) {
                wavFile->finalize(); // Finalize the WAV file
            }
            exit(0);
        }
    }
};

// Initialize static member
std::atomic<WAVFile*> AudioDuplexOperation::g_wavFile{nullptr};

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
        while ((opt = getopt(argc, argv, "m:s:r:c:f:F:u:C:O:z:d:h")) != -1) {
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
            case 'h': // help for use
                showHelp();
                exit(0);
            default:
                showHelp();
                exit(-1);
            }
        }

        // Get audio file path from remaining argument
        if (optind < argc) {
            if (mode == MODE_PLAY) {
                config.playFilePath = argv[optind];
            } else {
                config.recordFilePath = argv[optind];
            }
        }
    }

    static void showHelp() {
        printf("Audio Test Client - Combined Record and Play Demo\n");
        printf("Usage: audio_test_client -m{mode} [options] [audio_file]\n");
        printf("Modes:\n");
        printf("  -m0   Record mode\n");
        printf("  -m1   Play mode\n");
        printf("  -m2   Duplex mode (record and play simultaneously)\n\n");

        printf("Record Options:\n");
        printf("  -s{inputSource}     Set audio source\n");
        printf("                       0: AUDIO_SOURCE_DEFAULT\n");
        printf("                       1: AUDIO_SOURCE_MIC (Microphone)\n");
        printf("                       2: AUDIO_SOURCE_VOICE_UPLINK (Phone call uplink)\n");
        printf("                       3: AUDIO_SOURCE_VOICE_DOWNLINK (Phone call downlink)\n");
        printf("                       4: AUDIO_SOURCE_VOICE_CALL (Phone call bidirectional)\n");
        printf("                       5: AUDIO_SOURCE_CAMCORDER (Video camera)\n");
        printf("                       6: AUDIO_SOURCE_VOICE_RECOGNITION (Voice recognition)\n");
        printf("                       7: AUDIO_SOURCE_VOICE_COMMUNICATION (Voice communication)\n");
        printf("                       8: AUDIO_SOURCE_REMOTE_SUBMIX (Remote submix)\n");
        printf("                       9: AUDIO_SOURCE_UNPROCESSED (Unprocessed audio)\n");
        printf("                       10: AUDIO_SOURCE_VOICE_PERFORMANCE (Voice performance)\n");
        printf("                       1997: AUDIO_SOURCE_ECHO_REFERENCE (Echo reference)\n");
        printf("                       1998: AUDIO_SOURCE_FM_TUNER (FM tuner)\n");
        printf("                       1999: AUDIO_SOURCE_HOTWORD (Hotword)\n");
        printf("                       2000: AUDIO_SOURCE_ULTRASOUND (Ultrasound)\n");
        printf("  -r{sampleRate}      Set sample rate (e.g., 8000, 16000, 48000)\n");
        printf("  -c{channelCount}    Set channel count (1, 2, 4, 6, 8, 12, 16)\n");
        printf("  -f{format}          Set audio format\n");
        printf("                       0: AUDIO_FORMAT_DEFAULT (Default audio format)\n");
        printf("                       1: AUDIO_FORMAT_PCM_16_BIT (16-bit PCM)\n");
        printf("                       2: AUDIO_FORMAT_PCM_8_BIT (8-bit PCM)\n");
        printf("                       3: AUDIO_FORMAT_PCM_32_BIT (32-bit PCM)\n");
        printf("                       4: AUDIO_FORMAT_PCM_8_24_BIT (8-bit PCM with 24-bit padding)\n");
        printf("                       5: AUDIO_FORMAT_PCM_FLOAT (32-bit floating-point PCM)\n");
        printf("                       6: AUDIO_FORMAT_PCM_24_BIT_PACKED (24-bit packed PCM)\n");
        printf("  -F{inputFlag}       Set audio input flag\n");
        printf("                       0: AUDIO_INPUT_FLAG_NONE (No special input flag)\n");
        printf("                       1: AUDIO_INPUT_FLAG_FAST (Fast input flag)\n");
        printf("                       2: AUDIO_INPUT_FLAG_HW_HOTWORD (Hardware hotword input)\n");
        printf("                       4: AUDIO_INPUT_FLAG_RAW (Raw audio input)\n");
        printf("                       8: AUDIO_INPUT_FLAG_SYNC (Synchronous audio input)\n");
        printf("                       16: AUDIO_INPUT_FLAG_MMAP_NOIRQ (MMAP input without IRQ)\n");
        printf("                       32: AUDIO_INPUT_FLAG_VOIP_TX (VoIP transmission input)\n");
        printf("                       64: AUDIO_INPUT_FLAG_HW_AV_SYNC (Hardware audio/visual sync input)\n");
        printf("                       128: AUDIO_INPUT_FLAG_DIRECT (Direct audio input)\n");
        printf("                       256: AUDIO_INPUT_FLAG_ULTRASOUND (Ultrasound input)\n");
        printf("                       512: AUDIO_INPUT_FLAG_HOTWORD_TAP (Hotword tap input)\n");
        printf("                       1024: AUDIO_INPUT_FLAG_HW_LOOKBACK (Hardware lookback input)\n");
        printf("  -z{minFrameCount}   Set min frame count (default: system selected)\n");
        printf("  -d{duration}        Set recording duration(s) (0 = unlimited)\n\n");

        printf("Play Options:\n");
        printf("  -u{usage}           Set audio usage\n");
        printf("                       0: AUDIO_USAGE_UNKNOWN (Unknown audio usage)\n");
        printf("                       1: AUDIO_USAGE_MEDIA (Media playback)\n");
        printf("                       2: AUDIO_USAGE_VOICE_COMMUNICATION (Voice call)\n");
        printf("                       3: AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING (Call signaling)\n");
        printf("                       4: AUDIO_USAGE_ALARM (Alarm clock)\n");
        printf("                       5: AUDIO_USAGE_NOTIFICATION (General notification)\n");
        printf("                       6: AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE (Ringtone)\n");
        printf("                       7: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST (Incoming call)\n");
        printf("                       8: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT (Instant message)\n");
        printf("                       9: AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED (Delayed message)\n");
        printf("                      10: AUDIO_USAGE_NOTIFICATION_EVENT (Event notification)\n");
        printf("                      11: AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY (Accessibility)\n");
        printf("                      12: AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE (Navigation)\n");
        printf("                      13: AUDIO_USAGE_ASSISTANCE_SONIFICATION (System sonification)\n");
        printf("                      14: AUDIO_USAGE_GAME (Game audio)\n");
        printf("                      15: AUDIO_USAGE_VIRTUAL_SOURCE (Virtual source)\n");
        printf("                      16: AUDIO_USAGE_ASSISTANT (Assistant)\n");
        printf("                      17: AUDIO_USAGE_CALL_ASSISTANT (Call assistant)\n");
        printf("                      1000: AUDIO_USAGE_EMERGENCY (Emergency)\n");
        printf("                      1001: AUDIO_USAGE_SAFETY (Safety)\n");
        printf("                      1002: AUDIO_USAGE_VEHICLE_STATUS (Vehicle status)\n");
        printf("                      1003: AUDIO_USAGE_ANNOUNCEMENT (Announcement)\n");
        printf("                      1004: AUDIO_USAGE_SPEAKER_CLEANUP (Speaker cleanup)\n");
        printf("  -C{contentType}     Set content type\n");
        printf("                       0: AUDIO_CONTENT_TYPE_UNKNOWN (Unknown content type)\n");
        printf("                       1: AUDIO_CONTENT_TYPE_SPEECH (Speech)\n");
        printf("                       2: AUDIO_CONTENT_TYPE_MUSIC (Music)\n");
        printf("                       3: AUDIO_CONTENT_TYPE_MOVIE (Movie)\n");
        printf("                       4: AUDIO_CONTENT_TYPE_SONIFICATION (Sonification)\n");
        printf("                       1997: AUDIO_CONTENT_TYPE_ULTRASOUND (Ultrasound)\n");
        printf("  -O{outputFlag}      Set audio output flag\n");
        printf("                       0: AUDIO_OUTPUT_FLAG_NONE (No special output flag)\n");
        printf("                       1: AUDIO_OUTPUT_FLAG_DIRECT (Direct audio output)\n");
        printf("                       2: AUDIO_OUTPUT_FLAG_PRIMARY (Primary audio output)\n");
        printf("                       4: AUDIO_OUTPUT_FLAG_FAST (Fast audio output)\n");
        printf("                       8: AUDIO_OUTPUT_FLAG_DEEP_BUFFER (Deep buffer audio output)\n");
        printf("                       16: AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD (Compress offload audio output)\n");
        printf("                       32: AUDIO_OUTPUT_FLAG_NON_BLOCKING (Non-blocking audio output)\n");
        printf("                       64: AUDIO_OUTPUT_FLAG_HW_AV_SYNC (Hardware audio/visual sync output)\n");
        printf("                       128: AUDIO_OUTPUT_FLAG_TTS (Text-to-speech output)\n");
        printf("                       256: AUDIO_OUTPUT_FLAG_RAW (Raw audio output)\n");
        printf("                       512: AUDIO_OUTPUT_FLAG_SYNC (Synchronous audio output)\n");
        printf("                       1024: AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO (IEC958 non-audio output)\n");
        printf("                       8192: AUDIO_OUTPUT_FLAG_DIRECT_PCM (Direct PCM audio output)\n");
        printf("                       16384: AUDIO_OUTPUT_FLAG_MMAP_NOIRQ (MMAP no IRQ audio output)\n");
        printf("                       32768: AUDIO_OUTPUT_FLAG_VOIP_RX (VoIP receive audio output)\n");
        printf("                       65536: AUDIO_OUTPUT_FLAG_INCALL_MUSIC (In-call music audio output)\n");
        printf("                       131072: AUDIO_OUTPUT_FLAG_GAPLESS_OFFLOAD (Gapless offload audio output)\n");
        printf("                       262144: AUDIO_OUTPUT_FLAG_SPATIALIZER (Spatializer audio output)\n");
        printf("                       524288: AUDIO_OUTPUT_FLAG_ULTRASOUND (Ultrasound audio output)\n");
        printf("                       1048576: AUDIO_OUTPUT_FLAG_BIT_PERFECT (Bit perfect audio output)\n");
        printf("  -z{minFrameCount}   Set min frame count (default: system selected)\n\n");

        printf("For more details, please refer to system/media/audio/include/system/audio-hal-enums.h\n\n");

        printf("General Options:\n");
        printf("  -h                  Show this help message\n\n");

        printf("Examples:\n");
        printf("    Record: audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480 -d10\n");
        printf("    Play:   audio_test_client -m1 -u5 -C0 -O4 -z480 /data/audio_test.wav\n");
        printf("    Duplex: audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480 -d30\n");
    }
};

/************************** Main function ******************************/
int32_t main(int32_t argc, char** argv) {
    AudioMode mode = MODE_INVALID;
    AudioConfig config;

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
