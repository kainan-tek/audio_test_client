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
// This file contains the implementation of audio recording, playback,
// loopback testing, and system parameter configuration using Android
// AudioRecord and AudioTrack Native APIs.

#include "audio_test_client.h"

#include <getopt.h>

/************************** WavFile Implementation ******************************/

WavFile::~WavFile() noexcept { close(); }

void WavFile::Header::write(std::ostream& out) const {
    out.write(riff_id, 4);
    out.write(reinterpret_cast<const char*>(&riff_size), 4);
    out.write(wave_id, 4);
    out.write(fmt_id, 4);
    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    out.write(reinterpret_cast<const char*>(&audio_format), 2);
    out.write(reinterpret_cast<const char*>(&num_channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
    out.write(data_id, 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
}

void WavFile::Header::read(std::istream& in) {
    in.read(riff_id, 4);
    in.read(reinterpret_cast<char*>(&riff_size), 4);
    in.read(wave_id, 4);
    in.read(fmt_id, 4);
    in.read(reinterpret_cast<char*>(&fmt_size), 4);
    in.read(reinterpret_cast<char*>(&audio_format), 2);
    in.read(reinterpret_cast<char*>(&num_channels), 2);
    in.read(reinterpret_cast<char*>(&sample_rate), 4);
    in.read(reinterpret_cast<char*>(&byte_rate), 4);
    in.read(reinterpret_cast<char*>(&block_align), 2);
    in.read(reinterpret_cast<char*>(&bits_per_sample), 2);
    in.read(data_id, 4);
    in.read(reinterpret_cast<char*>(&data_size), 4);
}

void WavFile::Header::print() const {
    printf("RiffID: %.4s\n", riff_id);
    printf("RiffSize: %" PRIu32 "\n", riff_size);
    printf("WaveID: %.4s\n", wave_id);
    printf("FmtID: %.4s\n", fmt_id);
    printf("FmtSize: %" PRIu32 "\n", fmt_size);
    printf("AudioFormat: %u\n", static_cast<unsigned>(audio_format));
    printf("NumChannels: %u\n", static_cast<unsigned>(num_channels));
    printf("SampleRate: %" PRIu32 "\n", sample_rate);
    printf("ByteRate: %" PRIu32 "\n", byte_rate);
    printf("BlockAlign: %u\n", static_cast<unsigned>(block_align));
    printf("BitsPerSample: %u\n", static_cast<unsigned>(bits_per_sample));
    printf("DataID: %.4s\n", data_id);
    printf("DataSize: %" PRIu32 "\n", data_size);
}

bool WavFile::createForWriting(const std::string& file_path,
                               int32_t sample_rate,
                               int32_t num_channels,
                               uint32_t bits_per_sample) {
    file_path_ = file_path;
    file_stream_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_stream_.is_open()) {
        return false;
    }

    memcpy(header_.riff_id, "RIFF", 4);
    memcpy(header_.wave_id, "WAVE", 4);
    memcpy(header_.fmt_id, "fmt ", 4);
    memcpy(header_.data_id, "data", 4);

    header_.fmt_size = 16;
    header_.audio_format = 1;
    header_.num_channels = static_cast<uint16_t>(num_channels);
    header_.sample_rate = static_cast<uint32_t>(sample_rate);
    header_.bits_per_sample = static_cast<uint16_t>(bits_per_sample);

    uint32_t bytes_per_sample = bits_per_sample / 8;
    header_.byte_rate = static_cast<uint32_t>(sample_rate) * static_cast<uint32_t>(num_channels) * bytes_per_sample;
    header_.block_align = static_cast<uint16_t>(num_channels * static_cast<int32_t>(bytes_per_sample));
    header_.data_size = 0;
    header_.riff_size = 36;

    header_.write(file_stream_);
    if (!file_stream_.good()) {
        file_stream_.close();
        return false;
    }

    is_header_valid_ = true;
    data_size_pos_ = file_stream_.tellp() - std::streamoff(4);

    return file_stream_.good();
}

bool WavFile::openForReading(const std::string& file_path) {
    file_path_ = file_path;
    file_stream_.open(file_path_, std::ios::binary | std::ios::in);
    if (!file_stream_.is_open()) {
        return false;
    }

    header_.read(file_stream_);
    if (strncmp(header_.riff_id, "RIFF", 4) != 0 || strncmp(header_.wave_id, "WAVE", 4) != 0 ||
        strncmp(header_.fmt_id, "fmt ", 4) != 0 || strncmp(header_.data_id, "data", 4) != 0) {
        file_stream_.close();
        return false;
    }
    if (header_.fmt_size < 16 || header_.audio_format != 1 || header_.num_channels == 0 || header_.sample_rate == 0) {
        file_stream_.close();
        return false;
    }

    is_header_valid_ = true;
    return file_stream_.good();
}

size_t WavFile::writeData(const char* data, size_t size) {
    if (!file_stream_.is_open() || !is_header_valid_ || !data || size == 0) {
        return 0;
    }

    if (header_.data_size > UINT32_MAX - size) {
        printf("Error: WAV file size limit (4GB) reached. Stopping recording.\n");
        return 0;
    }

    file_stream_.write(data, static_cast<std::streamsize>(size));
    if (file_stream_.good()) {
        header_.data_size += static_cast<uint32_t>(size);
        header_.riff_size = 36 + header_.data_size;
        return size;
    }
    return 0;
}

void WavFile::updateHeader() {
    if (file_stream_.is_open() && is_header_valid_) {
        const auto current_pos = file_stream_.tellp();

        file_stream_.seekp(4, std::ios::beg);
        file_stream_.write(reinterpret_cast<const char*>(&header_.riff_size), sizeof(header_.riff_size));

        file_stream_.seekp(data_size_pos_);
        file_stream_.write(reinterpret_cast<const char*>(&header_.data_size), sizeof(header_.data_size));

        file_stream_.flush();
        file_stream_.seekp(current_pos);
    }
}

size_t WavFile::readData(char* data, size_t size) {
    if (!file_stream_.is_open() || !is_header_valid_) {
        return 0;
    }

    file_stream_.read(data, static_cast<std::streamsize>(size));
    return static_cast<size_t>(file_stream_.gcount());
}

void WavFile::finalize() {
    if (file_stream_.is_open() && is_header_valid_) {
        updateHeader();
        file_stream_.close();
    }
}

void WavFile::close() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

const std::string& WavFile::getFilePath() const { return file_path_; }
const WavFile::Header& WavFile::getHeader() const { return header_; }
int32_t WavFile::getSampleRate() const { return static_cast<int32_t>(header_.sample_rate); }
int32_t WavFile::getNumChannels() const { return static_cast<int32_t>(header_.num_channels); }
uint32_t WavFile::getBitsPerSample() const { return header_.bits_per_sample; }
bool WavFile::isOpen() const { return file_stream_.is_open(); }

audio_format_t WavFile::getAudioFormat() const {
    if (header_.audio_format == 1) {
        // PCM format
        switch (header_.bits_per_sample) {
        case 8:
            return AUDIO_FORMAT_PCM_8_BIT;
        case 16:
            return AUDIO_FORMAT_PCM_16_BIT;
        case 24:
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        case 32:
            return AUDIO_FORMAT_PCM_32_BIT;
        default:
            printf("Error: Unsupported PCM bit depth: %u\n", header_.bits_per_sample);
            return AUDIO_FORMAT_INVALID;
        }
    } else if (header_.audio_format == 3) {
        // IEEE float format - not supported by Android AudioTrack/AudioRecord
        printf("Error: IEEE float WAV format (audio_format=3) is not supported\n");
        return AUDIO_FORMAT_INVALID;
    }
    printf("Error: Unknown WAV audio format: %u\n", header_.audio_format);
    return AUDIO_FORMAT_INVALID;
}

/************************** RawPcmFile Implementation ******************************/

RawPcmFile::~RawPcmFile() noexcept { close(); }

bool RawPcmFile::createForWriting(const std::string& file_path,
                                  int32_t sample_rate,
                                  int32_t num_channels,
                                  uint32_t bits_per_sample) {
    file_path_ = file_path;
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
    bits_per_sample_ = bits_per_sample;

    file_stream_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    return file_stream_.is_open();
}

bool RawPcmFile::openForReading(const std::string& file_path) {
    file_path_ = file_path;
    file_stream_.open(file_path_, std::ios::binary | std::ios::in);
    return file_stream_.is_open();
}

size_t RawPcmFile::writeData(const char* data, size_t size) {
    if (!file_stream_.is_open() || !data || size == 0) {
        return 0;
    }

    file_stream_.write(data, static_cast<std::streamsize>(size));
    return file_stream_.good() ? size : 0;
}

size_t RawPcmFile::readData(char* data, size_t size) {
    if (!file_stream_.is_open()) {
        return 0;
    }

    file_stream_.read(data, static_cast<std::streamsize>(size));
    return static_cast<size_t>(file_stream_.gcount());
}

void RawPcmFile::close() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

const std::string& RawPcmFile::getFilePath() const { return file_path_; }
int32_t RawPcmFile::getSampleRate() const { return sample_rate_; }
int32_t RawPcmFile::getNumChannels() const { return num_channels_; }
uint32_t RawPcmFile::getBitsPerSample() const { return bits_per_sample_; }
bool RawPcmFile::isOpen() const { return file_stream_.is_open(); }

audio_format_t RawPcmFile::getAudioFormat() const {
    switch (bits_per_sample_) {
    case 8:
        return AUDIO_FORMAT_PCM_8_BIT;
    case 16:
        return AUDIO_FORMAT_PCM_16_BIT;
    case 24:
        return AUDIO_FORMAT_PCM_24_BIT_PACKED;
    case 32:
        return AUDIO_FORMAT_PCM_32_BIT;
    default:
        return AUDIO_FORMAT_PCM_16_BIT;
    }
}

void RawPcmFile::setAudioParameters(int32_t sample_rate, int32_t num_channels, uint32_t bits_per_sample) {
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
    bits_per_sample_ = bits_per_sample;
}

/************************** AudioFileFactory Implementation ******************************/

std::unique_ptr<AudioFileInterface> AudioFileFactory::create(AudioFileFormat format) {
    switch (format) {
    case AudioFileFormat::kWav:
        return std::make_unique<WavFile>();
    case AudioFileFormat::kRawPcm:
        return std::make_unique<RawPcmFile>();
    default:
        return nullptr;
    }
}

AudioFileFormat AudioFileFactory::detectFormatFromPath(const std::string& file_path) {
    if (file_path.size() >= 4) {
        std::string ext = file_path.substr(file_path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") {
            return AudioFileFormat::kWav;
        }
        if (ext == ".pcm" || ext == ".raw") {
            return AudioFileFormat::kRawPcm;
        }
    }
    return AudioFileFormat::kWav;
}

std::string AudioFileFactory::getDefaultExtension(AudioFileFormat format) {
    switch (format) {
    case AudioFileFormat::kWav:
        return ".wav";
    case AudioFileFormat::kRawPcm:
        return ".pcm";
    default:
        return ".wav";
    }
}

/************************** BufferManager Implementation ******************************/

BufferManager::BufferManager(size_t buffer_size) { initializeBuffer(buffer_size); }

char* BufferManager::get() const { return buffer_.get(); }
size_t BufferManager::getSize() const { return size_; }
bool BufferManager::isValid() const { return buffer_ != nullptr && size_ > 0; }

void BufferManager::initializeBuffer(size_t requested_size) {
    static constexpr size_t kMinBufferSize = 480;
    static constexpr size_t kMaxBufferSize = 64 * 1024 * 1024;
    const size_t validated_size = std::clamp(requested_size, kMinBufferSize, kMaxBufferSize);

    try {
        buffer_ = std::make_unique<char[]>(validated_size);
        size_ = validated_size;
        printf("BufferManager created with buffer size: %zu\n", size_);
    } catch (const std::bad_alloc& e) {
        buffer_.reset();
        size_ = 0;
        printf("Error: Failed to allocate buffer of size %zu: %s\n", validated_size, e.what());
    }
}

/************************** AudioUtils Implementation ******************************/

audio_stream_type_t AudioUtils::usageToStreamType(audio_usage_t usage) {
    switch (usage) {
    case AUDIO_USAGE_UNKNOWN:
    case AUDIO_USAGE_MEDIA:
    case AUDIO_USAGE_GAME:
    case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
    case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
        return AUDIO_STREAM_MUSIC;

    case AUDIO_USAGE_VOICE_COMMUNICATION:
    case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
        return AUDIO_STREAM_VOICE_CALL;

    case AUDIO_USAGE_ALARM:
        return AUDIO_STREAM_ALARM;

    case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
        return AUDIO_STREAM_RING;

    case AUDIO_USAGE_NOTIFICATION:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
    case AUDIO_USAGE_NOTIFICATION_EVENT:
        return AUDIO_STREAM_NOTIFICATION;

    case AUDIO_USAGE_ASSISTANT:
    case AUDIO_USAGE_CALL_ASSISTANT:
        return AUDIO_STREAM_ASSISTANT;

    case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
        return AUDIO_STREAM_SYSTEM;

    case AUDIO_USAGE_VIRTUAL_SOURCE:
        printf("Warning: VIRTUAL_SOURCE usage mapped to STREAM_MUSIC (virtual audio processing)\n");
        return AUDIO_STREAM_MUSIC;

    case AUDIO_USAGE_EMERGENCY:
    case AUDIO_USAGE_SAFETY:
    case AUDIO_USAGE_VEHICLE_STATUS:
    case AUDIO_USAGE_ANNOUNCEMENT:
        printf("Warning: Usage %d has no direct stream type mapping, using STREAM_SYSTEM\n", usage);
        return AUDIO_STREAM_SYSTEM;

    default:
        printf("Warning: Unknown audio usage %d, defaulting to STREAM_MUSIC\n", usage);
        return AUDIO_STREAM_MUSIC;
    }
}

audio_content_type_t AudioUtils::usageToContentType(audio_usage_t usage) {
    switch (usage) {
    case AUDIO_USAGE_UNKNOWN:
    case AUDIO_USAGE_MEDIA:
    case AUDIO_USAGE_GAME:
        return AUDIO_CONTENT_TYPE_MUSIC;

    case AUDIO_USAGE_VOICE_COMMUNICATION:
    case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
    case AUDIO_USAGE_ASSISTANT:
    case AUDIO_USAGE_CALL_ASSISTANT:
    case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
    case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
        return AUDIO_CONTENT_TYPE_SPEECH;

    case AUDIO_USAGE_ALARM:
    case AUDIO_USAGE_NOTIFICATION:
    case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
    case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
    case AUDIO_USAGE_NOTIFICATION_EVENT:
    case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
        return AUDIO_CONTENT_TYPE_SONIFICATION;

    case AUDIO_USAGE_VIRTUAL_SOURCE:
        return AUDIO_CONTENT_TYPE_SPEECH;

    case AUDIO_USAGE_EMERGENCY:
    case AUDIO_USAGE_SAFETY:
    case AUDIO_USAGE_VEHICLE_STATUS:
    case AUDIO_USAGE_ANNOUNCEMENT:
        return AUDIO_CONTENT_TYPE_SONIFICATION;

    default:
        printf("Warning: Unknown audio usage %d, defaulting to CONTENT_TYPE_MUSIC\n", usage);
        return AUDIO_CONTENT_TYPE_MUSIC;
    }
}

audio_format_t AudioUtils::parseFormatOption(const int v) {
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

std::string AudioUtils::getFormatTime() {
    time_t current_time = time(nullptr);
    struct tm now;
    if (localtime_r(&current_time, &now) != nullptr) {
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y%m%d_%H.%M.%S", &now);
        return std::string(buffer);
    } else {
        return std::string("00000000_00.00.00");
    }
}

std::string AudioUtils::getTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm now;
    if (localtime_r(&tv.tv_sec, &now) != nullptr) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", now.tm_hour, now.tm_min, now.tm_sec,
                 static_cast<int>(tv.tv_usec / 1000));
        return std::string(buffer);
    } else {
        return std::string("00:00:00.000");
    }
}

