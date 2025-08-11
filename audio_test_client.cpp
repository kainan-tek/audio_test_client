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
#include <signal.h>
#include <utils/Log.h>
#include <media/AudioRecord.h>
#include <media/AudioTrack.h>
#include <media/AudioSystem.h>
#include <android/content/AttributionSourceState.h>

using namespace android;
using android::content::AttributionSourceState;

// max data size for recording
#define MAX_DATA_SIZE 1024 * 1024 * 1024 // 1GB

class WAVFile;       // Forward declaration
class BufferManager; // Forward declaration

// Global pointer to WAVFile for signal handling
static WAVFile *g_wavFile = nullptr;

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

int32_t recordAudio(
    audio_source_t inputSource,
    int32_t sampleRate,
    int32_t channelCount,
    audio_format_t format,
    audio_input_flags_t inputFlag,
    size_t minFrameCount,
    const std::string &recordFile);

int32_t playAudio(
    audio_usage_t usage,
    audio_content_type_t contentType,
    audio_output_flags_t outputFlag,
    size_t minFrameCount,
    const std::string &playFile);

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
    const std::string &recordFile);

void signalHandler(int signal);
void get_format_time(char *format_time);

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

// Signal handler for SIGINT (Ctrl+C)
void signalHandler(int signal)
{
    if (signal == SIGINT)
    {
        printf("\nReceived SIGINT (Ctrl+C), finalizing recording...\n");
        if (g_wavFile != nullptr)
        {
            // g_wavFile->updateHeader(); // Update header before exit
            g_wavFile->finalize(); // Finalize the WAV file
            g_wavFile = nullptr;   // Clear the pointer
        }
        exit(0);
    }
}

