#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <typeinfo>
#include <fstream>
#include <string>
#include <time.h>
#include <vector>
#include <memory>
#include <utils/Log.h>
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>
#include <android/content/AttributionSourceState.h>

using namespace android;
using android::content::AttributionSourceState;

#define MAX_DATA_SIZE 1 * 1024 * 1024 * 1024 // 1GB

/************************** WAV header function ******************************/
struct WAVHeader
{
    // WAV header size is 44 bytes
    // format = RIFF + fmt + data
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

    /**
     * Writes the WAV file header to the specified output stream.
     *
     * @param out The output stream to write the header to.
     */
    void write(std::ofstream &out)
    {
        out.write(riffID, 4);                 // "RIFF"
        out.write((char *)&riffSize, 4);      // 36 + (numSamples * numChannels * bytesPerSample)
        out.write(waveID, 4);                 // "WAVE"
        out.write(fmtID, 4);                  // "fmt "
        out.write((char *)&fmtSize, 4);       // 16 for PCM; 18 for IEEE float
        out.write((char *)&audioFormat, 2);   // 1 for PCM; 3 for IEEE float
        out.write((char *)&numChannels, 2);   // 1 for mono; 2 for stereo
        out.write((char *)&sampleRate, 4);    // sample rate
        out.write((char *)&byteRate, 4);      // sampleRate * numChannels * bytesPerSample
        out.write((char *)&blockAlign, 2);    // numChannels * bytesPerSample
        out.write((char *)&bitsPerSample, 2); // 8 for 8-bit; 16 for 16-bit; 32 for 32-bit
        out.write(dataID, 4);                 // "data"
        out.write((char *)&dataSize, 4);      // numSamples * numChannels * bytesPerSample
    }

    /**
     * Reads the WAV file header from the specified input stream.
     *
     * @param in The input stream to read the header from.
     */
    void read(std::ifstream &in)
    {
        in.read(riffID, 4);                 // "RIFF"
        in.read((char *)&riffSize, 4);      // 36 + (numSamples * numChannels * bytesPerSample)
        in.read(waveID, 4);                 // "WAVE"
        in.read(fmtID, 4);                  // "fmt "
        in.read((char *)&fmtSize, 4);       // 16 for PCM; 18 for IEEE float
        in.read((char *)&audioFormat, 2);   // 1 for PCM; 3 for IEEE float
        in.read((char *)&numChannels, 2);   // 1 for mono; 2 for stereo
        in.read((char *)&sampleRate, 4);    // sample rate
        in.read((char *)&byteRate, 4);      // sampleRate * numChannels * bytesPerSample
        in.read((char *)&blockAlign, 2);    // numChannels * bytesPerSample
        in.read((char *)&bitsPerSample, 2); // 8 for 8-bit; 16 for 16-bit; 32 for 32-bit
        in.read(dataID, 4);                 // "data"
        in.read((char *)&dataSize, 4);      // numSamples * numChannels * bytesPerSample
    }

    /** Prints the WAV file header. */
    void print()
    {
        printf("RiffID: %s\n", riffID);
        printf("RiffSize: %d\n", riffSize);
        printf("WaveID: %s\n", waveID);
        printf("FmtID: %s\n", fmtID);
        printf("FmtSize: %d\n", fmtSize);
        printf("AudioFormat: %d\n", audioFormat);
        printf("NumChannels: %d\n", numChannels);
        printf("SampleRate: %d\n", sampleRate);
        printf("ByteRate: %d\n", byteRate);
        printf("BlockAlign: %d\n", blockAlign);
        printf("BitsPerSample: %d\n", bitsPerSample);
        printf("DataID: %s\n", dataID);
        printf("DataSize: %d\n", dataSize);
    }
};

/**
 * Writes the WAV file header to the specified output stream.
 *
 * @param outFile The output stream to write the header to.
 * @param numSamples The number of samples in the audio data.
 * @param sampleRate The sample rate of the audio data.
 * @param numChannels The number of channels in the audio data.
 * @param bitsPerSample The number of bits per sample in the audio data.
 *
 * @return True if the header was written successfully, false otherwise.
 */
bool writeWAVHeader(std::ofstream &outFile, uint32_t numSamples, uint32_t sampleRate, uint32_t numChannels, uint32_t bitsPerSample)
{
    if (!outFile.is_open())
    {
        return false;
    }

    WAVHeader header;

    // RIFF chunk
    header.riffID[0] = 'R';
    header.riffID[1] = 'I';
    header.riffID[2] = 'F';
    header.riffID[3] = 'F';

    uint32_t bytesPerSample = bitsPerSample / 8;
    header.riffSize = 36 + numSamples * numChannels * bytesPerSample; // 36 = header size after RIFF
    header.waveID[0] = 'W';
    header.waveID[1] = 'A';
    header.waveID[2] = 'V';
    header.waveID[3] = 'E';

    // fmt subchunk
    header.fmtID[0] = 'f';
    header.fmtID[1] = 'm';
    header.fmtID[2] = 't';
    header.fmtID[3] = ' ';

    header.fmtSize = 16;
    header.audioFormat = 1; // PCM
    header.numChannels = numChannels;
    header.sampleRate = sampleRate;

    header.byteRate = sampleRate * numChannels * bytesPerSample;
    header.blockAlign = numChannels * bytesPerSample;
    header.bitsPerSample = bitsPerSample;

    // data subchunk
    header.dataID[0] = 'd';
    header.dataID[1] = 'a';
    header.dataID[2] = 't';
    header.dataID[3] = 'a';
    header.dataSize = numSamples * numChannels * bytesPerSample;

    header.write(outFile);
    // header.print();

    return true;
}

/**
 * Reads the WAV file header from the specified input stream.
 *
 * @param inFile The input stream to read the header from.
 * @param header The WAVHeader object to store the header in.
 *
 * @return True if the header was read successfully, false otherwise.
 */
