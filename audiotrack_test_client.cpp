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
#include <utils/Log.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>

using namespace android;

#define USE_WAV_HEADER

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

/************************** AudioTrack function ******************************/
static void help()
{
#ifdef USE_WAV_HEADER
    printf("-u{usage} -C{contentType} -F{outputFlag} -z{minFrameCount}\n");
#else
    printf("-u{usage} -C{contentType} -r{sampleRate} -c{channelCount} -f{format} -F{outputFlag} -z{minFrameCount}\n");
#endif
    printf("    -u{0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16|17|1000|1001|1002|1003} set audio usage\n");
    printf("        0 = AUDIO_USAGE_UNKNOWN\n");
    printf("        1 = AUDIO_USAGE_MEDIA\n");
    printf("        2 = AUDIO_USAGE_VOICE_COMMUNICATION\n");
    printf("        3 = AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING\n");
    printf("        4 = AUDIO_USAGE_ALARM\n");
    printf("        5 = AUDIO_USAGE_NOTIFICATION\n");
    printf("        6 = AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE\n");
    printf("        7 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST\n");
    printf("        8 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT\n");
    printf("        9 = AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED\n");
    printf("        10 = AUDIO_USAGE_NOTIFICATION_EVENT\n");
    printf("        11 = AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY\n");
    printf("        12 = AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE\n");
    printf("        13 = AUDIO_USAGE_ASSISTANCE_SONIFICATION\n");
    printf("        14 = AUDIO_USAGE_GAME\n");
    printf("        15 = AUDIO_USAGE_VIRTUAL_SOURCE\n");
    printf("        16 = AUDIO_USAGE_ASSISTANT\n");
    printf("        17 = AUDIO_USAGE_CALL_ASSISTANT\n");
    printf("        1000 = AUDIO_USAGE_EMERGENCY\n");
    printf("        1001 = AUDIO_USAGE_SAFETY\n");
    printf("        1002 = AUDIO_USAGE_VEHICLE_STATUS\n");
    printf("        1003 = AUDIO_USAGE_ANNOUNCEMENT\n");
    printf("    -C{0|1|2|3|4} set content type\n");
    printf("        0 = AUDIO_CONTENT_TYPE_UNKNOWN\n");
    printf("        1 = AUDIO_CONTENT_TYPE_SPEECH\n");
    printf("        2 = AUDIO_CONTENT_TYPE_MUSIC\n");
    printf("        3 = AUDIO_CONTENT_TYPE_MOVIE\n");
    printf("        4 = AUDIO_CONTENT_TYPE_SONIFICATION\n");
#ifndef USE_WAV_HEADER
    printf("    -r{8000|16000|32000|48000} set sample rate\n");
    printf("    -c{1|2|4|6|8|16} set channel count\n");
    printf("    -f{1|2|3|4|5|6} set format\n");
    printf("        1 = AUDIO_FORMAT_PCM_16_BIT\n");
    printf("        2 = AUDIO_FORMAT_PCM_8_BIT\n");
    printf("        3 = AUDIO_FORMAT_PCM_32_BIT\n");
    printf("        4 = AUDIO_FORMAT_PCM_8_24_BIT\n");
    printf("        5 = AUDIO_FORMAT_PCM_FLOAT\n");
    printf("        6 = AUDIO_FORMAT_PCM_24_BIT_PACKED\n");
#endif
    printf("    -F{0|1|2|4|8|16|32|64|128|256|512|1024|8192|16384|32768} set audio output flag\n");
    printf("        0 = AUDIO_OUTPUT_FLAG_NONE\n");
    printf("        1 = AUDIO_OUTPUT_FLAG_DIRECT\n");
    printf("        2 = AUDIO_OUTPUT_FLAG_PRIMARY\n");
    printf("        4 = AUDIO_OUTPUT_FLAG_FAST\n");
    printf("        8 = AUDIO_OUTPUT_FLAG_DEEP_BUFFER\n");
    printf("        16 = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD\n");
    printf("        32 = AUDIO_OUTPUT_FLAG_NON_BLOCKING\n");
    printf("        64 = AUDIO_OUTPUT_FLAG_HW_AV_SYNC\n");
    printf("        128 = AUDIO_OUTPUT_FLAG_TTS\n");
    printf("        256 = AUDIO_OUTPUT_FLAG_RAW\n");
    printf("        512 = AUDIO_OUTPUT_FLAG_SYNC\n");
    printf("        1024 = AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO\n");
    printf("        8192 = AUDIO_OUTPUT_FLAG_DIRECT_PCM\n");
    printf("        16384 = AUDIO_OUTPUT_FLAG_MMAP_NOIRQ\n");
    printf("        32768 = AUDIO_OUTPUT_FLAG_VOIP_RX\n");
    printf("    -z{minFrameCount} set min frame count. eg: 960\n");
}