std::string AudioUtils::makeRecordFilePath(const int32_t sample_rate,
                                           const int32_t channel_count,
                                           const uint32_t bits_per_sample,
                                           const std::string& override_path,
                                           AudioFileFormat format) {
    if (!override_path.empty()) {
        return override_path;
    }
    const std::string format_time = AudioUtils::getFormatTime();
    const std::string ext = AudioFileFactory::getDefaultExtension(format);
    char buffer[256];
    int bytes_written = snprintf(buffer, sizeof(buffer), "/data/record_%dHz_%dch_%dbit_%s%s", sample_rate,
                                 channel_count, bits_per_sample, format_time.c_str(), ext.c_str());

    if (bytes_written >= 240 || bytes_written < 0) {
        snprintf(buffer, sizeof(buffer), "/data/audio_%s%s", format_time.c_str(), ext.c_str());
        printf("Warning: File path too long, using shortened name\n");
    }

    return std::string(buffer);
}

/************************** SignalGuard Implementation ******************************/

SignalGuard::SignalGuard() {
    s_exit_requested_.store(false);
    signal(SIGINT, signalHandler);
}

SignalGuard::~SignalGuard() noexcept { signal(SIGINT, SIG_DFL); }

bool SignalGuard::isExitRequested() const { return s_exit_requested_.load(); }