bool readWAVHeader(const std::string &filename, WAVHeader &header)
{
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    if (!in.is_open())
    {
        return false;
    }
    header.read(in);
    in.close();
    return true;
}

/**
 * Updates the sizes of the RIFF and data chunks in the WAV file.
 *
 * @param outfile The output stream to write the updated header to.
 * @param data_chunk_size The size of the data chunk in bytes.
 */
void UpdateSizes(std::ofstream &outfile, uint32_t data_chunk_size)
{
    // record current position
    std::streampos current_position = outfile.tellp();

    // calculates RIFF chunk size and data chunk size
    uint32_t riff_size = 36 + data_chunk_size;
    uint32_t data_size = data_chunk_size;

    // move to riff size field and update
    outfile.seekp(4, std::ios::beg);
    outfile.write(reinterpret_cast<const char *>(&riff_size), sizeof(riff_size));

    // move to data size field and update
    outfile.seekp(40, std::ios::beg);
    outfile.write(reinterpret_cast<const char *>(&data_size), sizeof(data_size));

    // return to record position
    outfile.seekp(current_position);
}

void get_format_time(char *format_time)
{
    time_t t = time(nullptr);
    struct tm *now = localtime(&t);
    strftime(format_time, 32, "%Y%m%d_%H.%M.%S", now);
}

/************************** Audio Mode Definitions ******************************/
enum AudioMode
{
    MODE_INVALID = -1, // invalid mode
    MODE_RECORD = 0,   // record only
    MODE_PLAY = 1,     // play only
    MODE_DUPLEX = 2    // record and play simultaneously
};

