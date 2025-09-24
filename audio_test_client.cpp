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

private:
    Header header_;
    std::string filePath_;
    mutable std::fstream fileStream_;
    bool isHeaderValid_;
    std::streampos dataSizePos_; // Position of dataSize field for updates

public:
    WAVFile() : isHeaderValid_(false) {
        memset(&header_, 0, sizeof(Header));
        static_assert(sizeof(Header) == 44, "WAV header size must be 44 bytes");
    }

    ~WAVFile() {
        if (fileStream_.is_open()) {
            fileStream_.close();
        }
    }

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
        if (!fileStream_.is_open() || !isHeaderValid_) {
            return 0;
        }

        // Prevent 32-bit overflow of WAV header sizes
        if (size > 0 && (header_.dataSize > UINT32_MAX - size)) {
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

    int32_t getSampleRate() const { return header_.sampleRate; }
    int32_t getNumChannels() const { return header_.numChannels; }
    uint32_t getBitsPerSample() const { return header_.bitsPerSample; }
};

/************************** BufferManager class ******************************/
class BufferManager {
private:
    std::unique_ptr<char[]> buffer;
    size_t size;

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
        getFormatTime(formatTime);
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
protected:
    AudioConfig mConfig;
    static constexpr uint32_t kRetryDelayUs = 2000;         // 2ms delay for retry
    static constexpr uint32_t kMaxRetries = 3;              // max retries
    static constexpr uint32_t kProgressReportInterval = 10; // report progress every 10 seconds

public:
    explicit AudioOperation(const AudioConfig& config) : mConfig(config) {}
    virtual ~AudioOperation() = default;
    virtual int32_t execute() = 0;

protected:
    // Common error handling utility
    void logError(const char* message) {
        printf("Error: %s\n", message);
        ALOGE("Error: %s\n", message);
    }

    // Common warning handling utility
    void logWarning(const char* message) {
        printf("Warning: %s\n", message);
        ALOGW("Warning: %s\n", message);
    }

    // Common info handling utility
    void logInfo(const char* message) {
        printf("%s\n", message);
        ALOGD("%s\n", message);
    }
};

/************************** Audio Record Operation ******************************/
class AudioRecordOperation : public AudioOperation {
private:
    static std::atomic<WAVFile*> g_wavFile;

public:
    explicit AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        // Validate parameters
        if (!validateParameters()) {
            return -1;
        }

        // Initialize AudioRecord
        sp<AudioRecord> audioRecord;
        if (!initializeAudioRecord(audioRecord)) {
            return -1;
        }

        // Setup WAV file
        WAVFile wavFile;
        std::string audioFilePath;
        if (!setupWavFile(wavFile, audioFilePath)) {
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
        int32_t result = recordLoop(audioRecord, wavFile, audioFilePath);

        // Cleanup
        audioRecord->stop();
        wavFile.finalize();

        return result;
    }

private:
    bool validateParameters() {
        if (mConfig.sampleRate <= 0) {
            logError("Invalid sample rate specified");
            return false;
        }

        if (mConfig.channelCount <= 0 || mConfig.channelCount > 32) {
            logError("Invalid channel count specified");
            return false;
        }

        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        if (bytesPerSample == 0) {
            logError("Invalid audio format specified");
            return false;
        }

        return true;
    }