void SignalGuard::signalHandler(int sig) {
    if (sig == SIGINT) {
        s_exit_requested_.store(true);
    }
}

/************************** AudioParameterManager Implementation ******************************/

const android::String8 AudioParameterManager::kParamOpenSource = android::String8("open_source");
const android::String8 AudioParameterManager::kParamCloseSource = android::String8("close_source");
const android::String8 AudioParameterManager::kParamChannelMask = android::String8("channel_mask");

void AudioParameterManager::setOpenSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(kParamOpenSource, audioUsageToString(usage));
}

void AudioParameterManager::setCloseSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(kParamCloseSource, audioUsageToString(usage));
}

void AudioParameterManager::setChannelMask(const android::sp<android::AudioTrack>& audio_track,
                                           audio_channel_mask_t channel_mask) {
    setAudioTrackParameter(audio_track, kParamChannelMask, android::String8::format("%d", channel_mask));
}

void AudioParameterManager::setSystemParameter(const android::String8& key, const android::String8& value) {
    if constexpr (kEnableSetParams) {
        android::AudioParameter audio_param;
        audio_param.add(key, value);
        android::String8 param_string = audio_param.toString();
        android::AudioSystem::setParameters(param_string);
        printf("Set parameter: %s\n", param_string.c_str());
    }
}

void AudioParameterManager::setAudioTrackParameter(const android::sp<android::AudioTrack>& audio_track,
                                                   const android::String8& key,
                                                   const android::String8& value) {
    if constexpr (kEnableSetParams) {
        android::AudioParameter audio_param;
        audio_param.add(key, value);
        android::String8 param_string = audio_param.toString();
        audio_track->setParameters(param_string);
        printf("Set parameter: %s\n", param_string.c_str());
    }
}