/************************** Help function ******************************/
static void help()
{
    printf("Audio Test Client - Combined Record and Play Demo\n");
    printf("Usage: audio_test_client -m{mode} [options] [audio_file]\n");
    printf("Modes:\n");
    printf("  -m0   Record mode\n");
    printf("  -m1   Play mode\n");
    printf("  -m2   Duplex mode (record and play simultaneously)\n\n");

    printf("Record Options:\n");
    printf("  -s{inputSource}     Set audio source\n");
    printf("  -r{sampleRate}      Set sample rate\n");
    printf("  -c{channelCount}    Set channel count\n");
    printf("  -f{format}          Set audio format\n");
    printf("  -F{inputFlag}       Set audio input flag\n");
    printf("  -z{minFrameCount}   Set min frame count\n\n");

    printf("Play Options:\n");
    printf("  -u{usage}           Set audio usage\n");
    printf("  -C{contentType}     Set content type\n");
    printf("  -O{outputFlag}      Set audio output flag\n");
    printf("  -z{minFrameCount}   Set min frame count\n\n");

    printf("General Options:\n");
    printf("  -h                  Show this help message\n\n");

    printf("AudioRecord Source Options(-s{inputSource}):\n");
    printf("    0 = AUDIO_SOURCE_DEFAULT\n");
    printf("    1 = AUDIO_SOURCE_MIC\n");
    printf("    2 = AUDIO_SOURCE_VOICE_UPLINK\n");
    printf("    3 = AUDIO_SOURCE_VOICE_DOWNLINK\n");
    printf("    4 = AUDIO_SOURCE_VOICE_CALL\n");
    printf("    5 = AUDIO_SOURCE_CAMCORDER\n");
    printf("    6 = AUDIO_SOURCE_VOICE_RECOGNITION\n");
    printf("    7 = AUDIO_SOURCE_VOICE_COMMUNICATION\n");
    printf("    8 = AUDIO_SOURCE_REMOTE_SUBMIX\n");
    printf("    9 = AUDIO_SOURCE_UNPROCESSED\n");
    printf("    10 = AUDIO_SOURCE_VOICE_PERFORMANCE\n");
    printf("    1997 = AUDIO_SOURCE_ECHO_REFERENCE\n");
    printf("    1998 = AUDIO_SOURCE_FM_TUNER\n");
    printf("    1999 = AUDIO_SOURCE_HOTWORD\n\n");

    printf("AudioRecord SampleRate Options(-r{sampleRate}):\n");
    printf("    8000, 16000, 32000, 48000\n\n");

    printf("AudioRecord Channel Count Options(-c{channelCount}):\n");
    printf("    1, 2, 4, 6, 8, 12, 16\n\n");

    printf("AudioRecord Format Options(-f{format}):\n");
    printf("    1 = AUDIO_FORMAT_PCM_16_BIT\n");
    printf("    2 = AUDIO_FORMAT_PCM_8_BIT\n");
    printf("    3 = AUDIO_FORMAT_PCM_32_BIT\n");
    printf("    4 = AUDIO_FORMAT_PCM_8_24_BIT\n");
    printf("    5 = AUDIO_FORMAT_PCM_FLOAT\n");
    printf("    6 = AUDIO_FORMAT_PCM_24_BIT_PACKED\n\n");

    printf("AudioRecord Input Flag Options(-F{inputFlag}):\n");
    printf("    0 = AUDIO_INPUT_FLAG_NONE\n");
    printf("    1 = AUDIO_INPUT_FLAG_FAST\n");
    printf("    2 = AUDIO_INPUT_FLAG_HW_HOTWORD\n");
    printf("    4 = AUDIO_INPUT_FLAG_RAW\n");
    printf("    8 = AUDIO_INPUT_FLAG_SYNC\n");
    printf("    16 = AUDIO_INPUT_FLAG_MMAP_NOIRQ\n");
    printf("    32 = AUDIO_INPUT_FLAG_VOIP_TX\n");
    printf("    64 = AUDIO_INPUT_FLAG_HW_AV_SYNC\n");
    printf("    128 = AUDIO_INPUT_FLAG_DIRECT\n");
    printf("    256 = AUDIO_INPUT_FLAG_DIRECT_PROCESSED\n");
    printf("    512 = AUDIO_INPUT_FLAG_VOICE_COMMUNICATION\n");
    printf("    1024 = AUDIO_INPUT_FLAG_AEC_CONVERGED\n");
    printf("    2048 = AUDIO_INPUT_FLAG_NO_PERSISTENT_CACHE\n");
    printf("    4096 = AUDIO_INPUT_FLAG_HW_HOTWORD_CONTINUOUS\n");
    printf("    8192 = AUDIO_INPUT_FLAG_BUILTIN_FAR_FIELD_MIC\n\n");

    printf("Audio min frameCount Options(-z{minFrameCount}):\n");
    printf("    480, 960, 1920\n\n");

    printf("AudioTrack Usage Options(-u{usage}):\n");
    printf("    0 = AUDIO_USAGE_UNKNOWN\n");
    printf("    1 = AUDIO_USAGE_MEDIA\n");
    printf("    2 = AUDIO_USAGE_VOICE_COMMUNICATION\n");
    printf("    3 = AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING\n");
    printf("    4 = AUDIO_USAGE_ALARM\n");
    printf("    5 = AUDIO_USAGE_NOTIFICATION\n");
    printf("    6 = AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE\n");
    printf("    7 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST\n");
    printf("    8 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT\n");
    printf("    9 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED\n");
    printf("    10 = AUDIO_USAGE_NOTIFICATION_EVENT\n");
    printf("    11 = AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY\n");
    printf("    12 = AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE\n");
    printf("    13 = AUDIO_USAGE_ASSISTANCE_SONIFICATION\n");
    printf("    14 = AUDIO_USAGE_GAME\n");
    printf("    15 = AUDIO_USAGE_VIRTUAL_SOURCE\n");
    printf("    16 = AUDIO_USAGE_ASSISTANT\n");
    printf("    17 = AUDIO_USAGE_CALL_ASSISTANT\n");
    printf("    1000 = AUDIO_USAGE_EMERGENCY\n");
    printf("    1001 = AUDIO_USAGE_SAFETY\n");
    printf("    1002 = AUDIO_USAGE_VEHICLE_STATUS\n");
    printf("    1003 = AUDIO_USAGE_ANNOUNCEMENT\n\n");

    printf("AudioTrack Content Type Options(-C{contentType}):\n");
    printf("    0 = AUDIO_CONTENT_TYPE_UNKNOWN\n");
    printf("    1 = AUDIO_CONTENT_TYPE_SPEECH\n");
    printf("    2 = AUDIO_CONTENT_TYPE_MUSIC\n");
    printf("    3 = AUDIO_CONTENT_TYPE_MOVIE\n");
    printf("    4 = AUDIO_CONTENT_TYPE_SONIFICATION\n\n");

    printf("AudioTrack Output Flag Options(-O{outputFlag}):\n");
    printf("    0 = AUDIO_OUTPUT_FLAG_NONE\n");
    printf("    1 = AUDIO_OUTPUT_FLAG_DIRECT\n");
    printf("    2 = AUDIO_OUTPUT_FLAG_PRIMARY\n");
    printf("    4 = AUDIO_OUTPUT_FLAG_FAST\n");
    printf("    8 = AUDIO_OUTPUT_FLAG_DEEP_BUFFER\n");
    printf("    16 = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD\n");
    printf("    32 = AUDIO_OUTPUT_FLAG_NON_BLOCKING\n");
    printf("    64 = AUDIO_OUTPUT_FLAG_HW_AV_SYNC\n");
    printf("    128 = AUDIO_OUTPUT_FLAG_TTS\n");
    printf("    256 = AUDIO_OUTPUT_FLAG_RAW\n");
    printf("    512 = AUDIO_OUTPUT_FLAG_SYNC\n");
    printf("    1024 = AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO\n");
    printf("    2048 = AUDIO_OUTPUT_FLAG_LOW_LATENCY\n");
    printf("    4096 = AUDIO_OUTPUT_FLAG_DEEP_BUFFER_COMPRESS_OFFLOAD\n");
    printf("    8192 = AUDIO_OUTPUT_FLAG_DIRECT_PCM\n");
    printf("    16384 = AUDIO_OUTPUT_FLAG_MMAP_NOIRQ\n");
    printf("    32768 = AUDIO_OUTPUT_FLAG_VOIP_RX\n");
    printf("    65536 = AUDIO_OUTPUT_FLAG_LOW_LATENCY_VOIP\n\n");

    printf("Examples:\n");
    printf("    Record: audio_test_client -m0 -s1 -r48000 -c2 -f1 -F1 -z480\n");
    printf("    Play:   audio_test_client -m1 -u5 -C0 -O4 -z480 /data/audio_test.wav\n");
    printf("    Duplex: audio_test_client -m2 -s1 -r48000 -c2 -f1 -F1 -u5 -C0 -O4 -z480\n");
}

// RAII wrapper for buffer management
class BufferManager
{
private:
    std::unique_ptr<char[]> buffer;
    size_t size;

public:
    BufferManager(size_t bufferSize) : size(bufferSize)
    {
        buffer = std::make_unique<char[]>(bufferSize);
    }

    ~BufferManager() = default;

    char *get() { return buffer.get(); }
    size_t getSize() const { return size; }
};

// RAII wrapper for file management (output)
class OutputFileManager
{
private:
    std::ofstream outFile;
    std::string filePath;

public:
    OutputFileManager(const std::string &path) : filePath(path)
    {
        outFile.open(filePath, std::ios::binary | std::ios::out);
    }

    ~OutputFileManager()
    {
        if (outFile.is_open())
        {
            outFile.close();
        }
    }

    bool isOpen() const { return outFile.is_open(); }
    std::ofstream &getStream() { return outFile; }
    const std::string &getPath() const { return filePath; }
};

// RAII wrapper for file management (input)
class InputFileManager
{
private:
    std::ifstream inFile;
    std::string filePath;

public:
    InputFileManager(const std::string &path) : filePath(path)
    {
        inFile.open(filePath, std::ios::binary | std::ios::in);
    }

    ~InputFileManager()
    {
        if (inFile.is_open())
        {
            inFile.close();
        }
    }