void get_format_time(char *format_time)
{
    time_t t = time(nullptr);
    struct tm *now = localtime(&t);
    strftime(format_time, 32, "%Y%m%d_%H.%M.%S", now);
}

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

    /************** create WAV file **************/
    WAVFile wavFile;
    if (!wavFile.createForWriting(audioFilePath, sampleRate, channelCount, bytesPerSample * 8))
    {
        printf("Error: can't create output file %s\n", audioFilePath.c_str());
        return -1;
    }

    // Set global WAV file pointer for signal handling
    g_wavFile = &wavFile;

    // Register signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, signalHandler);

    /************** AudioRecord start **************/
    printf("AudioRecord start\n");
    status_t startResult = audioRecord->start();
    if (startResult != NO_ERROR)
    {
        printf("Error: AudioRecord start failed with status %d\n", startResult);
        wavFile.close(); // Close file without finalizing
        return -1;
    }

    /************** Audio recording loop **************/
    int32_t bytesRead = 0;      // bytes read from AudioRecord
    int32_t totalBytesRead = 0; // total bytes read from AudioRecord
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    int32_t bytesPerSecond = sampleRate * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000;         // 1ms delay for retry
    const int32_t kMaxRetries = 3;              // max retries
    const int32_t kProgressReportInterval = 10; // report progress every 10 seconds

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
        size_t bytesWritten = wavFile.writeData(buffer, bytesRead);
        if (bytesWritten != static_cast<size_t>(bytesRead))
        {
            printf("Error: Failed to write to output file\n");
            break;
        }

        // Update total bytes read
        totalBytesRead += bytesRead;

        // Periodically update the WAV header to ensure file validity
        static int32_t headerUpdateCount = 0;
        headerUpdateCount++;
        if (headerUpdateCount >= 100)
        { // Update header every 100 iterations
            wavFile.updateHeader();
            headerUpdateCount = 0;
        }

        // Print progress every 10 seconds
        if (totalBytesRead >= nextProgressReport)
        {
            printf("Recording progress: recorded %d seconds, %d MB\n", totalBytesRead / bytesPerSecond, totalBytesRead / (1024 * 1024));
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

    // Finalize and close WAV file
    wavFile.finalize();
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

    /************** open WAV file for reading **************/
    WAVFile wavFile;
    if (!wavFile.openForReading(playFile))
    {
        printf("Error: Failed to open WAV file %s\n", playFile.c_str());
        return -1;
    }

    // Get audio parameters from WAV file
    int32_t sampleRate = wavFile.getSampleRate();
    int32_t channelCount = wavFile.getNumChannels();
    audio_format_t format = wavFile.getAudioFormat();

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

    // Validate format
    if (format == AUDIO_FORMAT_INVALID)
    {
        printf("Error: Unsupported format in WAV header\n");
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

    /************** AudioTrack init check **************/
    if (audioTrack->initCheck() != NO_ERROR)
    {
        printf("Error: AudioTrack init check failed\n");
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
    int32_t bytesRead = 0;        // Number of bytes read from file
    int32_t bytesWritten = 0;     // Number of bytes written to AudioTrack
    int32_t totalBytesPlayed = 0; // Total bytes played by AudioTrack
    size_t bufferSize = frameCount * channelCount * bytesPerSample;
    int32_t bytesPerSecond = sampleRate * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000;         // 1ms retry delay
    const int32_t kMaxRetries = 3;              // max retries
    const int32_t kProgressReportInterval = 10; // report progress every 10 seconds

    // Use RAII for buffer management
    BufferManager bufferManager(bufferSize);
    char *buffer = bufferManager.get();

    printf("Playback started. Playing audio from: %s\n", playFile.c_str());

    int32_t retryCount = 0;
    int32_t nextProgressReport = bytesPerSecond * kProgressReportInterval;
    while (true)
    {
        bytesRead = wavFile.readData(buffer, bufferSize);
        if (bytesRead <= 0)
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
            printf("Playing progress: played %d seconds, %d MB\n", totalBytesPlayed / bytesPerSecond, totalBytesPlayed / (1024 * 1024));
            nextProgressReport += bytesPerSecond * kProgressReportInterval;
        }
    }

    /************** audioTrack stop **************/
    printf("AudioTrack stop, total bytes played: %d\n", totalBytesPlayed);
    audioTrack->stop();

    // Close WAV file
    wavFile.close();
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

    /************** create WAV file **************/
    WAVFile wavFile;
    if (!wavFile.createForWriting(audioFilePath, sampleRate, channelCount, bytesPerSample * 8))
    {
        printf("Error: can't create output file %s\n", audioFilePath.c_str());
        return -1;
    }

    // Set global WAV file pointer for signal handling
    g_wavFile = &wavFile;

    // Register signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, signalHandler);

    /************** start AudioRecord and AudioTrack **************/
    printf("AudioRecord start\n");
    status_t recordStartResult = audioRecord->start();
    if (recordStartResult != NO_ERROR)
    {
        printf("Error: AudioRecord start failed with status %d\n", recordStartResult);
        wavFile.close(); // Close file without finalizing
        return -1;
    }

    printf("AudioTrack start\n");
    status_t playStartResult = audioTrack->start();
    if (playStartResult != NO_ERROR)
    {
        printf("Error: AudioTrack start failed with status %d\n", playStartResult);
        audioRecord->stop();
        wavFile.close(); // Close file without finalizing
        return -1;
    }

    /************** duplex audio loop **************/
    int32_t bytesRead = 0;      // Number of bytes read from AudioRecord
    int32_t bytesWritten = 0;   // Number of bytes written to AudioTrack
    int32_t totalBytesRead = 0; // Total number of bytes read from AudioRecord
    size_t bufferSize = recordFrameCount * channelCount * bytesPerSample;
    const int32_t kRetryDelayUs = 1000; // 1ms retry delay
    const int32_t kMaxRetries = 3;      // max retries

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
        bytesRead = audioRecord->read(recordBuffer, bufferSize);
        if (bytesRead < 0)
        {
            printf("Warning: AudioRecord read returned error %d, retry %d/%d\n", bytesRead, recordRetryCount + 1, kMaxRetries);
            recordRetryCount++;
            if (recordRetryCount >= kMaxRetries)
            {
                printf("Error: AudioRecord read failed after %d retries\n", kMaxRetries);
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

        // Successfully read data, reset retry count
        recordRetryCount = 0;

        // Write recorded data to file
        size_t bytesWrittenToFile = wavFile.writeData(recordBuffer, bytesRead);
        if (bytesWrittenToFile != static_cast<size_t>(bytesRead))
        {
            printf("Error: Failed to write to output file\n");
            recording = false;
            playing = false;
            break;
        }

        // Update total bytes read
        totalBytesRead += bytesRead;

        // Periodically update the WAV header to ensure file validity
        static int32_t headerUpdateCount = 0;
        headerUpdateCount++;
        if (headerUpdateCount >= 100)
        { // Update header every 100 iterations
            wavFile.updateHeader();
            headerUpdateCount = 0;
        }

        // quit if totalBytesRead exceeds limit (1GB)
        if (totalBytesRead >= MAX_DATA_SIZE)
        {
            printf("Warning: AudioRecord data size exceeds limit: %d MB\n", MAX_DATA_SIZE / (1024 * 1024));
            recording = false;
        }

        // Play the recorded data
        bytesWritten = 0;
        playRetryCount = 0;
        while (bytesWritten < bytesRead && playing)
        {
            int32_t written = audioTrack->write(recordBuffer + bytesWritten, bytesRead - bytesWritten);
            if (written < 0)
            {
                printf("Warning: AudioTrack write failed with error %d, retry %d/%d\n", written, playRetryCount + 1, kMaxRetries);
                playRetryCount++;
                if (playRetryCount >= kMaxRetries)
                {
                    printf("Error: AudioTrack write failed after %d retries\n", kMaxRetries);
                    playing = false;
                    break;
                }
                usleep(kRetryDelayUs); // wait before retry
                continue;
            }
            bytesWritten += written;
            playRetryCount = 0; // Reset retry count on successful write
        }
    }

    /************** stop AudioRecord and AudioTrack **************/
    printf("AudioRecord stop\n");
    audioRecord->stop();

    printf("AudioTrack stop\n");
    audioTrack->stop();

    // Finalize and close WAV file
    wavFile.finalize();
    printf("Total bytes read: %d\n", totalBytesRead);
    printf("Duplex audio completed. Recording saved to: %s\n", audioFilePath.c_str());

    return 0;
}

/************************** WAV File Management ******************************/
class WAVFile
{
public:
    struct Header
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
        void write(std::ofstream &out) const
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
        void print() const
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

private:
    Header header_;
    std::string filePath_;
    mutable std::fstream fileStream_;
    bool headerWritten_;
    std::streampos dataSizePos_; // Position of dataSize field for updates

public:
    WAVFile() : headerWritten_(false)
    {
        // Initialize header with default values
        memset(&header_, 0, sizeof(Header));
    }

    /**
     * Creates a new WAV file for writing with the specified parameters.
     *
     * @param filePath The path to the WAV file.
     * @param sampleRate The sample rate of the audio data.
     * @param numChannels The number of channels in the audio data.
     * @param bitsPerSample The number of bits per sample in the audio data.
     * @return True if the file was created successfully, false otherwise.
     */
    bool createForWriting(const std::string &filePath, uint32_t sampleRate, uint32_t numChannels, uint32_t bitsPerSample)
    {
        filePath_ = filePath;
        fileStream_.open(filePath_, std::ios::binary | std::ios::out | std::ios::in | std::ios::trunc);
        if (!fileStream_.is_open())
        {
            return false;
        }

        // Initialize header
        strncpy(header_.riffID, "RIFF", 4);
        strncpy(header_.waveID, "WAVE", 4);
        strncpy(header_.fmtID, "fmt ", 4);
        strncpy(header_.dataID, "data", 4);

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
        headerWritten_ = true;
        dataSizePos_ = fileStream_.tellp() - 4; // Position of dataSize field

        return fileStream_.good();
    }

    /**
     * Opens an existing WAV file for reading.
     *
     * @param filePath The path to the WAV file.
     * @return True if the file was opened successfully, false otherwise.
     */
    bool openForReading(const std::string &filePath)
    {
        filePath_ = filePath;
        fileStream_.open(filePath_, std::ios::binary | std::ios::in);
        if (!fileStream_.is_open())
        {
            return false;
        }

        header_.read(fileStream_);
        headerWritten_ = true;
        return fileStream_.good();
    }

    /**
     * Writes audio data to the WAV file.
     *
     * @param data Pointer to the audio data to write.
     * @param size Number of bytes to write.
     * @return Number of bytes actually written.
     */
    size_t writeData(const char *data, size_t size)
    {
        if (!fileStream_.is_open() || !headerWritten_)
        {
            return 0;
        }

        fileStream_.write(data, size);
        if (fileStream_.good())
        {
            header_.dataSize += size;
            header_.riffSize += size;
            return size;
        }
        return 0;
    }

    /**
     * Updates the WAV file header with current sizes.
     * This should be called periodically to ensure the file is valid
     * even if recording is interrupted.
     */
    void updateHeader()
    {
        if (fileStream_.is_open() && headerWritten_)
        {
            // Save current position
            std::streampos currentPos = fileStream_.tellp();

            // Update RIFF chunk size
            fileStream_.seekp(4, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char *>(&header_.riffSize), sizeof(header_.riffSize));

            // Update data chunk size
            fileStream_.seekp(dataSizePos_, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char *>(&header_.dataSize), sizeof(header_.dataSize));

            // Flush to ensure data is written to disk
            fileStream_.flush();

            // Return to previous position
            fileStream_.seekp(currentPos);
        }
    }

    /**
     * Reads audio data from the WAV file.
     *
     * @param data Pointer to buffer where data will be stored.
     * @param size Maximum number of bytes to read.
     * @return Number of bytes actually read.
     */
    size_t readData(char *data, size_t size)
    {
        if (!fileStream_.is_open() || !headerWritten_)
        {
            return 0;
        }

        fileStream_.read(data, size);
        return fileStream_.gcount();
    }

    /**
     * Updates the WAV file header with final sizes and closes the file.
     */
    void finalize()
    {
        if (fileStream_.is_open() && headerWritten_)
        {
            // Update header with final sizes
            std::streampos currentPos = fileStream_.tellp();

            // Update RIFF chunk size
            fileStream_.seekp(4, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char *>(&header_.riffSize), sizeof(header_.riffSize));

            // Update data chunk size
            fileStream_.seekp(dataSizePos_, std::ios::beg);
            fileStream_.write(reinterpret_cast<const char *>(&header_.dataSize), sizeof(header_.dataSize));

            fileStream_.seekp(currentPos); // Return to end of file
            fileStream_.close();
        }
    }

    /**
     * Closes the WAV file without updating the header.
     */
    void close()
    {
        if (fileStream_.is_open())
        {
            fileStream_.close();
        }
    }

    /**
     * Gets the file path.
     *
     * @return The file path.
     */
    const std::string &getFilePath() const
    {
        return filePath_;
    }

    /**
     * Gets the WAV header.
     *
     * @return The WAV header.
     */
    const Header &getHeader() const
    {
        return header_;
    }

    /**
     * Determines the audio format from the WAV header.
     *
     * @return The audio format.
     */
    audio_format_t getAudioFormat() const
    {
        if ((header_.audioFormat == 1) && (header_.bitsPerSample == 8))
        {
            return AUDIO_FORMAT_PCM_8_BIT;
        }
        else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 16))
        {
            return AUDIO_FORMAT_PCM_16_BIT;
        }
        else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 24))
        {
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        }
        else if ((header_.audioFormat == 1) && (header_.bitsPerSample == 32))
        {
            return AUDIO_FORMAT_PCM_32_BIT;
        }
        else if ((header_.audioFormat == 3) && (header_.bitsPerSample == 32))
        {
            return AUDIO_FORMAT_PCM_FLOAT;
        }
        else
        {
            return AUDIO_FORMAT_INVALID;
        }
    }

    /**
     * Gets the sample rate from the header.
     *
     * @return The sample rate.
     */
    int32_t getSampleRate() const
    {
        return header_.sampleRate;
    }

    /**
     * Gets the number of channels from the header.
     *
     * @return The number of channels.
     */
    int32_t getNumChannels() const
    {
        return header_.numChannels;
    }

    /**
     * Gets the bits per sample from the header.
     *
     * @return The bits per sample.
     */
    uint32_t getBitsPerSample() const
    {
        return header_.bitsPerSample;
    }
};

/* BufferManager class */
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