android::String8 AudioParameterManager::audioUsageToString(audio_usage_t usage) {
    static const std::unordered_map<audio_usage_t, const char*> usage_map = {
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
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST"},
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT"},
        {AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED, "AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED"},
        {AUDIO_USAGE_EMERGENCY, "AUDIO_USAGE_EMERGENCY"},
        {AUDIO_USAGE_SAFETY, "AUDIO_USAGE_SAFETY"},
        {AUDIO_USAGE_VEHICLE_STATUS, "AUDIO_USAGE_VEHICLE_STATUS"},
        {AUDIO_USAGE_ANNOUNCEMENT, "AUDIO_USAGE_ANNOUNCEMENT"},
    };
    const char* usage_name = "AUDIO_USAGE_UNKNOWN";
    const auto it = usage_map.find(usage);
    if (it != usage_map.end()) {
        usage_name = it->second;
    }
    return android::String8::format("0:%s", usage_name);
}

/************************** ThreadSafeBufferQueue Implementation ******************************/

ThreadSafeBufferQueue::ThreadSafeBufferQueue(size_t max_buffers) : max_buffers_(max_buffers), stopped_(false) {}

void ThreadSafeBufferQueue::push(std::vector<char>&& buffer) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < max_buffers_ || stopped_; });
    if (!stopped_) {
        queue_.push(std::move(buffer));
        not_empty_.notify_one();
    }
}

bool ThreadSafeBufferQueue::pop(std::vector<char>& buffer) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [this] { return !queue_.empty() || stopped_; });
    if (queue_.empty()) {
        return false;
    }
    buffer = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return true;
}

void ThreadSafeBufferQueue::stop() {
    std::unique_lock<std::mutex> lock(mutex_);
    stopped_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
}

void ThreadSafeBufferQueue::reset() {
    std::unique_lock<std::mutex> lock(mutex_);
    stopped_ = false;
    while (!queue_.empty()) {
        queue_.pop();
    }
}

size_t ThreadSafeBufferQueue::size() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

/************************** AudioOperation Implementation ******************************/

AudioOperation::AudioOperation(const AudioConfig& config) : config_(config) {}

size_t AudioOperation::calculateBufferSize() const {
    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    // Use default frame count if min_frame_count is not set or too small
    static constexpr size_t kDefaultFrameCount = 480;
    const size_t effective_frame_count = (config_.min_frame_count > 0) ? config_.min_frame_count : kDefaultFrameCount;
    // Double the buffer for safety margin
    return (effective_frame_count * 2) * config_.channel_count * bytes_per_sample;
}

size_t AudioOperation::calculateFrameCount() const {
    const size_t min_frames = static_cast<size_t>((config_.sample_rate * 10) / 1000);
    const size_t adjusted_min_frame_count = std::max(config_.min_frame_count, min_frames);
    return adjusted_min_frame_count * 2;
}

uint64_t AudioOperation::calculateBytesPerSecond() const {
    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    return static_cast<uint64_t>(config_.sample_rate) * config_.channel_count * bytes_per_sample;
}

bool AudioOperation::validateAudioParameters() const {
    if (config_.sample_rate <= 0 || config_.channel_count <= 0) {
        printf("Error: Invalid sample rate or channel count\n");
        return false;
    }
    if (config_.format == AUDIO_FORMAT_INVALID) {
        printf("Error: Invalid audio format\n");
        return false;
    }
    return true;
}

android::content::AttributionSourceState AudioOperation::createAttributionSource() const {
    android::content::AttributionSourceState attribution_source;
    attribution_source.packageName = std::string("Audio Test Client");
    attribution_source.token = android::sp<android::BBinder>::make();
    attribution_source.uid = getuid();
    attribution_source.pid = getpid();
    return attribution_source;
}

bool AudioOperation::initializeAudioRecord(android::sp<android::AudioRecord>& audio_record) {
    audio_channel_mask_t channel_mask = audio_channel_in_mask_from_count(config_.channel_count);
    if (android::AudioRecord::getMinFrameCount(&config_.min_frame_count, config_.sample_rate, config_.format,
                                               channel_mask) != android::NO_ERROR) {
        printf("Warning: Cannot get min frame count, using default value\n");
    }
    const size_t frame_count = calculateFrameCount();

    printf("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
           "frameCount=%zu\n",
           config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);
    ALOGI("Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
          "frameCount=%zu",
          config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);

    android::content::AttributionSourceState attribution_source = createAttributionSource();
    audio_attributes_t attributes{};
    attributes.source = config_.input_source;
    audio_record = android::sp<android::AudioRecord>::make(attribution_source);
    if (audio_record->set(config_.input_source, config_.sample_rate, config_.format, channel_mask, frame_count, nullptr,
#ifndef ANDROID_API_14_PLUS
                          nullptr,
#endif
                          0, false, AUDIO_SESSION_ALLOCATE, android::AudioRecord::TRANSFER_SYNC, config_.input_flag,
                          getuid(), getpid(), &attributes, AUDIO_PORT_HANDLE_NONE) != android::NO_ERROR) {
        printf("Error: Failed to initialize AudioRecord parameters\n");
        ALOGE("Failed to initialize AudioRecord parameters");
        return false;
    }

    if (audio_record->initCheck() != android::NO_ERROR) {
        printf("Error: AudioRecord initialization check failed\n");
        ALOGE("AudioRecord initialization check failed");
        return false;
    }

    printf("AudioRecord initialized successfully\n");
    return true;
}

bool AudioOperation::initializeAudioTrack(android::sp<android::AudioTrack>& audio_track) {
    audio_channel_mask_t channel_mask = audio_channel_out_mask_from_count(config_.channel_count);

    audio_stream_type_t stream_type = AudioUtils::usageToStreamType(config_.usage);
    if (android::AudioTrack::getMinFrameCount(&config_.min_frame_count, stream_type, config_.sample_rate) !=
        android::NO_ERROR) {
        printf("Warning: Cannot get min frame count using streamType, using default value\n");
    }
    const size_t frame_count = calculateFrameCount();

    printf("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
           "frameCount=%zu\n",
           config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);
    ALOGI("Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
          "frameCount=%zu",
          config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, frame_count);

    android::content::AttributionSourceState attribution_source = createAttributionSource();
    audio_attributes_t attributes{};
    attributes.usage = config_.usage;
    attributes.content_type = AudioUtils::usageToContentType(config_.usage);
    audio_track = android::sp<android::AudioTrack>::make(attribution_source);
    if (audio_track->set(AUDIO_STREAM_DEFAULT, config_.sample_rate, config_.format, channel_mask, frame_count,
                         config_.output_flag, nullptr,
#ifndef ANDROID_API_14_PLUS
                         nullptr,
#endif
                         0, nullptr, false, AUDIO_SESSION_ALLOCATE, android::AudioTrack::TRANSFER_SYNC, nullptr,
                         attribution_source, &attributes, false, 1.0f, AUDIO_PORT_HANDLE_NONE) != android::NO_ERROR) {
        printf("Error: Failed to initialize AudioTrack parameters\n");
        ALOGE("Failed to initialize AudioTrack parameters");
        return false;
    }

    if (audio_track->initCheck() != android::NO_ERROR) {
        printf("Error: AudioTrack initialization check failed\n");
        ALOGE("AudioTrack initialization check failed");
        return false;
    }

    printf("AudioTrack initialized successfully\n");
    return true;
}