    bool isOpen() const { return inFile.is_open(); }
    std::ifstream &getStream() { return inFile; }
    const std::string &getPath() const { return filePath; }
};

/************************** Audio Record Function ******************************/
int32_t recordAudio(
    audio_source_t inputSource,
    int32_t sampleRate,
    int32_t channelCount,
    audio_format_t format,
    audio_input_flags_t inputFlag,
    size_t minFrameCount,
    const std::string &recordFile)
{
    /************** set audio parameters **************/
    // Validate sample rate
    if (sampleRate <= 0)
    {
        printf("Error: Invalid sample rate specified: %d\n", sampleRate);
        return -1;
    }

    // Validate channel count
    if (channelCount <= 0 || channelCount > 32)
    {
        printf("Error: Invalid channel count specified: %d\n", channelCount);
        return -1;
    }

    audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(channelCount);
    size_t bytesPerSample = audio_bytes_per_sample(format);
    if (bytesPerSample == 0)
    {
        printf("Error: Invalid audio format specified\n");
        return -1;
    }

    /************** get min frame count **************/
    if (minFrameCount == 0)
    {
        // Calculate default frame count (20ms)
        minFrameCount = (sampleRate / 1000) * 20;
    }

    if (AudioRecord::getMinFrameCount(&minFrameCount, sampleRate, format, channelMask) != NO_ERROR)
    {
        printf("Warning: cannot get min frame count, using default minFrameCount: %zu\n", minFrameCount);
    }
    else
    {
        printf("AudioRecord::getMinFrameCount: minFrameCount: %zu\n", minFrameCount);
    }

    AttributionSourceState attributionSource;
    attributionSource.packageName = std::string("Audio Test Client");
    attributionSource.token = sp<BBinder>::make();

    audio_attributes_t attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.source = inputSource;
    size_t frameCount = minFrameCount * 2;

    printf("AudioRecord Params to set: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
           inputSource, sampleRate, format, channelCount, frameCount, inputFlag);
    ALOGD("AudioRecord Params to set: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
          inputSource, sampleRate, format, channelCount, frameCount, inputFlag);

    /************** set AudioRecord params **************/
    printf("AudioRecord init\n");
    sp<AudioRecord> audioRecord = new AudioRecord(attributionSource);
    if (audioRecord->set(
            AUDIO_SOURCE_DEFAULT,       // source
            sampleRate,                 // sampleRate
            format,                     // format
            channelMask,                // channelMask
            frameCount,                 // frameCount
            nullptr,                    // callback
            nullptr,                    // user mCallbackData
            0,                          // notificationFrames
            false,                      // threadCanCallJava
            AUDIO_SESSION_ALLOCATE,     // sessionId
            AudioRecord::TRANSFER_SYNC, // transferType
            inputFlag,                  // audioInputFlags
            getuid(),                   // uid
            getpid(),                   // pid
            &attributes,                // pAttributes
            AUDIO_PORT_HANDLE_NONE      // selectedDeviceId
            ) != NO_ERROR)
    {
        printf("Error: set AudioRecord params failed\n");
        return -1;
    }

    /************** AudioRecord init check **************/
    if (audioRecord->initCheck() != NO_ERROR)
    {
        printf("Error: AudioRecord init check failed\n");
        return -1;
    }

    /************** set audio file path **************/
    char audioFile[256] = {0};
    char formatTime[32] = {0};
    get_format_time(formatTime);
    snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%zubit_%s.wav", sampleRate, channelCount, bytesPerSample * 8, formatTime);
    std::string audioFilePath = std::string(audioFile);
    if (!recordFile.empty())
    {
        audioFilePath = recordFile;
    }
    printf("Recording audio to file: %s\n", audioFilePath.c_str());

    /************** open output file **************/
    OutputFileManager fileManager(audioFilePath);
    if (!fileManager.isOpen() || fileManager.getStream().fail())
    {
        printf("Error: can't open output file %s\n", audioFilePath.c_str());
        return -1;
    }

    /************** write audio file header **************/
    int32_t numSamples = 0;
    if (!writeWAVHeader(fileManager.getStream(), numSamples, sampleRate, channelCount, bytesPerSample * 8))
    {
        printf("Error: writeWAVHeader failed\n");
        return -1;
    }

    /************** AudioRecord start **************/
    printf("AudioRecord start\n");
    status_t startResult = audioRecord->start();
    if (startResult != NO_ERROR)
    {
        printf("Error: AudioRecord start failed with status %d\n", startResult);
        return -1;
    }

    /************** Audio recording loop **************/
    int32_t bytesRead = 0;
    int32_t totalBytesRead = 0;
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    int32_t bytesPerSecond = sampleRate * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000; // 1ms
    const int32_t kMaxRetries = 3;
    const int32_t kProgressReportInterval = 10; // seconds

    // Use RAII for buffer management
    BufferManager bufferManager(bufferSize);
    char *buffer = bufferManager.get();

    printf("Recording started. Press Ctrl+C to stop.\n");

    int32_t retryCount = 0;
    int32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
    while (true)
    {
        bytesRead = audioRecord->read(buffer, bufferSize);
        if (bytesRead < 0)
        {
            printf("Warning: AudioRecord read returned error %d, retry %d/%d\n", bytesRead, retryCount + 1, kMaxRetries);
            retryCount++;
            if (retryCount >= kMaxRetries)
            {
                printf("Error: AudioRecord read failed after %d retries\n", kMaxRetries);
                break; // Error occurred
            }
            // Use a shorter delay to reduce blocking
            usleep(kRetryDelayUs);
            continue;
        }
        else if (bytesRead == 0)
        {
            // No data available, but not an error
            printf("Warning: AudioRecord read returned 0 bytes\n");
            // Use a shorter delay to reduce blocking
            usleep(kRetryDelayUs);
            continue;
        }
        else
        {
            // Successfully read data, reset retry count
            retryCount = 0;
        }

        // bytesRead > 0 at this point
        fileManager.getStream().write(buffer, bytesRead);
        if (fileManager.getStream().fail())
        {
            printf("Error: Failed to write to output file\n");
            break;
        }

        totalBytesRead += bytesRead;
        UpdateSizes(fileManager.getStream(), totalBytesRead); // update RIFF chunk size and data chunk size

        // Print progress every 10 seconds
        if (totalBytesRead >= nextProgressReport)
        {
            printf("Recording progress, recorded %d seconds, %d MB\n", totalBytesRead / bytesPerSecond, totalBytesRead / (1024 * 1024));
            nextProgressReport += bytesPerSecond * kProgressReportInterval;
        }

        if (totalBytesRead >= MAX_DATA_SIZE)
        {
            printf("Warning: AudioRecord data size exceeds limit: %d MB\n", MAX_DATA_SIZE / (1024 * 1024));
            break;
        }
    }

    /************** AudioRecord stop **************/
    printf("AudioRecord stop, total bytes recorded: %d\n", totalBytesRead);
    audioRecord->stop();

    // Ensure file is properly closed through RAII
    printf("Recording finished. Audio file: %s\n", audioFilePath.c_str());

    return 0;
}