    bool initializeAudioRecord(sp<AudioRecord>& audioRecord) {
        if (mConfig.minFrameCount == 0) {
            mConfig.minFrameCount = (mConfig.sampleRate / 1000) * 20; // 20ms
        }

        audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(mConfig.channelCount);

        // Get minimum frame count from AudioRecord
        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMask) !=
            NO_ERROR) {
            logWarning("Cannot get min frame count, using default value");
        }

        size_t frameCount = mConfig.minFrameCount * 2;

        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.source = mConfig.inputSource;

        logInfo("Initializing AudioRecord");
        audioRecord = new AudioRecord(attributionSource);
        if (audioRecord->set(mConfig.inputSource, mConfig.sampleRate, mConfig.format, channelMask, frameCount, nullptr,
                             nullptr, 0, false, AUDIO_SESSION_ALLOCATE, AudioRecord::TRANSFER_SYNC, mConfig.inputFlag,
                             getuid(), getpid(), &attributes, AUDIO_PORT_HANDLE_NONE) != NO_ERROR) {
            logError("Failed to set AudioRecord parameters");
            return false;
        }

        if (audioRecord->initCheck() != NO_ERROR) {
            logError("AudioRecord initialization check failed");
            return false;
        }

        return true;
    }

    bool setupWavFile(WAVFile& wavFile, std::string& audioFilePath) {
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        audioFilePath = AudioUtils::makeRecordFilePath(mConfig.sampleRate, mConfig.channelCount, bytesPerSample * 8,
                                                       mConfig.recordFilePath);

        logInfo((std::string("Recording audio to file: ") + audioFilePath).c_str());

        if (!wavFile.createForWriting(audioFilePath, mConfig.sampleRate, mConfig.channelCount, bytesPerSample * 8)) {
            logError((std::string("Can't create output file ") + audioFilePath).c_str());
            return false;
        }

        return true;
    }

    void setupSignalHandler(WAVFile* wavFile) {
        g_wavFile.store(wavFile);
        signal(SIGINT, signalHandler);
    }

    bool startRecording(const sp<AudioRecord>& audioRecord) {
        logInfo("Starting AudioRecord");
        status_t startResult = audioRecord->start();
        if (startResult != NO_ERROR) {
            logError((std::string("AudioRecord start failed with status ") + std::to_string(startResult)).c_str());
            return false;
        }
        return true;
    }

    int32_t recordLoop(const sp<AudioRecord>& audioRecord, WAVFile& wavFile, const std::string& audioFilePath) {
        ssize_t bytesRead = 0;
        uint32_t totalBytesRead = 0;
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        size_t bufferSize = (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
        uint32_t bytesPerSecond = mConfig.sampleRate * mConfig.channelCount * bytesPerSample;

        // Setup buffer
        BufferManager bufferManager(bufferSize);
        if (!bufferManager.isValid()) {
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            logInfo((std::string("Recording started. Recording for ") + std::to_string(mConfig.durationSeconds) +
                     " seconds...")
                        .c_str());
        } else {
            logInfo("Recording started. Press Ctrl+C to stop.");
        }

        uint32_t retryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
        uint32_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint32_t>(mConfig.durationSeconds) * bytesPerSecond, MAX_AUDIO_DATA_SIZE)
                : MAX_AUDIO_DATA_SIZE;

        logInfo((std::string("Set maxBytesToRecord to ") + std::to_string(maxBytesToRecord) + " bytes").c_str());

        while (totalBytesRead < maxBytesToRecord) {
            // Read data from AudioRecord
            bytesRead = audioRecord->read(buffer, bufferSize);
            if (bytesRead < 0) {
                logWarning(
                    (std::string("AudioRecord read returned error ") + std::to_string(bytesRead) + ", retrying...")
                        .c_str());
                retryCount++;
                if (retryCount >= kMaxRetries) {
                    logError("AudioRecord read failed after maximum retries");
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

            // Write data to WAV file
            size_t bytesWritten = wavFile.writeData(buffer, static_cast<size_t>(bytesRead));
            if (bytesWritten != static_cast<size_t>(bytesRead)) {
                logError("Failed to write to output file");
                break;
            }

            // Report progress
            if (totalBytesRead >= nextProgressReport) {
                float secondsRecorded = static_cast<float>(totalBytesRead) / bytesPerSecond;
                float mbRecorded = static_cast<float>(totalBytesRead) / (1024u * 1024u);
                printf("Recording ... , recorded %.2f seconds, %.2f MB\n", secondsRecorded, mbRecorded);
                nextProgressReport += bytesPerSecond * kProgressReportInterval;

                // Periodically update header to ensure file validity if recording is interrupted
                wavFile.updateHeader();
            }
        }

        logInfo((std::string("Recording finished. Recorded ") + std::to_string(totalBytesRead) + " bytes").c_str());
        logInfo((std::string("Audio file saved to: ") + audioFilePath).c_str());

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
        // Validate input file
        if (mConfig.playFilePath.empty() || access(mConfig.playFilePath.c_str(), F_OK) == -1) {
            logError((std::string("File does not exist: ") + mConfig.playFilePath).c_str());
            return -1;
        }

        // Open WAV file for reading
        WAVFile wavFile;
        if (!wavFile.openForReading(mConfig.playFilePath)) {
            logError((std::string("Failed to open WAV file: ") + mConfig.playFilePath).c_str());
            return -1;
        }

        // Get WAV file params and update config
        if (!updateConfigFromWavFile(wavFile)) {
            return -1;
        }

        // Initialize AudioTrack
        sp<AudioTrack> audioTrack;
        if (!initializeAudioTrack(audioTrack)) {
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
    bool updateConfigFromWavFile(WAVFile& wavFile) {
        mConfig.sampleRate = wavFile.getSampleRate();
        mConfig.channelCount = wavFile.getNumChannels();
        mConfig.format = wavFile.getAudioFormat();

        // Validate parameters
        if (mConfig.sampleRate <= 0) {
            logError("Invalid sample rate in WAV header");
            return false;
        }

        if (mConfig.channelCount <= 0 || mConfig.channelCount > 32) {
            logError("Invalid channel count in WAV header");
            return false;
        }

        if (mConfig.format == AUDIO_FORMAT_INVALID) {
            logError("Unsupported format in WAV header");
            return false;
        }

        printf("Parsed WAV header params: sampleRate:%d, channelCount:%d, format:%d\n", mConfig.sampleRate,
               mConfig.channelCount, mConfig.format);

        return true;
    }

    bool initializeAudioTrack(sp<AudioTrack>& audioTrack) {
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        if (bytesPerSample == 0) {
            logError("Invalid audio format specified");
            return false;
        }

        audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(mConfig.channelCount);

        if (mConfig.minFrameCount < static_cast<size_t>(mConfig.sampleRate / 1000) * 10) {
            mConfig.minFrameCount = static_cast<size_t>(mConfig.sampleRate / 1000) * 10;
            logWarning((std::string("Reset minFrameCount to ") + std::to_string(mConfig.minFrameCount)).c_str());
        }

        size_t frameCount = mConfig.minFrameCount * 2;

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.content_type = mConfig.contentType;
        attributes.usage = mConfig.usage;

        logInfo("Initializing AudioTrack");
        audioTrack = new AudioTrack();
        if (audioTrack->set(AUDIO_STREAM_DEFAULT, mConfig.sampleRate, mConfig.format, channelMask, frameCount,
                            mConfig.outputFlag, nullptr, nullptr, 0, nullptr, false, AUDIO_SESSION_ALLOCATE,
                            AudioTrack::TRANSFER_SYNC, nullptr, getuid(), getpid(), &attributes, false, 1.0f,
                            AUDIO_PORT_HANDLE_NONE) != NO_ERROR) {
            logError("Failed to set AudioTrack parameters");
            return false;
        }

        if (audioTrack->initCheck() != NO_ERROR) {
            logError("AudioTrack initialization check failed");
            return false;
        }

        return true;
    }

    bool startPlayback(const sp<AudioTrack>& audioTrack) {
        logInfo("Starting AudioTrack");
        status_t startResult = audioTrack->start();
        if (startResult != NO_ERROR) {
            logError((std::string("AudioTrack start failed with status ") + std::to_string(startResult)).c_str());
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
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        logInfo((std::string("Playback started. Playing audio from: ") + mConfig.playFilePath).c_str());

        uint32_t retryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;

        while (true) {
            bytesRead = wavFile.readData(buffer, bufferSize);
            if (bytesRead == 0) {
                logInfo("End of file reached");
                break;
            }

            // Write buffer to AudioTrack with retry logic
            bytesWritten = 0;
            retryCount = 0;
            while (static_cast<size_t>(bytesWritten) < bytesRead && retryCount < kMaxRetries) {
                ssize_t written =
                    audioTrack->write(buffer + bytesWritten, bytesRead - static_cast<size_t>(bytesWritten));
                if (written < 0) {
                    logWarning(
                        (std::string("AudioTrack write failed with error ") + std::to_string(written) + ", retrying...")
                            .c_str());
                    retryCount++;
                    usleep(kRetryDelayUs);
                    continue;
                }
                bytesWritten += written;
                retryCount = 0; // Reset retry count on successful write
            }

            if (retryCount >= kMaxRetries) {
                logError("AudioTrack write failed after maximum retries");
                break;
            }

            // Update total bytes played
            totalBytesPlayed += static_cast<uint32_t>(bytesRead);

            // Report progress
            if (totalBytesPlayed >= nextProgressReport) {
                float secondsPlayed = static_cast<float>(totalBytesPlayed) / bytesPerSecond;
                float mbPlayed = static_cast<float>(totalBytesPlayed) / (1024u * 1024u);
                printf("Playing ... , played %.2f seconds, %.2f MB\n", secondsPlayed, mbPlayed);
                nextProgressReport += bytesPerSecond * kProgressReportInterval;
            }
        }

        logInfo((std::string("Playback finished. Total bytes played: ") + std::to_string(totalBytesPlayed)).c_str());

        return 0;
    }
};

/************************** Audio Duplex Operation ******************************/
class AudioDuplexOperation : public AudioOperation {
private:
    static std::atomic<WAVFile*> g_wavFile;

public:
    explicit AudioDuplexOperation(const AudioConfig& config) : AudioOperation(config) {}

    int32_t execute() override {
        // Validate parameters
        if (!validateParameters()) {
            return -1;
        }

        // Initialize AudioRecord and AudioTrack
        sp<AudioRecord> audioRecord;
        sp<AudioTrack> audioTrack;
        if (!initializeAudioComponents(audioRecord, audioTrack)) {
            return -1;
        }

        // Setup WAV file
        WAVFile wavFile;
        std::string audioFilePath;
        if (!setupWavFile(wavFile, audioFilePath)) {
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
        int32_t result = duplexLoop(audioRecord, audioTrack, wavFile, audioFilePath);

        // Cleanup
        audioRecord->stop();
        audioTrack->stop();
        wavFile.finalize();

        return result;
    }

private:
    bool validateParameters() {
        if (mConfig.sampleRate <= 0) {
            logError("Invalid sample rate specified");
            return false;
        }

        if (mConfig.channelCount <= 0 || mConfig.channelCount > 32) {
            logError("Invalid channel count specified");
            return false;
        }

        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        if (bytesPerSample == 0) {
            logError("Invalid audio format specified");
            return false;
        }

        return true;
    }

    bool initializeAudioComponents(sp<AudioRecord>& audioRecord, sp<AudioTrack>& audioTrack) {
        if (mConfig.minFrameCount == 0) {
            mConfig.minFrameCount = (mConfig.sampleRate / 1000) * 20; // 20ms
        }

        audio_channel_mask_t channelMaskIn = audio_channel_in_mask_from_count(mConfig.channelCount);
        audio_channel_mask_t channelMaskOut = audio_channel_out_mask_from_count(mConfig.channelCount);

        // Get minimum frame count from AudioRecord
        if (AudioRecord::getMinFrameCount(&mConfig.minFrameCount, mConfig.sampleRate, mConfig.format, channelMaskIn) !=
            NO_ERROR) {
            logWarning("Cannot get min frame count, using default value");
        }

        size_t frameCount = mConfig.minFrameCount * 2;

        // Initialize AudioRecord
        if (!initializeAudioRecord(audioRecord, channelMaskIn, frameCount)) {
            return false;
        }

        // Initialize AudioTrack
        if (!initializeAudioTrack(audioTrack, channelMaskOut, frameCount)) {
            return false;
        }

        return true;
    }

    bool initializeAudioRecord(sp<AudioRecord>& audioRecord, audio_channel_mask_t channelMask, size_t frameCount) {
        AttributionSourceState attributionSource;
        attributionSource.packageName = std::string("Audio Test Client");
        attributionSource.token = sp<BBinder>::make();

        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.source = mConfig.inputSource;

        logInfo("Initializing AudioRecord");
        audioRecord = new AudioRecord(attributionSource);
        if (audioRecord->set(mConfig.inputSource, mConfig.sampleRate, mConfig.format, channelMask, frameCount, nullptr,
                             nullptr, 0, false, AUDIO_SESSION_ALLOCATE, AudioRecord::TRANSFER_SYNC, mConfig.inputFlag,
                             getuid(), getpid(), &attributes, AUDIO_PORT_HANDLE_NONE) != NO_ERROR) {
            logError("Failed to set AudioRecord parameters");
            return false;
        }

        if (audioRecord->initCheck() != NO_ERROR) {
            logError("AudioRecord initialization check failed");
            return false;
        }

        return true;
    }

    bool initializeAudioTrack(sp<AudioTrack>& audioTrack, audio_channel_mask_t channelMask, size_t frameCount) {
        audio_attributes_t attributes;
        memset(&attributes, 0, sizeof(attributes));
        attributes.content_type = mConfig.contentType;
        attributes.usage = mConfig.usage;

        logInfo("Initializing AudioTrack");
        audioTrack = new AudioTrack();
        if (audioTrack->set(AUDIO_STREAM_DEFAULT, mConfig.sampleRate, mConfig.format, channelMask, frameCount,
                            mConfig.outputFlag, nullptr, nullptr, 0, nullptr, false, AUDIO_SESSION_ALLOCATE,
                            AudioTrack::TRANSFER_SYNC, nullptr, getuid(), getpid(), &attributes, false, 1.0f,
                            AUDIO_PORT_HANDLE_NONE) != NO_ERROR) {
            logError("Failed to set AudioTrack parameters");
            return false;
        }

        if (audioTrack->initCheck() != NO_ERROR) {
            logError("AudioTrack initialization check failed");
            return false;
        }

        return true;
    }

    bool setupWavFile(WAVFile& wavFile, std::string& audioFilePath) {
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        audioFilePath = AudioUtils::makeRecordFilePath(mConfig.sampleRate, mConfig.channelCount, bytesPerSample * 8,
                                                       mConfig.recordFilePath);

        logInfo((std::string("Recording audio to file: ") + audioFilePath).c_str());

        if (!wavFile.createForWriting(audioFilePath, mConfig.sampleRate, mConfig.channelCount, bytesPerSample * 8)) {
            logError((std::string("Can't create output file ") + audioFilePath).c_str());
            return false;
        }

        return true;
    }

    void setupSignalHandler(WAVFile* wavFile) {
        g_wavFile.store(wavFile);
        signal(SIGINT, signalHandler);
    }

    bool startAudioComponents(const sp<AudioRecord>& audioRecord, const sp<AudioTrack>& audioTrack) {
        logInfo("Starting AudioRecord");
        status_t recordStartResult = audioRecord->start();
        if (recordStartResult != NO_ERROR) {
            logError(
                (std::string("AudioRecord start failed with status ") + std::to_string(recordStartResult)).c_str());
            return false;
        }

        logInfo("Starting AudioTrack");
        status_t playStartResult = audioTrack->start();
        if (playStartResult != NO_ERROR) {
            logError((std::string("AudioTrack start failed with status ") + std::to_string(playStartResult)).c_str());
            audioRecord->stop();
            return false;
        }

        return true;
    }

    int32_t duplexLoop(const sp<AudioRecord>& audioRecord,
                       const sp<AudioTrack>& audioTrack,
                       WAVFile& wavFile,
                       const std::string& audioFilePath) {
        ssize_t bytesRead = 0;
        ssize_t bytesWritten = 0;
        uint32_t totalBytesRead = 0;
        size_t bytesPerSample = audio_bytes_per_sample(mConfig.format);
        size_t bufferSize = (mConfig.minFrameCount * 2) * mConfig.channelCount * bytesPerSample;
        uint32_t bytesPerSecond = mConfig.sampleRate * mConfig.channelCount * bytesPerSample;

        // Setup buffer
        BufferManager bufferManager(bufferSize);
        if (!bufferManager.isValid()) {
            logError("Failed to create valid buffer manager");
            return -1;
        }
        char* buffer = bufferManager.get();

        if (mConfig.durationSeconds > 0) {
            logInfo((std::string("Duplex audio started. Recording for ") + std::to_string(mConfig.durationSeconds) +
                     " seconds...")
                        .c_str());
        } else {
            logInfo("Duplex audio started. Press Ctrl+C to stop.");
        }

        uint32_t recordRetryCount = 0;
        uint32_t playRetryCount = 0;
        uint32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
        uint32_t maxBytesToRecord =
            (mConfig.durationSeconds > 0)
                ? std::min(static_cast<uint32_t>(mConfig.durationSeconds) * bytesPerSecond, MAX_AUDIO_DATA_SIZE)
                : MAX_AUDIO_DATA_SIZE;

        logInfo((std::string("Set maxBytesToRecord to ") + std::to_string(maxBytesToRecord) + " bytes").c_str());

        bool recording = true;
        bool playing = true;

        while (recording && playing && totalBytesRead < maxBytesToRecord) {
            // Read from AudioRecord
            bytesRead = audioRecord->read(buffer, bufferSize);
            if (bytesRead < 0) {
                logWarning(
                    (std::string("AudioRecord read returned error ") + std::to_string(bytesRead) + ", retrying...")
                        .c_str());
                recordRetryCount++;
                if (recordRetryCount >= kMaxRetries) {
                    logError("AudioRecord read failed after maximum retries");
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

            // Write to WAV file
            size_t bytesWrittenToFile = wavFile.writeData(buffer, static_cast<size_t>(bytesRead));
            if (bytesWrittenToFile != static_cast<size_t>(bytesRead)) {
                logError("Failed to write to output file");
                recording = false;
                playing = false;
                break;
            }

            // Report progress
            if (totalBytesRead >= nextProgressReport) {
                float secondsRecorded = static_cast<float>(totalBytesRead) / bytesPerSecond;
                float mbRecorded = static_cast<float>(totalBytesRead) / (1024u * 1024u);
                printf("Recording ... , recorded %.2f seconds, %.2f MB\n", secondsRecorded, mbRecorded);
                nextProgressReport += bytesPerSecond * kProgressReportInterval;

                // Periodically update header to ensure file validity if recording is interrupted
                wavFile.updateHeader();
            }

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
                    logWarning(
                        (std::string("AudioTrack write failed with error ") + std::to_string(written) + ", retrying...")
                            .c_str());
                    playRetryCount++;
                    if (playRetryCount >= kMaxRetries) {
                        logError("AudioTrack write failed after maximum retries");
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

        logInfo((std::string("Duplex audio completed. Total bytes read: ") + std::to_string(totalBytesRead)).c_str());
        logInfo((std::string("Recording saved to: ") + audioFilePath).c_str());

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

    // Create and execute the appropriate audio operation
    std::unique_ptr<AudioOperation> operation;

    switch (mode) {
    case MODE_RECORD:
        printf("Running in RECORD mode\n");
        operation = std::make_unique<AudioRecordOperation>(config);
        break;

    case MODE_PLAY:
        printf("Running in PLAY mode\n");
        operation = std::make_unique<AudioPlayOperation>(config);
        break;

    case MODE_DUPLEX:
        printf("Running in DUPLEX mode (record and play simultaneously)\n");
        operation = std::make_unique<AudioDuplexOperation>(config);
        break;

    default:
        printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
        CommandLineParser::showHelp();
        return -1;
    }

    // Execute the audio operation
    return operation->execute();
}