bool AudioOperation::setupAudioFileForRecording(std::unique_ptr<AudioFileInterface>& audio_file) {
    size_t bytes_per_sample = audio_bytes_per_sample(config_.format);

    AudioFileFormat actual_format = config_.record_file_format;

    if (!config_.record_file_path.empty()) {
        AudioFileFormat detected_format = AudioFileFactory::detectFormatFromPath(config_.record_file_path);
        if (detected_format != config_.record_file_format) {
            printf("Note: File format determined by file extension: %s (ignoring -t option)\n",
                   detected_format == AudioFileFormat::kWav ? "WAV" : "RawPCM");
            actual_format = detected_format;
        }
    }

    config_.record_file_path = AudioUtils::makeRecordFilePath(
        config_.sample_rate, config_.channel_count, bytes_per_sample * 8, config_.record_file_path, actual_format);

    audio_file = AudioFileFactory::create(actual_format);
    if (!audio_file) {
        printf("Error: Failed to create audio file handler\n");
        return false;
    }

    printf("Recording audio to file: %s (format: %s)\n", config_.record_file_path.c_str(),
           actual_format == AudioFileFormat::kWav ? "WAV" : "RawPCM");
    if (!audio_file->createForWriting(config_.record_file_path, config_.sample_rate, config_.channel_count,
                                      bytes_per_sample * 8)) {
        printf("Error: Can't create record file: %s\n", config_.record_file_path.c_str());
        return false;
    }

    return true;
}

bool AudioOperation::setupAudioFileForPlayback(std::unique_ptr<AudioFileInterface>& audio_file) {
    if (config_.play_file_path.empty() || access(config_.play_file_path.c_str(), F_OK) == -1) {
        printf("Error: File does not exist: %s\n", config_.play_file_path.c_str());
        return false;
    }

    AudioFileFormat format = AudioFileFactory::detectFormatFromPath(config_.play_file_path);
    audio_file = AudioFileFactory::create(format);
    if (!audio_file) {
        printf("Error: Failed to create audio file handler\n");
        return false;
    }

    if (!audio_file->openForReading(config_.play_file_path)) {
        printf("Error: Failed to open audio file: %s\n", config_.play_file_path.c_str());
        return false;
    }

    if (format == AudioFileFormat::kWav) {
        config_.sample_rate = audio_file->getSampleRate();
        config_.channel_count = audio_file->getNumChannels();
        config_.format = audio_file->getAudioFormat();
    } else {
        // Raw PCM file - use specified or default parameters
        printf("Note: Raw PCM file has no header. Using parameters: sampleRate=%d, channels=%d, format=%d\n",
               config_.sample_rate, config_.channel_count, config_.format);
        printf("      Use -r, -c, -f options to specify correct parameters if needed.\n");
    }
    printf("audio file info: %s, sampleRate: %d, channelCount: %d, format: %d, fileType: %s\n",
           config_.play_file_path.c_str(), config_.sample_rate, config_.channel_count, config_.format,
           format == AudioFileFormat::kWav ? "WAV" : "RawPCM");

    return true;
}

void AudioOperation::updateLevelMeter(const char* buffer, size_t size) {
    if (++level_meter_counter_ % kLevelMeterInterval != 0) {
        return;
    }

    constexpr float kNorm16bit = 32768.0f;
    constexpr float kNorm32bit = 2147483648.0f;
    constexpr float kDbFloor = -60.0f;

    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    if (size == 0 || bytes_per_sample == 0) {
        printf("Error: Invalid input size or bytes_per_sample\n");
        return;
    }

    const size_t num_samples = size / bytes_per_sample;
    float peak_amplitude = 0.0f;
    if (bytes_per_sample == 2) {
        const int16_t* int16_data = reinterpret_cast<const int16_t*>(buffer);
        for (size_t i = 0; i < num_samples; ++i) {
            peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(int16_data[i])) / kNorm16bit);
        }
    } else if (bytes_per_sample == 4) {
        const int32_t* int32_data = reinterpret_cast<const int32_t*>(buffer);
        for (size_t i = 0; i < num_samples; ++i) {
            peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(int32_data[i])) / kNorm32bit);
        }
    } else {
        printf("Error: Unsupported audio format for level meter\n");
        return;
    }

    const float db_level = peak_amplitude > 0.0f ? std::max(20.0f * std::log10(peak_amplitude), kDbFloor) : kDbFloor;
    const std::string timestamp = AudioUtils::getTimestamp();
    printf("[%s] Debug Audio Level: %.1f dB, bytes: %zu\n", timestamp.c_str(), db_level, size);
}

bool AudioOperation::startAudioRecord(const android::sp<android::AudioRecord>& audio_record) {
    printf("Starting AudioRecord\n");
    ALOGI("Starting AudioRecord");
    android::status_t start_result = audio_record->start();
    if (start_result != android::NO_ERROR) {
        printf("Error: AudioRecord start failed with status %d\n", start_result);
        ALOGE("AudioRecord start failed with status %d", start_result);
        return false;
    }
    return true;
}

bool AudioOperation::startAudioTrack(const android::sp<android::AudioTrack>& audio_track) {
    audio_param_manager_.setOpenSourceWithUsage(config_.usage);

    printf("Starting AudioTrack\n");
    ALOGI("Starting AudioTrack");
    android::status_t start_result = audio_track->start();
    if (start_result != android::NO_ERROR) {
        printf("Error: AudioTrack start failed with status %d\n", start_result);
        ALOGE("AudioTrack start failed with status %d", start_result);
        return false;
    }
    return true;
}

void AudioOperation::stopAudioRecord(const android::sp<android::AudioRecord>& audio_record) {
    if (audio_record != nullptr) {
        printf("Stopping AudioRecord\n");
        ALOGI("Stopping AudioRecord");
        audio_record->stop();
    }
}

void AudioOperation::stopAudioTrack(const android::sp<android::AudioTrack>& audio_track) {
    if (audio_track != nullptr) {
        printf("Stopping AudioTrack\n");
        ALOGI("Stopping AudioTrack");
        audio_track->stop();
        audio_param_manager_.setCloseSourceWithUsage(config_.usage);
    }
}