/************************** Audio Play Function ******************************/
int32_t playAudio(
    audio_usage_t usage,
    audio_content_type_t contentType,
    audio_output_flags_t outputFlag,
    size_t minFrameCount,
    const std::string &playFile)
{
    // Variables to store audio parameters read from WAV file
    int32_t sampleRate;
    int32_t channelCount;
    audio_format_t format;
    // Validate input file
    if (playFile.empty())
    {
        printf("Error: Audio file path is required\n");
        return -1;
    }

    if (access(playFile.c_str(), F_OK) == -1)
    {
        /* file not exist */
        printf("Error: File %s does not exist\n", playFile.c_str());
        return -1;
    }

    /* read wav header */
    WAVHeader header;
    if (!readWAVHeader(playFile, header))
    {
        printf("Error: Failed to read WAV header\n");
        return -1;
    }
    // header.print();

    sampleRate = header.sampleRate;
    channelCount = header.numChannels;

    // Validate sample rate
    if (sampleRate <= 0)
    {
        printf("Error: Invalid sample rate in WAV header: %d\n", sampleRate);
        return -1;
    }

    // Validate channel count
    if (channelCount <= 0 || channelCount > 32)
    {
        printf("Error: Invalid channel count in WAV header: %d\n", channelCount);
        return -1;
    }

    // Determine format based on WAV header
    if ((header.audioFormat == 1) && (header.bitsPerSample == 8))
    {
        format = AUDIO_FORMAT_PCM_8_BIT;
    }
    else if ((header.audioFormat == 1) && (header.bitsPerSample == 16))
    {
        format = AUDIO_FORMAT_PCM_16_BIT;
    }
    else if ((header.audioFormat == 1) && (header.bitsPerSample == 24))
    {
        format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
    }
    else if ((header.audioFormat == 1) && (header.bitsPerSample == 32))
    {
        format = AUDIO_FORMAT_PCM_32_BIT;
    }
    else if ((header.audioFormat == 3) && (header.bitsPerSample == 32))
    {
        format = AUDIO_FORMAT_PCM_FLOAT;
    }
    else
    {
        printf("Error: Unsupported format and bitsPerSample in WAV header: audioFormat=%d, bitsPerSample=%d\n",
               header.audioFormat, header.bitsPerSample);
        return -1;
    }
    printf("Parsed WAV header params: sampleRate:%d, channelCount:%d, format:%d\n", sampleRate, channelCount, format);

    size_t bytesPerSample = audio_bytes_per_sample(format);
    if (bytesPerSample == 0)
    {
        printf("Error: Invalid audio format specified\n");
        return -1;
    }

    // size_t bytesPerFrame = audio_bytes_per_frame(channelCount, format);
    audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(channelCount);

    /* set min frame count */
    if (minFrameCount == 0)
    {
        // Calculate default frame count (20ms)
        minFrameCount = (sampleRate / 1000) * 20;
    }
    if (minFrameCount < (size_t)(sampleRate / 1000) * 10)
    {
        minFrameCount = (size_t)(sampleRate / 1000) * 10;
        printf("Reset minFrameCount: %zu\n", minFrameCount);
    }

    // if (AudioTrack::getMinFrameCount(&minFrameCount, AUDIO_STREAM_MUSIC, sampleRate) != NO_ERROR)
    // {
    //     printf("Warning: cannot get min frame count, using default minFrameCount: %zu\n", minFrameCount);
    // }
    // else
    // {
    //     printf("AudioTrack::getMinFrameCount: minFrameCount: %zu\n", minFrameCount);
    // }

    audio_attributes_t attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.content_type = contentType;
    attributes.usage = usage;
    size_t frameCount = minFrameCount * 2;

    printf("AudioTrack Params to set: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
           usage, sampleRate, format, channelCount, frameCount, outputFlag);
    ALOGD("AudioTrack Params to set: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
          usage, sampleRate, format, channelCount, frameCount, outputFlag);

    /************** AudioTrack init **************/
    printf("AudioTrack init\n");
    sp<AudioTrack> audioTrack = new AudioTrack();
    if (audioTrack->set(
            AUDIO_STREAM_DEFAULT,      // streamType
            sampleRate,                // sampleRate
            format,                    // format
            channelMask,               // channelMask
            frameCount,                // frameCount
            outputFlag,                // outputFlag
            nullptr,                   // callback, use TRANSFER_CALLBACK
            nullptr,                   // user mCallbackData
            0,                         // notificationFrames
            nullptr,                   // sharedBuffer, use TRANSFER_SHARED
            false,                     // threadCanCallJava
            AUDIO_SESSION_ALLOCATE,    // sessionId
            AudioTrack::TRANSFER_SYNC, // transferType
            nullptr,                   // offloadInfo
            getuid(),                  // uid
            getpid(),                  // pid
            &attributes,               // pAttributes
            false,                     // doNotReconnect
            1.0f,                      // maxRequiredSpeed
            AUDIO_PORT_HANDLE_NONE     // selectedDeviceId
            ) != NO_ERROR)
    {
        printf("Error: Failed to set AudioTrack parameters\n");
        return -1;
    }

    /************** params check **************/
    if (audioTrack->initCheck() != NO_ERROR)
    {
        printf("Error: AudioTrack init check failed\n");
        return -1;
    }

    /************** open audio file for playback **************/
    InputFileManager fileManager(playFile);
    if (!fileManager.isOpen() || !fileManager.getStream().good())
    {
        printf("Error: Can't open audio file %s\n", playFile.c_str());
        return -1;
    }

    /************** audioTrack start **************/
    printf("AudioTrack start\n");
    status_t startResult = audioTrack->start();
    if (startResult != NO_ERROR)
    {
        printf("Error: AudioTrack start failed with status %d\n", startResult);
        return -1;
    }

    /************** audio playback loop **************/
    int32_t bytesRead = 0;
    int32_t bytesWritten = 0;
    int32_t totalBytesPlayed = 0;
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    int32_t bytesPerSecond = sampleRate * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000; // 1ms
    const int32_t kMaxRetries = 3;
    const int32_t kProgressReportInterval = 10; // seconds

    // Use RAII for buffer management
    BufferManager bufferManager(bufferSize);
    char *buffer = bufferManager.get();

    // Skip WAV header for playback
    fileManager.getStream().seekg(sizeof(WAVHeader), std::ios::beg);

    printf("Playback started. Playing audio from: %s\n", playFile.c_str());

    int32_t retryCount = 0;
    int32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
    while (true)
    {
        fileManager.getStream().read(buffer, bufferSize);
        bytesRead = fileManager.getStream().gcount();
        if (fileManager.getStream().eof() || bytesRead <= 0)
        {
            printf("End of file reached or read error\n");
            break;
        }

        bytesWritten = 0;
        retryCount = 0; // Reset retry count for each new buffer write
        while (bytesWritten < bytesRead && retryCount < kMaxRetries)
        {
            int32_t written = audioTrack->write(buffer + bytesWritten, bytesRead - bytesWritten);
            if (written < 0)
            {
                printf("Warning: AudioTrack write failed with error %d, retry %d/%d\n", written, retryCount + 1, kMaxRetries);
                retryCount++;
                usleep(kRetryDelayUs); // 1ms delay before retry
                continue;
            }
            bytesWritten += written;
            retryCount = 0; // Reset retry count on successful write
        }

        if (retryCount >= kMaxRetries)
        {
            printf("Error: AudioTrack write failed after %d retries\n", kMaxRetries);
            break;
        }

        totalBytesPlayed += bytesRead;
        // Print progress every 10 seconds
        if (totalBytesPlayed >= nextProgressReport)
        {
            printf("Playing progress, played %d seconds, %d MB\n", totalBytesPlayed / bytesPerSecond, totalBytesPlayed / (1024 * 1024));
            nextProgressReport += bytesPerSecond * kProgressReportInterval;
        }
    }

    /************** audioTrack stop **************/
    printf("AudioTrack stop, total bytes played: %d\n", totalBytesPlayed);
    audioTrack->stop();

    // Ensure file is properly closed through RAII
    printf("Playing finished. Audio file: %s\n", playFile.c_str());

    return 0;
}

