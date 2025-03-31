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
#include <iostream>
#include <chrono>
#include <ctime>
#include <utils/Log.h>
#include <media/AudioRecord.h>
#include <android/content/AttributionSourceState.h>

using namespace android;
using android::content::AttributionSourceState;

#define USE_WAV_HEADER
#define MAX_DATA_SIZE 1 * 1024 * 1024 * 1024

#ifdef USE_WAV_HEADER
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
     *
     * @throws None
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
     *
     * @throws None
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
    header.riffID[0] = 'R';
    header.riffID[1] = 'I';
    header.riffID[2] = 'F';
    header.riffID[3] = 'F';

    header.riffSize = 0;
    header.waveID[0] = 'W';
    header.waveID[1] = 'A';
    header.waveID[2] = 'V';
    header.waveID[3] = 'E';

    header.fmtID[0] = 'f';
    header.fmtID[1] = 'm';
    header.fmtID[2] = 't';
    header.fmtID[3] = ' ';

    header.fmtSize = 16;
    header.audioFormat = 1;
    header.numChannels = numChannels;
    header.sampleRate = sampleRate;

    header.byteRate = sampleRate * numChannels * bitsPerSample / 8;
    header.blockAlign = numChannels * bitsPerSample / 8;
    header.bitsPerSample = bitsPerSample;

    header.dataID[0] = 'd';
    header.dataID[1] = 'a';
    header.dataID[2] = 't';
    header.dataID[3] = 'a';
    header.dataSize = numSamples * numChannels * bitsPerSample / 8;

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
    if (in.is_open())
    {
        in.close();
    }
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
    uint32_t riff_size = 4 + (8 + 16) + (8 + data_chunk_size);
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
#endif // USE_WAV_HEADER

void get_format_time(char *format_time)
{
    time_t t = time(nullptr);
    struct tm *now = localtime(&t);
    strftime(format_time, 32, "%Y%m%d_%H.%M.%S", now);
    return;
}

/************************** AudioRecord function ******************************/
static void help()
{
    printf("-s{inputSource} -r{sampleRate} -c{channelount} -f{format} -F{inputFlag} -z{minFrameCount}\n");
    printf("    -s{0|1|2|3|4|5|6|7|8|9|10|1997|1998|1999} set audio source\n");
    printf("        0 = AUDIO_SOURCE_DEFAULT\n");
    printf("        1 = AUDIO_SOURCE_MIC\n");
    printf("        2 = AUDIO_SOURCE_VOICE_UPLINK\n");
    printf("        3 = AUDIO_SOURCE_VOICE_DOWNLINK\n");
    printf("        4 = AUDIO_SOURCE_VOICE_CALL\n");
    printf("        5 = AUDIO_SOURCE_CAMCORDER\n");
    printf("        6 = AUDIO_SOURCE_VOICE_RECOGNITION\n");
    printf("        7 = AUDIO_SOURCE_VOICE_COMMUNICATION\n");
    printf("        8 = AUDIO_SOURCE_REMOTE_SUBMIX\n");
    printf("        9 = AUDIO_SOURCE_UNPROCESSED\n");
    printf("        10 = AUDIO_SOURCE_VOICE_PERFORMANCE\n");
    printf("        1997 = AUDIO_SOURCE_ECHO_REFERENCE\n");
    printf("        1998 = AUDIO_SOURCE_FM_TUNER\n");
    printf("        1999 = AUDIO_SOURCE_HOTWORD\n");
    printf("    -r{8000|16000|32000|48000} set sample rate\n");
    printf("    -c{1|2|4|6|12|10|16} set channel count\n");
    printf("    -f{1|2|3|4|5|6} set audio format\n");
    printf("        1 = AUDIO_FORMAT_PCM_16_BIT\n");
    printf("        2 = AUDIO_FORMAT_PCM_8_BIT\n");
    printf("        3 = AUDIO_FORMAT_PCM_32_BIT\n");
    printf("        4 = AUDIO_FORMAT_PCM_8_24_BIT\n");
    printf("        5 = AUDIO_FORMAT_PCM_FLOAT\n");
    printf("        6 = AUDIO_FORMAT_PCM_24_BIT_PACKED\n");
    printf("    -F{0|1|2|4|8|16|32|64|128} set audio input flag\n");
    printf("        0 = AUDIO_INPUT_FLAG_NONE\n");
    printf("        1 = AUDIO_INPUT_FLAG_FAST\n");
    printf("        2 = AUDIO_INPUT_FLAG_HW_HOTWORD\n");
    printf("        4 = AUDIO_INPUT_FLAG_RAW\n");
    printf("        8 = AUDIO_INPUT_FLAG_SYNC\n");
    printf("        16 = AUDIO_INPUT_FLAG_MMAP_NOIRQ\n");
    printf("        32 = AUDIO_INPUT_FLAG_VOIP_TX\n");
    printf("        64 = AUDIO_INPUT_FLAG_HW_AV_SYNC\n");
    printf("        128 = AUDIO_INPUT_FLAG_DIRECT\n");
    printf("    -z{minFrameCount} set min frame count. eg: 960\n");
}