bool AudioOperation::reportRecordProgress(const android::sp<android::AudioRecord>& audio_record,
                                          uint64_t total_bytes_processed,
                                          uint64_t bytes_per_second,
                                          AudioFileInterface* audio_file) {
    if (audio_record == nullptr) {
        return false;
    }

    if (total_bytes_processed >= next_progress_report_) {
        printf("Recording ... , processed %.2f seconds, %.2f MB\n",
               static_cast<float>(total_bytes_processed) / bytes_per_second,
               static_cast<float>(total_bytes_processed) / (1024u * 1024u));
        next_progress_report_ += bytes_per_second * kProgressReportInterval;

        if (audio_file) {
            audio_file->updateHeader();
        }
        return true;
    }
    return false;
}

bool AudioOperation::reportPlayProgress(const android::sp<android::AudioTrack>& audio_track,
                                        uint64_t total_bytes_processed,
                                        uint64_t bytes_per_second) {
    if (audio_track == nullptr) {
        return false;
    }

    if (total_bytes_processed >= next_progress_report_) {
        printf("Playing ... , processed %.2f seconds, %.2f MB\n",
               static_cast<float>(total_bytes_processed) / bytes_per_second,
               static_cast<float>(total_bytes_processed) / (1024u * 1024u));
        next_progress_report_ += bytes_per_second * kProgressReportInterval;
        return true;
    }
    return false;
}

/************************** AudioRecordOperation Implementation ******************************/

AudioRecordOperation::AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioRecordOperation::execute() {
    std::unique_ptr<AudioFileInterface> audio_file;
    android::sp<android::AudioRecord> audio_record;

    if (!setupAudioFileForRecording(audio_file) || !validateAudioParameters()) {
        printf("Error: Failed to setup audio file or validate audio parameters\n");
        return -1;
    }

    if (!initializeAudioRecord(audio_record)) {
        audio_file->close();
        return -1;
    }

    if (!startAudioRecord(audio_record)) {
        audio_file->close();
        return -1;
    }

    int32_t operation_result = recordLoop(audio_record, *audio_file);

    stopAudioRecord(audio_record);
    audio_file->finalize();

    return operation_result;
}

int32_t AudioRecordOperation::recordLoop(const android::sp<android::AudioRecord>& audio_record,
                                         AudioFileInterface& audio_file) {
    BufferManager buffer_manager(calculateBufferSize());
    if (!buffer_manager.isValid()) {
        printf("Error: Failed to create valid buffer manager\n");
        return -1;
    }
    char* const audio_buffer = buffer_manager.get();

    if (config_.duration_seconds > 0) {
        printf("Recording for %d seconds...\n", config_.duration_seconds);
    }

    printf("Recording in progress. Press Ctrl+C to stop\n");
    ALOGI("Recording in progress.");
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    const uint64_t max_bytes_to_record =
        (config_.duration_seconds > 0) ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                                                  static_cast<uint64_t>(kMaxAudioDataSize))
                                       : static_cast<uint64_t>(kMaxAudioDataSize);
    next_progress_report_ = bytes_per_second * kProgressReportInterval;

    uint64_t total_bytes_read = 0;
    while (total_bytes_read < max_bytes_to_record && !signal_guard_.isExitRequested()) {
        const ssize_t bytes_read = audio_record->read(audio_buffer, calculateBufferSize());
        if (bytes_read < 0) {
            printf("Error: AudioRecord read failed: %zd\n", bytes_read);
            ALOGE("AudioRecord read failed: %zd", bytes_read);
            break;
        }
        if (bytes_read == 0) {
            continue;
        }
        total_bytes_read += static_cast<uint64_t>(bytes_read);

        updateLevelMeter(audio_buffer, static_cast<size_t>(bytes_read));

        if (audio_file.writeData(audio_buffer, static_cast<size_t>(bytes_read)) != static_cast<size_t>(bytes_read)) {
            printf("Error: Failed to save audio data to file\n");
            ALOGE("Failed to save audio data to file");
            break;
        }

        reportRecordProgress(audio_record, total_bytes_read, bytes_per_second, &audio_file);
    }

    printf("Recording finished: Recorded %" PRIu64 " bytes, File saved: %s\n", total_bytes_read,
           audio_file.getFilePath().c_str());

    return 0;
}

/************************** AudioPlayOperation Implementation ******************************/

AudioPlayOperation::AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioPlayOperation::execute() {
    std::unique_ptr<AudioFileInterface> audio_file;
    android::sp<android::AudioTrack> audio_track;

    if (!setupAudioFileForPlayback(audio_file) || !validateAudioParameters()) {
        printf("Error: Failed to setup audio file or validate audio parameters\n");
        return -1;
    }

    if (!initializeAudioTrack(audio_track)) {
        audio_file->close();
        return -1;
    }

    if (!startAudioTrack(audio_track)) {
        audio_file->close();
        return -1;
    }

    int32_t operation_result = playLoop(audio_track, *audio_file);

    stopAudioTrack(audio_track);
    audio_file->close();

    return operation_result;
}