/************************** Audio Duplex Function ******************************/
int32_t duplexAudio(
    audio_source_t inputSource,
    int32_t sampleRate,
    int32_t channelCount,
    audio_format_t format,
    audio_input_flags_t inputFlag,
    audio_usage_t usage,
    audio_content_type_t contentType,
    audio_output_flags_t outputFlag,
    size_t minFrameCount,
    const std::string &recordFile)
{
    /************** set audio parameters **************/
    // Validate sample rate
    if (sampleRate <= 0)
    {
        printf("Error: Invalid sample rate specified: %d\n", sampleRate);
        return -1;
    }

    // Validate channel count
    if (channelCount <= 0 || channelCount > 32)
    {
        printf("Error: Invalid channel count specified: %d\n", channelCount);
        return -1;
    }

    audio_channel_mask_t channelMaskIn = audio_channel_in_mask_from_count(channelCount);
    audio_channel_mask_t channelMaskOut = audio_channel_out_mask_from_count(channelCount);
    size_t bytesPerSample = audio_bytes_per_sample(format);

    // Validate parameters
    if (bytesPerSample == 0)
    {
        printf("Error: Invalid audio format specified\n");
        return -1;
    }

    /************** get min frame count **************/
    if (minFrameCount == 0)
    {
        // Calculate default frame count (20ms)
        minFrameCount = (sampleRate / 1000) * 20;
    }

    if (AudioRecord::getMinFrameCount(&minFrameCount, sampleRate, format, channelMaskIn) != NO_ERROR)
    {
        printf("Warning: cannot get min frame count, using default minFrameCount: %zu\n", minFrameCount);
    }
    else
    {
        printf("AudioRecord::getMinFrameCount: minFrameCount: %zu\n", minFrameCount);
    }

    // For duplex, we might want to use the same frame count for both
    size_t recordFrameCount = minFrameCount * 2;
    size_t playFrameCount = minFrameCount * 2;

    AttributionSourceState attributionSource;
    attributionSource.packageName = std::string("Audio Test Client");
    attributionSource.token = sp<BBinder>::make();

    audio_attributes_t recordAttributes;
    memset(&recordAttributes, 0, sizeof(recordAttributes));
    recordAttributes.source = inputSource;

    printf("AudioRecord Params to set: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
           inputSource, sampleRate, format, channelCount, recordFrameCount, inputFlag);
    ALOGD("AudioRecord Params to set: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
          inputSource, sampleRate, format, channelCount, recordFrameCount, inputFlag);

    /************** set AudioRecord params **************/
    printf("AudioRecord init\n");
    sp<AudioRecord> audioRecord = new AudioRecord(attributionSource);
    if (audioRecord->set(
            AUDIO_SOURCE_DEFAULT,       // source
            sampleRate,                 // sampleRate
            format,                     // format
            channelMaskIn,              // channelMask
            recordFrameCount,           // frameCount
            nullptr,                    // callback
            nullptr,                    // user mCallbackData
            0,                          // notificationFrames
            false,                      // threadCanCallJava
            AUDIO_SESSION_ALLOCATE,     // sessionId
            AudioRecord::TRANSFER_SYNC, // transferType
            inputFlag,                  // audioInputFlags
            getuid(),                   // uid
            getpid(),                   // pid
            &recordAttributes,          // pAttributes
            AUDIO_PORT_HANDLE_NONE      // selectedDeviceId
            ) != NO_ERROR)
    {
        printf("Error: set AudioRecord params failed\n");
        return -1;
    }

    /************** AudioRecord init check **************/
    if (audioRecord->initCheck() != NO_ERROR)
    {
        printf("Error: AudioRecord init check failed\n");
        return -1;
    }

    audio_attributes_t playAttributes;
    memset(&playAttributes, 0, sizeof(playAttributes));
    playAttributes.content_type = contentType;
    playAttributes.usage = usage;

    printf("AudioTrack Params to set: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
           usage, sampleRate, format, channelCount, playFrameCount, outputFlag);
    ALOGD("AudioTrack Params to set: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
          usage, sampleRate, format, channelCount, playFrameCount, outputFlag);

    /************** AudioTrack init **************/
    printf("AudioTrack init\n");
    sp<AudioTrack> audioTrack = new AudioTrack();
    if (audioTrack->set(
            AUDIO_STREAM_DEFAULT,      // streamType
            sampleRate,                // sampleRate
            format,                    // format
            channelMaskOut,            // channelMask
            playFrameCount,            // frameCount
            outputFlag,                // outputFlag
            nullptr,                   // callback, use TRANSFER_CALLBACK
            nullptr,                   // user mCallbackData
            0,                         // notificationFrames
            nullptr,                   // sharedBuffer, use TRANSFER_SHARED
            false,                     // threadCanCallJava
            AUDIO_SESSION_ALLOCATE,    // sessionId
            AudioTrack::TRANSFER_SYNC, // transferType
            nullptr,                   // offloadInfo
            getuid(),                  // uid
            getpid(),                  // pid
            &playAttributes,           // pAttributes
            false,                     // doNotReconnect
            1.0f,                      // maxRequiredSpeed
            AUDIO_PORT_HANDLE_NONE     // selectedDeviceId
            ) != NO_ERROR)
    {
        printf("Error: Failed to set AudioTrack parameters\n");
        return -1;
    }

    /************** AudioTrack init check **************/
    if (audioTrack->initCheck() != NO_ERROR)
    {
        printf("Error: AudioTrack init check failed\n");
        return -1;
    }

    /************** set audio file path **************/
    char audioFile[256] = {0};
    char formatTime[32] = {0};
    get_format_time(formatTime);
    snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%zubit_%s.wav", sampleRate, channelCount, bytesPerSample * 8, formatTime);
    std::string audioFilePath = std::string(audioFile);
    if (!recordFile.empty())
    {
        audioFilePath = recordFile;
    }
    printf("Recording audio to file: %s\n", audioFilePath.c_str());

    /************** open output file **************/
    OutputFileManager fileManager(audioFilePath);
    if (!fileManager.isOpen() || fileManager.getStream().fail())
    {
        printf("Error: can't open output file %s\n", audioFilePath.c_str());
        return -1;
    }

    /************** write audio file header **************/
    int32_t numSamples = 0;
    if (!writeWAVHeader(fileManager.getStream(), numSamples, sampleRate, channelCount, bytesPerSample * 8))
    {
        printf("Error: writeWAVHeader failed\n");
        return -1;
    }

    /************** start AudioRecord and AudioTrack **************/
    printf("AudioRecord start\n");
    status_t recordStartResult = audioRecord->start();
    if (recordStartResult != NO_ERROR)
    {
        printf("Error: AudioRecord start failed with status %d\n", recordStartResult);
        return -1;
    }

    printf("AudioTrack start\n");
    status_t playStartResult = audioTrack->start();
    if (playStartResult != NO_ERROR)
    {
        printf("Error: AudioTrack start failed with status %d\n", playStartResult);
        audioRecord->stop();
        return -1;
    }

    /************** duplex audio loop **************/
    int32_t bytesRead = 0;
    int32_t bytesWritten = 0;
    int32_t totalBytesRead = 0;
    size_t bufferSize = recordFrameCount * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000; // 1ms
    const int32_t maxRetries = 3;

    // Use RAII for buffer management
    BufferManager recordBufferManager(bufferSize);
    char *recordBuffer = recordBufferManager.get();

    printf("Duplex audio started. Press Ctrl+C to stop.\n");

    int32_t recordRetryCount = 0;
    int32_t playRetryCount = 0;
    bool recording = true;
    bool playing = true;
    while (recording && playing)
    {
        // Record audio
        if (recording)
        {
            bytesRead = audioRecord->read(recordBuffer, bufferSize);
            if (bytesRead < 0)
            {
                printf("Warning: AudioRecord read returned error %d, retry %d/%d\n", bytesRead, recordRetryCount + 1, maxRetries);
                recordRetryCount++;
                if (recordRetryCount >= maxRetries)
                {
                    printf("Error: AudioRecord read failed after %d retries\n", maxRetries);
                    recording = false;
                    break;
                }
                usleep(kRetryDelayUs); // wait before retry
                continue;
            }
            else if (bytesRead == 0)
            {
                // No data available, but not an error
                printf("Warning: AudioRecord read returned 0 bytes\n");
                usleep(kRetryDelayUs); // wait before retry
                continue;
            }
            else
            {
                // Successfully read data, reset retry count
                recordRetryCount = 0;

                // Write to file
                fileManager.getStream().write(recordBuffer, bytesRead);
                if (fileManager.getStream().fail())
                {
                    printf("Error: Failed to write to output file\n");
                    recording = false;
                    playing = false;
                    break;
                }

                totalBytesRead += bytesRead;
                UpdateSizes(fileManager.getStream(), totalBytesRead); // update RIFF chunk size and data chunk size

                if (totalBytesRead >= MAX_DATA_SIZE)
                {
                    printf("Warning: AudioRecord data size exceeds limit: %d MB\n", MAX_DATA_SIZE / (1024 * 1024));
                    recording = false;
                    playing = false;
                    break;
                }

                // Play the recorded data
                bytesWritten = 0;
                playRetryCount = 0;
                while (bytesWritten < bytesRead && playing)
                {
                    int32_t written = audioTrack->write(recordBuffer + bytesWritten, bytesRead - bytesWritten);
                    if (written < 0)
                    {
                        printf("Warning: AudioTrack write failed with error %d, retry %d/%d\n", written, playRetryCount + 1, maxRetries);
                        playRetryCount++;
                        if (playRetryCount >= maxRetries)
                        {
                            printf("Error: AudioTrack write failed after %d retries\n", maxRetries);
                            playing = false;
                            break;
                        }
                        usleep(kRetryDelayUs); // wait before retry
                        continue;
                    }
                    bytesWritten += written;
                    playRetryCount = 0; // Reset retry count on successful write
                }

                // Check if play failed
                if (!playing)
                {
                    recording = false;
                    break;
                }
            }
        }
    }

    /************** stop AudioRecord and AudioTrack **************/
    printf("AudioRecord stop\n");
    audioRecord->stop();

    printf("AudioTrack stop\n");
    audioTrack->stop();

    printf("Total bytes read: %d\n", totalBytesRead);
    printf("Duplex audio completed. Recording saved to: %s\n", audioFilePath.c_str());

    return 0;
}