int32_t main(int32_t argc, char **argv)
{
    audio_source_t inputSource = AUDIO_SOURCE_HOTWORD;            // default audio source
    int32_t sampleRate = 48000;                                   // default sample rate
    int32_t channelCount = 1;                                     // default channel count
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;              // default format
    audio_input_flags_t inputFlag = AUDIO_INPUT_FLAG_NONE;        // default input flag
    size_t minFrameCount = (sampleRate / 1000) * 10;              // default frame count(20ms)
    std::string audioFilePath = "/data/record_48k_1ch_16bit.raw"; // default audio file path

    /* check input params */
    int32_t opt = 0;
    while ((opt = getopt(argc, argv, "s:r:f:c:F:z:h")) != -1)
    {
        switch (opt)
        {
        case 's': // audio source
            inputSource = static_cast<audio_source_t>(atoi(optarg));
            break;
        case 'r': // sample rate
            sampleRate = atoi(optarg);
            break;
        case 'c': // channel count
            channelCount = atoi(optarg);
            break;
        case 'f': // format
            format = static_cast<audio_format_t>(atoi(optarg));
            break;
        case 'F': // input flag
            inputFlag = static_cast<audio_input_flags_t>(atoi(optarg));
            break;
        case 'z': // min frame count
            minFrameCount = atoi(optarg);
            break;
        case 'h': // help for use
            help();
            return 0;
        default:
            help();
            return -1;
        }
    }

    /************** set audio parameters **************/
    audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(channelCount);
    size_t bytesPerSample = audio_bytes_per_sample(format);
    // size_t bytesPerFrame = audio_bytes_per_frame(channelCount, format);

    /************** get min frame count **************/
    if (AudioRecord::getMinFrameCount(&minFrameCount, sampleRate, format, channelMask) == NO_ERROR)
    {
        printf("AudioRecord::getMinFrameCount: minFrameCount = %zu\n", minFrameCount);
    }
    else
    {
        printf("Error: cannot compute min frame count\n");
    }

    AttributionSourceState attributionSource;
    attributionSource.packageName = std::string("AudioRecord test");
    attributionSource.token = sp<BBinder>::make();

    audio_attributes_t attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.source = inputSource;
    size_t frameCount = minFrameCount * 2;

    printf("AudioRecord Params: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
           inputSource, sampleRate, format, channelCount, frameCount, inputFlag);
    ALOGD("AudioRecord Params: inputSource:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, inputFlag:%d\n",
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
        printf("set AudioRecord params failed\n");
        return -1;
    }

    /************** AudioRecord init check **************/
    if (audioRecord->initCheck() != NO_ERROR)
    {
        printf("AudioRecord init check failed\n");
        return -1;
    }

    /************** set audio file path **************/
    char audioFile[256] = {0};
    char formatTime[32] = {0};
    get_format_time(formatTime);
#ifdef USE_WAV_HEADER
    snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%zubit_%s.wav", sampleRate, channelCount, bytesPerSample * 8, formatTime);
#else
    snprintf(audioFile, sizeof(audioFile), "/data/record_%dHz_%dch_%zubit_%s.raw", sampleRate, channelCount, bytesPerSample * 8, formatTime);
#endif
    audioFilePath = std::string(audioFile);
    printf("record audio file: %s\n", audioFilePath.c_str());

    /************** open output file **************/
    std::ofstream outFile(audioFilePath, std::ios::binary | std::ios::out);
    if (!outFile.is_open() || outFile.fail())
    {
        printf("can't open output file");
        return -1;
    }

#ifdef USE_WAV_HEADER
    /************** write audio file header **************/
    int32_t numSamples = 0;
    if (!writeWAVHeader(outFile, numSamples, sampleRate, channelCount, bytesPerSample * 8))
    {
        printf("writeWAVHeader failed\n");
        return -1;
    }
#endif

    /************** AudioRecord start **************/
    printf("AudioRecord start\n");
    if (audioRecord->start() != NO_ERROR)
    {
        printf("AudioRecord start failed\n");
        return -1;
    }

    int32_t bytesRead = 0;
    int32_t totalBytesRead = 0;
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    char *buffer = new char[bufferSize];
    while (true)
    {
        bytesRead = audioRecord->read(buffer, bufferSize);
        if (bytesRead <= 0)
        {
            printf("AudioRecord read failed, bytesRead = %d\n", bytesRead);
            break;
        }
        // printf("AudioRecord read %d bytes\n", bytesRead);
        outFile.write(buffer, bytesRead);
        totalBytesRead += bytesRead;
#ifdef USE_WAV_HEADER
        UpdateSizes(outFile, totalBytesRead); // update RIFF chunk size and data chunk size
#endif
        if (totalBytesRead >= MAX_DATA_SIZE)
        {
            printf("AudioRecord data size exceeds limit: %d MB\n", MAX_DATA_SIZE / (1024 * 1024));
            break;
        }
    }

    /************** AudioRecord stop **************/
    printf("AudioRecord stop\n");
    audioRecord->stop();
    if (outFile.is_open())
    {
        outFile.close();
    }
    if (buffer != nullptr)
    {
        delete[] buffer;
    }

    return 0;
}