int32_t AudioPlayOperation::playLoop(const android::sp<android::AudioTrack>& audio_track,
                                     AudioFileInterface& audio_file) {
    BufferManager buffer_manager(calculateBufferSize());
    if (!buffer_manager.isValid()) {
        printf("Error: Failed to create valid buffer manager\n");
        return -1;
    }
    char* const audio_buffer = buffer_manager.get();

    printf("Playing in progress. Press Ctrl+C to stop\n");
    ALOGI("Playing in progress.");
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    next_progress_report_ = bytes_per_second * kProgressReportInterval;
    uint64_t total_bytes_played = 0;
    while (!signal_guard_.isExitRequested()) {
        const size_t bytes_read = audio_file.readData(audio_buffer, calculateBufferSize());
        if (bytes_read == 0) {
            printf("End of file reached\n");
            break;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = bytes_read;
        while (bytes_written < bytes_to_write && !signal_guard_.isExitRequested()) {
            const ssize_t written = audio_track->write(audio_buffer + bytes_written, bytes_to_write - bytes_written);
            if (written < 0) {
                printf("Error: AudioTrack write failed: %zd\n", written);
                ALOGE("AudioTrack write failed: %zd", written);
                return -1;
            }
            bytes_written += static_cast<size_t>(written);
        }
        total_bytes_played += static_cast<uint64_t>(bytes_written);

        updateLevelMeter(audio_buffer, bytes_read);

        reportPlayProgress(audio_track, total_bytes_played, bytes_per_second);
    }
    printf("Playback finished: Total bytes played: %" PRIu64 "\n", total_bytes_played);

    return 0;
}

/************************** AudioLoopbackOperation Implementation ******************************/

AudioLoopbackOperation::AudioLoopbackOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioLoopbackOperation::execute() {
    std::unique_ptr<AudioFileInterface> audio_file;
    android::sp<android::AudioRecord> audio_record;
    android::sp<android::AudioTrack> audio_track;

    if (!setupAudioFileForRecording(audio_file) || !validateAudioParameters()) {
        printf("Error: Failed to setup audio file or validate audio parameters\n");
        return -1;
    }

    if (!initializeAudioRecord(audio_record)) {
        audio_file->close();
        return -1;
    }

    if (!initializeAudioTrack(audio_track)) {
        audio_file->close();
        return -1;
    }

    if (!startAudioRecord(audio_record)) {
        audio_file->close();
        return -1;
    }
    if (!startAudioTrack(audio_track)) {
        stopAudioRecord(audio_record);
        audio_file->close();
        return -1;
    }

    int32_t operation_result = loopbackLoopDualThread(audio_record, audio_track, *audio_file);

    stopAudioRecord(audio_record);
    stopAudioTrack(audio_track);
    audio_file->finalize();

    return operation_result;
}

int32_t AudioLoopbackOperation::loopbackLoopDualThread(const android::sp<android::AudioRecord>& audio_record,
                                                       const android::sp<android::AudioTrack>& audio_track,
                                                       AudioFileInterface& audio_file) {
    const size_t buffer_size = calculateBufferSize();
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    const uint64_t max_bytes_to_record =
        (config_.duration_seconds > 0) ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                                                  static_cast<uint64_t>(kMaxAudioDataSize))
                                       : static_cast<uint64_t>(kMaxAudioDataSize);

    if (config_.duration_seconds > 0) {
        printf("Duplex audio started: Recording for %d seconds...\n", config_.duration_seconds);
    }
    printf("Duplex audio in progress (dual-thread mode). Press Ctrl+C to stop\n");
    ALOGI("Duplex audio in progress (dual-thread mode).");

    next_progress_report_ = bytes_per_second * kProgressReportInterval;
    buffer_queue_.reset();
    record_error_.store(false);
    play_error_.store(false);
    total_bytes_recorded_.store(0);
    total_bytes_played_.store(0);

    std::thread record_thread(&AudioLoopbackOperation::recordThread, this, audio_record, buffer_size,
                              max_bytes_to_record, &audio_file);

    std::thread play_thread(&AudioLoopbackOperation::playThread, this, audio_track, buffer_size);

    while (!signal_guard_.isExitRequested() && !record_error_.load() && !play_error_.load()) {
        uint64_t current_recorded = total_bytes_recorded_.load();
        if (current_recorded >= max_bytes_to_record) {
            break;
        }

        if (current_recorded >= next_progress_report_) {
            printf("Loopback ... , recorded %.2f seconds (%.2f MB), played %.2f seconds (%.2f MB)\n",
                   static_cast<float>(current_recorded) / bytes_per_second,
                   static_cast<float>(current_recorded) / (1024u * 1024u),
                   static_cast<float>(total_bytes_played_.load()) / bytes_per_second,
                   static_cast<float>(total_bytes_played_.load()) / (1024u * 1024u));
            next_progress_report_ += bytes_per_second * kProgressReportInterval;
            audio_file.updateHeader();
        }

        usleep(100000); // 100ms
    }

    buffer_queue_.stop();
    if (signal_guard_.isExitRequested() || record_error_.load()) {
        play_error_.store(true);
    }

    record_thread.join();
    play_thread.join();

    const uint64_t final_recorded = total_bytes_recorded_.load();
    const uint64_t final_played = total_bytes_played_.load();
    printf("Loopback audio completed: Total bytes recorded: %" PRIu64 ", Total bytes played: %" PRIu64
           ", File saved: %s\n",
           final_recorded, final_played, audio_file.getFilePath().c_str());

    return (record_error_.load() || play_error_.load()) ? -1 : 0;
}

void AudioLoopbackOperation::recordThread(const android::sp<android::AudioRecord>& audio_record,
                                          size_t buffer_size,
                                          uint64_t max_bytes,
                                          AudioFileInterface* audio_file) {
    std::vector<char> buffer(buffer_size);
    uint64_t total_recorded = 0;

    while (total_recorded < max_bytes && !signal_guard_.isExitRequested() && !play_error_.load()) {
        const ssize_t bytes_read = audio_record->read(buffer.data(), buffer_size);
        if (bytes_read < 0) {
            printf("Error: AudioRecord read failed: %zd\n", bytes_read);
            ALOGE("AudioRecord read failed: %zd", bytes_read);
            record_error_.store(true);
            return;
        }
        if (bytes_read == 0) {
            continue;
        }

        total_recorded += static_cast<uint64_t>(bytes_read);
        total_bytes_recorded_.store(total_recorded);

        if (audio_file->writeData(buffer.data(), static_cast<size_t>(bytes_read)) != static_cast<size_t>(bytes_read)) {
            printf("Error: Failed to save audio data to file\n");
            ALOGE("Failed to save audio data to file");
            record_error_.store(true);
            return;
        }

        std::vector<char> play_buffer(bytes_read);
        memcpy(play_buffer.data(), buffer.data(), static_cast<size_t>(bytes_read));
        buffer_queue_.push(std::move(play_buffer));
    }

    buffer_queue_.stop();
}