/************************** Main function ******************************/
int32_t main(int32_t argc, char **argv)
{
    // Default parameters
    AudioMode mode = MODE_INVALID; // default mode is invalid

    // Record parameters
    audio_source_t inputSource = AUDIO_SOURCE_HOTWORD;     // default audio source
    int32_t recordSampleRate = 48000;                      // default sample rate
    int32_t recordChannelCount = 1;                        // default channel count
    audio_format_t recordFormat = AUDIO_FORMAT_PCM_16_BIT; // default format
    audio_input_flags_t inputFlag = AUDIO_INPUT_FLAG_NONE; // default input flag
    size_t recordMinFrameCount = 0;                        // will be calculated
    // 根据传入参数和当前时间自动生成带时间戳的录音文件，如果在命令行参数后指定录音文件，则不会自动生成。
    // 不可在此指定录音文件，要么在命令行参数后指定，要么不指定，自动生成带时间戳的录音文件。
    std::string recordFilePath = "";

    // Play parameters
    audio_usage_t usage = AUDIO_USAGE_MEDIA;                       // default audio usage
    audio_content_type_t contentType = AUDIO_CONTENT_TYPE_UNKNOWN; // default content type
    audio_output_flags_t outputFlag = AUDIO_OUTPUT_FLAG_NONE;      // default output flag
    size_t playMinFrameCount = 0;                                  // will be calculated
    // 如果没有在命令行参数后指定播放文件，则使用此默认的音频文件。
    std::string playFilePath = "/data/audio_test.wav"; // default audio file path

    /************** parse input params **************/
    int32_t opt = 0;
    while ((opt = getopt(argc, argv, "m:s:r:c:f:F:u:C:O:z:h")) != -1)
    {
        switch (opt)
        {
        case 'm': // mode
            mode = static_cast<AudioMode>(atoi(optarg));
            break;
        case 's': // audio source
            inputSource = static_cast<audio_source_t>(atoi(optarg));
            break;
        case 'r': // sample rate
            recordSampleRate = atoi(optarg);
            break;
        case 'c': // channel count
            recordChannelCount = atoi(optarg);
            break;
        case 'f': // format
            recordFormat = static_cast<audio_format_t>(atoi(optarg));
            break;
        case 'F': // input flag
            inputFlag = static_cast<audio_input_flags_t>(atoi(optarg));
            break;
        case 'u': // audio usage
            usage = static_cast<audio_usage_t>(atoi(optarg));
            break;
        case 'C': // audio content type
            contentType = static_cast<audio_content_type_t>(atoi(optarg));
            break;
        case 'O': // output flag
            outputFlag = static_cast<audio_output_flags_t>(atoi(optarg));
            break;
        case 'z': // min frame count
            recordMinFrameCount = atoi(optarg);
            playMinFrameCount = recordMinFrameCount; // use same min frame count for play if not specified separately
            break;
        case 'h': // help for use
            help();
            return 0;
        default:
            help();
            return -1;
        }
    }

    /* get audio file path */
    if (optind < argc)
    {
        std::string tmpFile = std::string{argv[optind]};
        if (mode == MODE_PLAY)
        {
            playFilePath = tmpFile;
        }
        else
        {
            recordFilePath = tmpFile;
        }
    }

    switch (mode)
    {
    case MODE_RECORD:
        printf("Running in RECORD mode\n");
        return recordAudio(inputSource, recordSampleRate, recordChannelCount, recordFormat,
                           inputFlag, recordMinFrameCount, recordFilePath);

    case MODE_PLAY:
        printf("Running in PLAY mode\n");
        return playAudio(usage, contentType, outputFlag, playMinFrameCount, playFilePath);

    case MODE_DUPLEX:
        printf("Running in DUPLEX mode (record and play simultaneously)\n");
        return duplexAudio(inputSource, recordSampleRate, recordChannelCount, recordFormat,
                           inputFlag, usage, contentType, outputFlag,
                           recordMinFrameCount, recordFilePath);

    default:
        printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
        help();
        return -1;
    }

    return 0;
}