int32_t main(int32_t argc, char **argv)
{
    audio_usage_t usage = AUDIO_USAGE_MEDIA;                       // default audio usage
    audio_content_type_t contentType = AUDIO_CONTENT_TYPE_UNKNOWN; // default content type
    int32_t sampleRate = 48000;                                    // default sample rate
    int32_t channelCount = 2;                                      // default channel count
    audio_format_t format = AUDIO_FORMAT_PCM_16_BIT;               // default format
    audio_output_flags_t outputFlag = AUDIO_OUTPUT_FLAG_NONE;      // default output flag
    size_t minFrameCount = (sampleRate / 1000) * 10;               // default frame count(20ms)
#ifdef USE_WAV_HEADER
    std::string audioFilePath = "/data/audio_test.wav"; // default audio file path
#else
    std::string audioFilePath = "/data/48k_2ch_16bit.raw"; // default audio file path
#endif

    /************** parse input params **************/
    int32_t opt = 0;
#ifdef USE_WAV_HEADER
    while ((opt = getopt(argc, argv, "u:C:F:z:h")) != -1)
#else
    while ((opt = getopt(argc, argv, "u:C:r:c:f:F:z:h")) != -1)
#endif
    {
        switch (opt)
        {
        case 'u': // audio usage
            usage = static_cast<audio_usage_t>(atoi(optarg));
            break;
        case 'C': // audio content type
            contentType = static_cast<audio_content_type_t>(atoi(optarg));
            break;
#ifndef USE_WAV_HEADER
        case 'r': // sample rate
            sampleRate = atoi(optarg);
            break;
        case 'c': // channel count
            channelCount = atoi(optarg);
            break;
        case 'f': // format
            format = static_cast<audio_format_t>(atoi(optarg));
            break;
#endif
        case 'F': // output flag
            outputFlag = static_cast<audio_output_flags_t>(atoi(optarg));
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

    /* get audio file path */
    if (optind < argc)
    {
        audioFilePath = std::string{argv[optind]};
    }

    if (access(audioFilePath.c_str(), F_OK) == -1)
    {
        /* file not exist */
        printf("file %s not exist\n", audioFilePath.c_str());
        return -1;
    }

#ifdef USE_WAV_HEADER
    /* read wav header */
    WAVHeader header;
    if (!readWAVHeader(audioFilePath, header))
    {
        printf("readWAVHeader error\n");
        return -1;
    }
    // header.print();

    sampleRate = header.sampleRate;
    channelCount = header.numChannels;
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
        printf("unsupported format and bitsPerSample\n");
        return -1;
    }
    printf("wav header params: sampleRate:%d, channelCount:%d, format:%d\n", sampleRate, channelCount, format);
#endif // USE_WAV_HEADER

    size_t bytesPerSample = audio_bytes_per_sample(format);
    // size_t bytesPerFrame = audio_bytes_per_frame(channelCount, format);
    audio_channel_mask_t channelMask = audio_channel_out_mask_from_count(channelCount);

    /* set min frame count */
    if (minFrameCount < (size_t)(sampleRate / 1000) * 10)
    {
        minFrameCount = (size_t)(sampleRate / 1000) * 10;
        printf("reset minFrameCount: %zu\n", minFrameCount);
    }

    /* get min frame count */
    // if (AudioTrack::getMinFrameCount(&minFrameCount, AUDIO_STREAM_MUSIC, sampleRate) == NO_ERROR)
    // {
    //     printf("AudioTrack::getMinFrameCount: minFrameCount = %zu\n", minFrameCount);
    // }
    // else
    // {
    //     printf("Error: cannot compute min frame count\n");
    // }

    audio_attributes_t attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.content_type = contentType;
    attributes.usage = usage;
    size_t frameCount = minFrameCount * 2;

    printf("AudioTrack Params: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
           usage, sampleRate, format, channelCount, frameCount, outputFlag);
    ALOGD("AudioTrack Params: usage:%d, sampleRate:%d, format:%d, channelCount:%d, frameCount:%zu, outputFlag:%d\n",
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
        printf("set audioTrack params failed\n");
        return -1;
    }

    /************** params check **************/
    if (audioTrack->initCheck() != NO_ERROR)
    {
        printf("AudioTrack init check failed\n");
        return -1;
    }

    // audio_session_t sessionId = audioTrack->getSessionId();
    // audio_port_handle_t dev_id = audioTrack->getOutputDevice();
    // if (audioTrack->setOutputDevice(dev_id) != NO_ERROR)
    // {
    //     printf("AudioTrack::setOutputDevice() failed!\n");
    //     return -1;
    // }

    /************** open audio file for playback **************/
    std::ifstream inputFile(audioFilePath, std::ios::binary | std::ios::in);
    if (!inputFile.is_open() || !inputFile.good())
    {
        printf("can't open audio file");
        return -1;
    }

    /************** audioTrack start **************/
    printf("AudioTrack start\n");
    if (audioTrack->start() != NO_ERROR)
    {
        printf("AudioTrack start failed\n");
        return -1;
    }

    int32_t bytesRead = 0;
    int32_t bytesWritten = 0;
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    char *buffer = new char[bufferSize];
#ifdef USE_WAV_HEADER
    inputFile.seekg(sizeof(WAVHeader), std::ios::beg);
#endif // USE_WAV_HEADER
    while (true)
    {
        inputFile.read(buffer, bufferSize);
        bytesRead = inputFile.gcount();
        if (inputFile.eof())
        {
            break;
        }

        bytesWritten = 0;
        while (bytesWritten < bytesRead)
        {
            int32_t written = audioTrack->write(buffer + bytesWritten, bytesRead - bytesWritten);
            if (written < 0)
            {
                printf("AudioTrack write failed: %d\n", written);
                usleep(5 * 1000);
                break;
            }
            bytesWritten += written;
        }
    }

    /************** audioTrack stop **************/
    printf("AudioTrack stop\n");
    audioTrack->stop();
    if (inputFile.is_open())
    {
        inputFile.close();
    }
    if (buffer != nullptr)
    {
        delete[] buffer;
    }

    return 0;
}