void AudioLoopbackOperation::playThread(const android::sp<android::AudioTrack>& audio_track, size_t buffer_size) {
    std::vector<char> buffer;

    while (!signal_guard_.isExitRequested() && !record_error_.load()) {
        if (!buffer_queue_.pop(buffer)) {
            break;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = buffer.size();
        while (bytes_written < bytes_to_write && !signal_guard_.isExitRequested()) {
            const ssize_t written = audio_track->write(buffer.data() + bytes_written, bytes_to_write - bytes_written);
            if (written < 0) {
                printf("Error: AudioTrack write failed: %zd\n", written);
                ALOGE("AudioTrack write failed: %zd", written);
                play_error_.store(true);
                // Stop the buffer queue to unblock the record thread
                buffer_queue_.stop();
                return;
            }
            bytes_written += static_cast<size_t>(written);
        }
        total_bytes_played_.fetch_add(static_cast<uint64_t>(bytes_written));
    }
}

/************************** SetParamsOperation Implementation ******************************/

SetParamsOperation::SetParamsOperation(const AudioConfig& config, const std::vector<int32_t>& params)
    : AudioOperation(config), target_params_(params) {}

int32_t SetParamsOperation::execute() {
    if (target_params_.empty()) {
        printf("Error: No parameters provided\n");
        return -1;
    }

    if constexpr (kEnableSetParams) {
        printf("SetParams operation started with %zu parameters\n", target_params_.size());
        for (size_t i = 0; i < target_params_.size(); ++i) {
            printf("  Parameter %zu: %d\n", i + 1, target_params_[i]);
        }

        int32_t operation_type = target_params_[0];
        switch (operation_type) {
        case 1:
            if (target_params_.size() >= 2) {
                int32_t usage_value = target_params_[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usage_value);
                printf("Setting open_source with usage: %d\n", usage);
                audio_param_manager_.setOpenSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for open_source\n");
            }
            break;
        case 2:
            if (target_params_.size() >= 2) {
                int32_t usage_value = target_params_[1];
                audio_usage_t usage = static_cast<audio_usage_t>(usage_value);
                printf("Setting close_source with usage: %d\n", usage);
                audio_param_manager_.setCloseSourceWithUsage(usage);
            } else {
                printf("Error: Audio usage parameter is required for close_source\n");
            }
            break;
        default:
            printf("Error: Unknown primary parameter %d (1=open_source, 2=close_source)\n", operation_type);
            return -1;
        }

        printf("SetParams operation completed\n");
        return 0;
    } else {
        printf("Error: SetParams operation is disabled (kEnableSetParams=false)\n");
        printf("To enable, set kEnableSetParams to true in audio_test_client.h and rebuild\n");
        return -1;
    }
}

/************************** AudioOperationFactory Implementation ******************************/

std::unique_ptr<AudioOperation> AudioOperationFactory::createOperation(AudioMode mode, const AudioConfig& config) {
    switch (mode) {
    case AudioMode::kRecord:
        return std::make_unique<AudioRecordOperation>(config);
    case AudioMode::kPlay:
        return std::make_unique<AudioPlayOperation>(config);
    case AudioMode::kLoopback:
        return std::make_unique<AudioLoopbackOperation>(config);
    case AudioMode::kSetParams:
        return std::make_unique<SetParamsOperation>(config, config.set_params);
    default:
        printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
        return nullptr;
    }
}

/************************** CommandLineParser Implementation ******************************/

void CommandLineParser::parseArguments(int argc, char** argv, AudioMode& mode, AudioConfig& config) {
    int32_t opt = 0;
    while ((opt = getopt(argc, argv, "m:s:r:c:f:I:u:O:F:d:P:T:h:")) != -1) {
        switch (opt) {
        case 'm':
            mode = static_cast<AudioMode>(atoi(optarg));
            break;
        case 's':
            config.input_source = static_cast<audio_source_t>(atoi(optarg));
            break;
        case 'r':
            config.sample_rate = atoi(optarg);
            break;
        case 'c':
            config.channel_count = atoi(optarg);
            break;
        case 'f':
            config.format = AudioUtils::parseFormatOption(atoi(optarg));
            break;
        case 'I':
            config.input_flag = static_cast<audio_input_flags_t>(atoi(optarg));
            break;
        case 'd':
            config.duration_seconds = atoi(optarg);
            break;
        case 'u':
            config.usage = static_cast<audio_usage_t>(atoi(optarg));
            break;
        case 'O':
            config.output_flag = static_cast<audio_output_flags_t>(atoi(optarg));
            break;
        case 'F':
            config.min_frame_count = atoi(optarg);
            break;
        case 'P':
            if (mode == AudioMode::kPlay) {
                config.play_file_path = optarg;
            } else if ((mode == AudioMode::kRecord) || (mode == AudioMode::kLoopback)) {
                config.record_file_path = optarg;
            }
            break;
        case 'T':
            config.record_file_format = (atoi(optarg) == 1) ? AudioFileFormat::kRawPcm : AudioFileFormat::kWav;
            break;
        case 'h':
            showHelp();
            exit(0);
        default:
            showHelp();
            exit(-1);
        }
    }

    if (mode == AudioMode::kSetParams) {
        for (int32_t i = optind; i < argc; ++i) {
            std::string arg_str(argv[i]);
            size_t start = 0;
            size_t end = arg_str.find(',');
            while (end != std::string::npos) {
                if (end > start) {
                    config.set_params.emplace_back(std::stoi(arg_str.substr(start, end - start)));
                }
                start = end + 1;
                end = arg_str.find(',', start);
            }
            if (start < arg_str.length()) {
                config.set_params.emplace_back(std::stoi(arg_str.substr(start)));
            }
        }
    } else {
        if (optind < argc) {
            if (mode == AudioMode::kPlay) {
                config.play_file_path = argv[optind];
            } else if ((mode == AudioMode::kRecord) || (mode == AudioMode::kLoopback)) {
                config.record_file_path = argv[optind];
            }
        }
    }
}

void CommandLineParser::showHelp() {
    const char* help_text = R"(
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
  -I{inputFlag}       Set audio input flag (default: 0)
                       0: NONE, 1: FAST, 4: RAW
                       (See audio-hal-enums.h for full list)
  -d{duration}        Set recording duration(s) (0 = unlimited)
  -T{fileFormat}      Record file format (default: 0)
                       0: WAV format (.wav) - with header
                       1: Raw PCM format (.pcm) - no header, pure audio data

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
                       Note: Content type is automatically set based on usage type
  -O{outputFlag}      Set audio output flag (default: 0)
                       0: NONE, 1: DIRECT, 4: FAST, 8: DEEP_BUFFER
                       (See audio-hal-enums.h for full list)

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
  Record WAV:   audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20
  Record PCM:   audio_test_client -m0 -s1 -r48000 -c2 -f1 -I0 -F960 -d20 -T1
  Play:         audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav
  Play PCM:     audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.pcm -r48000 -c2 -f1
  Loopback:     audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
  SetParams:    audio_test_client -m100 1,1
)";
    puts(help_text);
}

/************************** Main Function ******************************/

int main(int argc, char** argv) {
    AudioMode mode = AudioMode::kInvalid;
    AudioConfig config;

    printf("Audio Test Client %s Start...\n", AUDIO_TEST_CLIENT_VERSION);
    CommandLineParser::parseArguments(argc, argv, mode, config);

    std::unique_ptr<AudioOperation> operation = AudioOperationFactory::createOperation(mode, config);
    if (!operation) {
        CommandLineParser::showHelp();
        return -1;
    }

    return operation->execute();
}
