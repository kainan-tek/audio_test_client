// Copyright 2026 Audio Test Client Authors
// SPDX-License-Identifier: GPL-3.0-only
//
// Audio Test Client - A professional Android system-level audio testing tool.
// Implementation using Android AudioRecord and AudioTrack Native APIs.

#include "audio_test_client.h"

#include <getopt.h>

/************************** AudioFileBase Implementation ******************************/

AudioFileBase::~AudioFileBase() noexcept {
    close();
}

void AudioFileBase::close() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

size_t AudioFileBase::readData(char* data, size_t size) {
    if (!file_stream_.is_open() || !is_valid_ || !data || size == 0) {
        return 0;
    }

    file_stream_.read(data, static_cast<std::streamsize>(size));
    return static_cast<size_t>(file_stream_.gcount());
}

const std::string& AudioFileBase::getFilePath() const {
    return file_path_;
}
int32_t AudioFileBase::getSampleRate() const {
    return sample_rate_;
}
int32_t AudioFileBase::getNumChannels() const {
    return num_channels_;
}

audio_format_t AudioFileBase::getAudioFormat() const {
    audio_format_t format = AudioUtils::bitsPerSampleToAudioFormat(bits_per_sample_);
    return (format != AUDIO_FORMAT_INVALID) ? format : AUDIO_FORMAT_PCM_16_BIT;
}

/************************** WavFile Implementation ******************************/

bool WavFile::Header::write(std::ostream& out) const {
    out.write(reinterpret_cast<const char*>(this), sizeof(Header));
    return out.good();
}

bool WavFile::Header::read(std::istream& in) {
    in.read(reinterpret_cast<char*>(this), sizeof(Header));
    return !in.fail();
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
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
    bits_per_sample_ = bits_per_sample;
    file_stream_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_stream_.is_open()) {
        return false;
    }

    std::copy_n("RIFF", 4, header_.riff_id);
    std::copy_n("WAVE", 4, header_.wave_id);
    std::copy_n("fmt ", 4, header_.fmt_id);
    std::copy_n("data", 4, header_.data_id);

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

    if (!header_.write(file_stream_)) {
        printf("Error: Failed to write WAV header\n");
        file_stream_.close();
        return false;
    }

    is_valid_ = true;

    return file_stream_.good();
}

bool WavFile::openForReading(const std::string& file_path) {
    file_path_ = file_path;
    file_stream_.open(file_path_, std::ios::binary | std::ios::in);
    if (!file_stream_.is_open()) {
        return false;
    }

    if (!header_.read(file_stream_)) {
        printf("Error: Failed to read WAV header\n");
        file_stream_.close();
        return false;
    }
    if (std::memcmp(header_.riff_id, "RIFF", 4) != 0 || std::memcmp(header_.wave_id, "WAVE", 4) != 0 ||
        std::memcmp(header_.fmt_id, "fmt ", 4) != 0 || std::memcmp(header_.data_id, "data", 4) != 0) {
        printf("Error: Invalid WAV file format\n");
        file_stream_.close();
        return false;
    }
    if (header_.fmt_size < 16 || header_.audio_format != 1 || header_.num_channels == 0 || header_.sample_rate == 0 ||
        header_.bits_per_sample == 0 ||
        (header_.bits_per_sample != 8 && header_.bits_per_sample != 16 && header_.bits_per_sample != 24 &&
         header_.bits_per_sample != 32)) {
        printf("Error: Invalid WAV format parameters (unsupported bits_per_sample=%u)\n", header_.bits_per_sample);
        file_stream_.close();
        return false;
    }

    sample_rate_ = static_cast<int32_t>(header_.sample_rate);
    num_channels_ = static_cast<int32_t>(header_.num_channels);
    bits_per_sample_ = header_.bits_per_sample;
    remaining_data_ = header_.data_size;
    is_valid_ = true;
    return file_stream_.good();
}

size_t WavFile::writeData(const char* data, size_t size) {
    if (!file_stream_.is_open() || !is_valid_ || !data || size == 0) {
        return 0;
    }

    if (size > UINT32_MAX || header_.data_size > UINT32_MAX - size) {
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

size_t WavFile::readData(char* data, size_t size) {
    // 只读 data 区，防止越过 data chunk 尾部把元数据当音频播出。
    size = std::min(size, remaining_data_);
    if (size == 0) {
        return 0;
    }
    const size_t n = AudioFileBase::readData(data, size);
    remaining_data_ -= n;
    return n;
}

void WavFile::updateHeader() {
    if (!file_stream_.is_open() || !is_valid_) {
        return;
    }
    const auto current_pos = file_stream_.tellp();

    file_stream_.seekp(0, std::ios::beg);
    if (!file_stream_.good()) {
        printf("Error: Failed to seek to header position\n");
        file_stream_.seekp(current_pos);
        return;
    }

    if (!header_.write(file_stream_)) {
        printf("Error: Failed to write WAV header\n");
        file_stream_.seekp(current_pos);
        return;
    }

    file_stream_.flush();
    file_stream_.seekp(current_pos);
}

void WavFile::finalize() {
    if (file_stream_.is_open() && is_valid_) {
        updateHeader();
        file_stream_.close();
    }
}

audio_format_t WavFile::getAudioFormat() const {
    if (header_.audio_format != 1) {
        printf("Error: Unsupported WAV audio format: %u (only PCM format=1 is supported)\n", header_.audio_format);
        return AUDIO_FORMAT_INVALID;
    }
    return AudioFileBase::getAudioFormat();
}

/************************** RawPcmFile Implementation ******************************/

bool RawPcmFile::createForWriting(const std::string& file_path,
                                  int32_t sample_rate,
                                  int32_t num_channels,
                                  uint32_t bits_per_sample) {
    file_path_ = file_path;
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
    bits_per_sample_ = bits_per_sample;

    file_stream_.open(file_path_, std::ios::binary | std::ios::out | std::ios::trunc);
    is_valid_ = file_stream_.is_open();
    return is_valid_;
}

bool RawPcmFile::openForReading(const std::string& file_path) {
    file_path_ = file_path;
    file_stream_.open(file_path_, std::ios::binary | std::ios::in);
    is_valid_ = file_stream_.is_open();
    return is_valid_;
}

size_t RawPcmFile::writeData(const char* data, size_t size) {
    if (!file_stream_.is_open() || !is_valid_ || !data || size == 0) {
        return 0;
    }

    file_stream_.write(data, static_cast<std::streamsize>(size));
    return file_stream_.good() ? size : 0;
}

/************************** AudioUtils Implementation ******************************/

audio_format_t AudioUtils::parseFormatOption(const int v) {
    switch (v) {
        case 0:
            printf("Using default format: PCM 16-bit\n");
            return AUDIO_FORMAT_PCM_16_BIT;
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

audio_format_t AudioUtils::bitsPerSampleToAudioFormat(uint32_t bits_per_sample) {
    switch (bits_per_sample) {
        case 8:
            return AUDIO_FORMAT_PCM_8_BIT;
        case 16:
            return AUDIO_FORMAT_PCM_16_BIT;
        case 24:
            return AUDIO_FORMAT_PCM_24_BIT_PACKED;
        case 32:
            // PCM_8_24_BIT left-justifies to 32-bit scale, round-trips as PCM_32_BIT (WAV stores bit depth only).
            return AUDIO_FORMAT_PCM_32_BIT;
        default:
            printf("Error: Unsupported PCM bit depth: %u\n", bits_per_sample);
            return AUDIO_FORMAT_INVALID;
    }
}

AudioUtils::UsageInfo AudioUtils::getUsageInfo(audio_usage_t usage) {
    switch (usage) {
        case AUDIO_USAGE_UNKNOWN:
        case AUDIO_USAGE_MEDIA:
        case AUDIO_USAGE_GAME:
            return {AUDIO_STREAM_MUSIC, AUDIO_CONTENT_TYPE_MUSIC};

        case AUDIO_USAGE_ASSISTANCE_ACCESSIBILITY:
            return {AUDIO_STREAM_ACCESSIBILITY, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
            return {AUDIO_STREAM_MUSIC, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_VOICE_COMMUNICATION:
        case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
            return {AUDIO_STREAM_VOICE_CALL, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_ALARM:
            return {AUDIO_STREAM_ALARM, AUDIO_CONTENT_TYPE_SONIFICATION};

        case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
            return {AUDIO_STREAM_RING, AUDIO_CONTENT_TYPE_SONIFICATION};

        case AUDIO_USAGE_NOTIFICATION:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_REQUEST:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_INSTANT:
        case AUDIO_USAGE_NOTIFICATION_COMMUNICATION_DELAYED:
        case AUDIO_USAGE_NOTIFICATION_EVENT:
            return {AUDIO_STREAM_NOTIFICATION, AUDIO_CONTENT_TYPE_SONIFICATION};

        case AUDIO_USAGE_ASSISTANT:
            return {AUDIO_STREAM_ASSISTANT, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_CALL_ASSISTANT:
            return {AUDIO_STREAM_CALL_ASSISTANT, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
            return {AUDIO_STREAM_SYSTEM, AUDIO_CONTENT_TYPE_SONIFICATION};

        case AUDIO_USAGE_VIRTUAL_SOURCE:
            printf("Warning: VIRTUAL_SOURCE usage mapped to STREAM_MUSIC (virtual audio processing)\n");
            return {AUDIO_STREAM_MUSIC, AUDIO_CONTENT_TYPE_SPEECH};

        case AUDIO_USAGE_EMERGENCY:
        case AUDIO_USAGE_SAFETY:
        case AUDIO_USAGE_VEHICLE_STATUS:
        case AUDIO_USAGE_ANNOUNCEMENT:
            printf("Warning: Usage %d has no direct stream type mapping, using STREAM_SYSTEM\n", usage);
            return {AUDIO_STREAM_SYSTEM, AUDIO_CONTENT_TYPE_SONIFICATION};

        default:
            printf("Warning: Unknown audio usage %d, defaulting to STREAM_MUSIC\n", usage);
            return {AUDIO_STREAM_MUSIC, AUDIO_CONTENT_TYPE_MUSIC};
    }
}

audio_stream_type_t AudioUtils::usageToStreamType(audio_usage_t usage) {
    return getUsageInfo(usage).stream_type;
}

audio_content_type_t AudioUtils::usageToContentType(audio_usage_t usage) {
    return getUsageInfo(usage).content_type;
}

std::string AudioUtils::getFormatTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_now;
    if (localtime_r(&time_t_now, &tm_now) == nullptr) {
        return std::string("00000000_00.00.00");
    }
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H.%M.%S", &tm_now);
    return std::string(buffer);
}

std::string AudioUtils::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm tm_now;
    if (localtime_r(&time_t_now, &tm_now) == nullptr) {
        return std::string("00:00:00.000");
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
                  static_cast<int>(ms.count()));
    return std::string(buffer);
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
    const std::string ext = AudioUtils::getAudioFileExtension(format);
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "/data/record_%dHz_%dch_%" PRIu32 "bit_%s%s", sample_rate,
             channel_count, bits_per_sample, format_time.c_str(), ext.c_str());
    return std::string(buffer);
}

std::vector<int32_t> AudioUtils::parseIntList(const std::string& str) {
    std::vector<int32_t> result;
    auto parseOne = [&str, &result](size_t start, size_t end) -> bool {
        if (start >= end)
            return true;
        try {
            result.emplace_back(std::stoi(str.substr(start, end - start)));
            return true;
        } catch (const std::exception& e) {
            printf("Error: Failed to parse parameter '%s': %s\n", str.substr(start, end - start).c_str(), e.what());
            return false;
        }
    };

    size_t start = 0;
    size_t end = str.find(',');
    while (end != std::string::npos) {
        if (!parseOne(start, end))
            return {};
        start = end + 1;
        end = str.find(',', start);
    }
    if (!parseOne(start, str.length()))
        return {};
    return result;
}

std::vector<char> AudioUtils::createAudioBuffer(size_t requested_size) {
    static constexpr size_t kMinBufferSize = 480;
    static constexpr size_t kMaxBufferSize = 64 * 1024 * 1024;

    size_t validated_size = requested_size;
    if (validated_size > kMaxBufferSize) {
        printf("Warning: Requested buffer size %zu exceeds max %zu, clamped\n", requested_size, kMaxBufferSize);
        validated_size = kMaxBufferSize;
    }
    validated_size = std::max(kMinBufferSize, validated_size);

    try {
        return std::vector<char>(validated_size);
    } catch (const std::bad_alloc& e) {
        printf("Error: Failed to allocate buffer of size %zu: %s\n", validated_size, e.what());
        return {};
    }
}

std::unique_ptr<AudioFileBase> AudioUtils::createAudioFile(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::kWav:
            return std::make_unique<WavFile>();
        case AudioFileFormat::kRawPcm:
            return std::make_unique<RawPcmFile>();
        default:
            return nullptr;
    }
}

AudioFileFormat AudioUtils::detectFileFormat(const std::string& file_path) {
    if (file_path.size() >= 4) {
        std::string ext = file_path.substr(file_path.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".wav") {
            return AudioFileFormat::kWav;
        }
        if (ext == ".pcm" || ext == ".raw") {
            return AudioFileFormat::kRawPcm;
        }
    }
    return AudioFileFormat::kWav;
}

std::string AudioUtils::getAudioFileExtension(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::kWav:
            return ".wav";
        case AudioFileFormat::kRawPcm:
            return ".pcm";
        default:
            return ".wav";
    }
}

/************************** SignalGuard Implementation ******************************/

volatile sig_atomic_t SignalGuard::s_exit_requested_ = 0;

SignalGuard::SignalGuard() {
    s_exit_requested_ = 0;
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

SignalGuard::~SignalGuard() noexcept {
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool SignalGuard::isExitRequested() {
    return s_exit_requested_ != 0;
}

void SignalGuard::signalHandler(int sig) {
    // Only use async-signal-safe operations here
    if (sig == SIGINT || sig == SIGTERM) {
        s_exit_requested_ = 1;
    }
}

/************************** AudioParameterManager Implementation ******************************/

const android::String8 AudioParameterManager::kParamOpenSource = android::String8("open_source");
const android::String8 AudioParameterManager::kParamCloseSource = android::String8("close_source");

void AudioParameterManager::setOpenSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(kParamOpenSource, audioUsageToString(usage));
}

void AudioParameterManager::setCloseSourceWithUsage(audio_usage_t usage) {
    setSystemParameter(kParamCloseSource, audioUsageToString(usage));
}

void AudioParameterManager::setSystemParameter(const android::String8& key, const android::String8& value) {
    if (kEnableSetParams) {
        android::AudioParameter audio_param;
        audio_param.add(key, value);
        android::String8 param_string = audio_param.toString();
        android::AudioSystem::setParameters(param_string);
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

bool ThreadSafeBufferQueue::push(std::vector<char>&& buffer) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [this] { return queue_.size() < max_buffers_ || stopped_; });
    if (stopped_) {
        return false;
    }
    queue_.push(std::move(buffer));
    not_empty_.notify_one();
    return true;
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

/************************** AudioOperation Implementation ******************************/

AudioOperation::AudioOperation(const AudioConfig& config) : config_(config) {}

size_t AudioOperation::calculateBufferSize(size_t actual_frame_count) const {
    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    // Use half of the internal buffer to reduce latency
    return (actual_frame_count / 2) * config_.channel_count * bytes_per_sample;
}

uint64_t AudioOperation::calculateBytesPerSecond() const {
    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    return static_cast<uint64_t>(config_.sample_rate) * config_.channel_count * bytes_per_sample;
}

size_t AudioOperation::resolveFrameCount(bool is_fast_path, size_t min_frame_count) const {
    const size_t frame_count = config_.frame_count;

    // Explicit -F: pass through as-is; let the HAL negotiate/reject (test tool needs boundary probing).
    if (frame_count > 0) {
        printf("Using frame_count=%zu (%.1fms)\n", frame_count, frame_count * 1000.0 / config_.sample_rate);
        return frame_count;
    }

    // No -F: pick a safe default; the HAL still decides the actual value.
    if (is_fast_path) {
        const size_t fast_frame_count = static_cast<size_t>((config_.sample_rate * 20) / 1000);  // 20ms
        printf("FAST path default frame_count=%zu (%.1fms)\n", fast_frame_count,
               fast_frame_count * 1000.0 / config_.sample_rate);
        return fast_frame_count;
    }

    // Non-fast: system minimum, with a 20ms fallback.
    const size_t min_frames_for_20ms = static_cast<size_t>((config_.sample_rate * 20) / 1000);
    const size_t default_frame_count = std::max(min_frame_count, min_frames_for_20ms);
    printf("Using system min frame_count=%zu (%.1fms)\n", default_frame_count,
           default_frame_count * 1000.0 / config_.sample_rate);
    return default_frame_count;
}

bool AudioOperation::validateAudioParameters() const {
    // Validate sample rate (8000 ~ 192000 Hz)
    if (config_.sample_rate < 8000 || config_.sample_rate > 192000) {
        printf("Error: Sample rate must be between 8000 and 192000 Hz, got %d\n", config_.sample_rate);
        return false;
    }
    // Validate channel count (1 ~ 16)
    if (config_.channel_count < 1 || config_.channel_count > 16) {
        printf("Error: Channel count must be between 1 and 16, got %d\n", config_.channel_count);
        return false;
    }
    // Validate audio format
    if (config_.format == AUDIO_FORMAT_INVALID) {
        printf("Error: Invalid audio format\n");
        return false;
    }
    // Validate duration
    if (config_.duration_seconds < 0) {
        printf("Error: Duration cannot be negative: %d\n", config_.duration_seconds);
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

    size_t min_frame_count = 0;
    if (android::AudioRecord::getMinFrameCount(&min_frame_count, config_.sample_rate, config_.format, channel_mask) !=
        android::NO_ERROR) {
        printf("Warning: Cannot get min frame count, using default value\n");
    } else {
        printf("getMinFrameCount=%zu (%.2fms)\n", min_frame_count, min_frame_count * 1000.0 / config_.sample_rate);
    }
    const size_t frame_count = resolveFrameCount((config_.input_flag & AUDIO_INPUT_FLAG_FAST) != 0, min_frame_count);

    printf(
        "Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
        "inputFlag=0x%x, frameCount=%zu (%.2fms)\n",
        config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask,
        config_.input_flag, frame_count, frame_count * 1000.0 / config_.sample_rate);
    ALOGI(
        "Initialize AudioRecord: source=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
        "inputFlag=0x%x, frameCount=%zu (%.2fms)\n",
        config_.input_source, config_.sample_rate, config_.channel_count, config_.format, channel_mask,
        config_.input_flag, frame_count, frame_count * 1000.0 / config_.sample_rate);

    android::content::AttributionSourceState attribution_source = createAttributionSource();
    audio_attributes_t attributes{};
    attributes.source = config_.input_source;
    audio_record = android::sp<android::AudioRecord>::make(attribution_source);
    if (audio_record->set(config_.input_source, config_.sample_rate, config_.format, channel_mask, frame_count, nullptr,
#ifndef ANDROID_API_14_PLUS
                          nullptr,  // callback argument removed in Android 14+
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

    const size_t actual_frame_count = audio_record->frameCount();
    printf("AudioRecord initialized successfully, actual frameCount=%zu (%.2fms)\n", actual_frame_count,
           actual_frame_count * 1000.0 / config_.sample_rate);
    return true;
}

bool AudioOperation::initializeAudioTrack(android::sp<android::AudioTrack>& audio_track) {
    audio_channel_mask_t channel_mask = audio_channel_out_mask_from_count(config_.channel_count);

    audio_stream_type_t stream_type = AudioUtils::usageToStreamType(config_.usage);
    size_t min_frame_count = 0;
    if (android::AudioTrack::getMinFrameCount(&min_frame_count, stream_type, config_.sample_rate) !=
        android::NO_ERROR) {
        printf("Warning: Cannot get min frame count using streamType, using default value\n");
    } else {
        printf("getMinFrameCount=%zu (%.2fms)\n", min_frame_count, min_frame_count * 1000.0 / config_.sample_rate);
    }
    const size_t frame_count = resolveFrameCount((config_.output_flag & AUDIO_OUTPUT_FLAG_FAST) != 0, min_frame_count);

    printf(
        "Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
        "outputFlag=0x%x, frameCount=%zu (%.2fms)\n",
        config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, config_.output_flag,
        frame_count, frame_count * 1000.0 / config_.sample_rate);
    ALOGI(
        "Initialize AudioTrack: usage=%d, sampleRate=%d, channelCount=%d, format=%d, channelMask=0x%x, "
        "outputFlag=0x%x, frameCount=%zu (%.2fms)\n",
        config_.usage, config_.sample_rate, config_.channel_count, config_.format, channel_mask, config_.output_flag,
        frame_count, frame_count * 1000.0 / config_.sample_rate);

    android::content::AttributionSourceState attribution_source = createAttributionSource();
    audio_attributes_t attributes{};
    attributes.usage = config_.usage;
    attributes.content_type = AudioUtils::usageToContentType(config_.usage);
    audio_track = android::sp<android::AudioTrack>::make(attribution_source);
    if (audio_track->set(AUDIO_STREAM_DEFAULT, config_.sample_rate, config_.format, channel_mask, frame_count,
                         config_.output_flag, nullptr,
#ifndef ANDROID_API_14_PLUS
                         nullptr,  // callback argument removed in Android 14+
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

    const size_t actual_frame_count = audio_track->frameCount();
    printf("AudioTrack initialized successfully, actual frameCount=%zu (%.2fms)\n", actual_frame_count,
           actual_frame_count * 1000.0 / config_.sample_rate);
    return true;
}

bool AudioOperation::setupAudioFileForRecording(std::unique_ptr<AudioFileBase>& audio_file) {
    size_t bytes_per_sample = audio_bytes_per_sample(config_.format);

    AudioFileFormat actual_format = config_.record_file_format;

    if (!config_.record_file_path.empty()) {
        AudioFileFormat detected_format = AudioUtils::detectFileFormat(config_.record_file_path);
        if (detected_format != config_.record_file_format) {
            printf("Note: File format determined by file extension: %s (ignoring -T option)\n",
                   detected_format == AudioFileFormat::kWav ? "WAV" : "RawPCM");
            actual_format = detected_format;
        }
    }

    config_.record_file_path = AudioUtils::makeRecordFilePath(
        config_.sample_rate, config_.channel_count, bytes_per_sample * 8, config_.record_file_path, actual_format);

    audio_file = AudioUtils::createAudioFile(actual_format);
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

bool AudioOperation::setupAudioFileForPlayback(std::unique_ptr<AudioFileBase>& audio_file) {
    if (config_.play_file_path.empty()) {
        printf("Error: No playback file path specified\n");
        return false;
    }

    AudioFileFormat format = AudioUtils::detectFileFormat(config_.play_file_path);
    audio_file = AudioUtils::createAudioFile(format);
    if (!audio_file) {
        printf("Error: Failed to create audio file handler\n");
        return false;
    }

    // Try to open the file directly - avoid TOCTOU race condition
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

    constexpr float kNorm16bit = 32768.0f;       // 2^15
    constexpr float kNorm24bit = 8388608.0f;     // 2^23
    constexpr float kNorm32bit = 2147483648.0f;  // 2^31
    constexpr float kDbFloor = -60.0f;

    const size_t bytes_per_sample = audio_bytes_per_sample(config_.format);
    if (size == 0 || bytes_per_sample == 0) {
        printf("Error: Invalid input size or bytes_per_sample\n");
        return;
    }

    // Ensure size is properly aligned with sample size to prevent buffer overread
    if (size % bytes_per_sample != 0) {
        printf("Note: Audio data size (%zu) not aligned with sample size (%zu), using aligned portion only\n", size,
               bytes_per_sample);
    }
    const size_t num_samples = size / bytes_per_sample;
    float peak_amplitude = 0.0f;
    if (bytes_per_sample == 2) {
        for (size_t i = 0; i < num_samples; ++i) {
            int16_t sample;
            std::memcpy(&sample, buffer + i * 2, sizeof(sample));
            peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(sample)) / kNorm16bit);
        }
    } else if (bytes_per_sample == 3) {
        // 24-bit packed PCM: 3 bytes per sample, little-endian
        const uint8_t* data = reinterpret_cast<const uint8_t*>(buffer);
        for (size_t i = 0; i < num_samples; ++i) {
            // Read 3 bytes as little-endian and sign-extend to 32-bit
            int32_t sample = static_cast<int32_t>(data[i * 3] | (data[i * 3 + 1] << 8) | (data[i * 3 + 2] << 16));
            // Sign extend from 24-bit to 32-bit
            if (sample & 0x800000) {
                sample |= 0xFF000000;
            }
            peak_amplitude = std::max(peak_amplitude, static_cast<float>(std::abs(sample)) / kNorm24bit);
        }
    } else if (bytes_per_sample == 4) {
        for (size_t i = 0; i < num_samples; ++i) {
            int32_t sample;
            std::memcpy(&sample, buffer + i * 4, sizeof(sample));
            // int64_t cast prevents signed overflow on abs(INT32_MIN) (UB).
            peak_amplitude =
                std::max(peak_amplitude, static_cast<float>(std::abs(static_cast<int64_t>(sample))) / kNorm32bit);
        }
    } else {
        // Skip unsupported byte depths (e.g. 8-bit PCM) silently to avoid "Error" spam.
        return;
    }

    const float db_level = peak_amplitude > 0.0f ? std::max(20.0f * std::log10(peak_amplitude), kDbFloor) : kDbFloor;
    const std::string timestamp = AudioUtils::getTimestamp();
    printf("[%s] Audio Level: %.1f dB, size: %zu bytes\n", timestamp.c_str(), db_level, size);
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
        // stop() discards buffered frames, cutting the tail; playLoop drains first, loopback does not.
        audio_track->stop();
        audio_param_manager_.setCloseSourceWithUsage(config_.usage);
    }
}

bool AudioOperation::reportProgress(const char* action,
                                    uint64_t total_bytes_processed,
                                    uint64_t bytes_per_second,
                                    std::chrono::steady_clock::time_point& last_time) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_time);
    if (elapsed.count() < kProgressReportInterval) {
        return false;
    }
    printf("%s ... , processed %.2f MB (%.2fs), elapsed %llds\n", action,
           static_cast<float>(total_bytes_processed) / (1024u * 1024u),
           static_cast<float>(total_bytes_processed) / bytes_per_second, elapsed.count());
    last_time = now;
    return true;
}

/************************** AudioRecordOperation Implementation ******************************/

AudioRecordOperation::AudioRecordOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioRecordOperation::execute() {
    if (!validateAudioParameters()) {
        return -1;
    }

    std::unique_ptr<AudioFileBase> audio_file;
    android::sp<android::AudioRecord> audio_record;

    if (!setupAudioFileForRecording(audio_file)) {
        return -1;
    }
    if (!initializeAudioRecord(audio_record)) {
        return -1;
    }
    if (!startAudioRecord(audio_record)) {
        return -1;
    }

    int32_t operation_result = recordLoop(audio_record, *audio_file);

    stopAudioRecord(audio_record);
    audio_file->finalize();

    return operation_result;
}

int32_t AudioRecordOperation::recordLoop(const android::sp<android::AudioRecord>& audio_record,
                                         AudioFileBase& audio_file) {
    const size_t buffer_size = calculateBufferSize(audio_record->frameCount());
    auto audio_buffer = AudioUtils::createAudioBuffer(buffer_size);
    if (audio_buffer.empty()) {
        printf("Error: Failed to create audio buffer\n");
        return -1;
    }

    if (config_.duration_seconds > 0) {
        printf("Recording for %d seconds...\n", config_.duration_seconds);
    }

    printf("Recording in progress. Press Ctrl+C to stop\n");
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    const uint64_t max_bytes_to_record =
        (config_.duration_seconds > 0) ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                                                  static_cast<uint64_t>(kMaxAudioDataSize))
                                       : static_cast<uint64_t>(kMaxAudioDataSize);

    auto last_progress_time = std::chrono::steady_clock::now();
    uint64_t total_bytes_read = 0;
    while (total_bytes_read < max_bytes_to_record && !SignalGuard::isExitRequested()) {
        const ssize_t bytes_read = audio_record->read(audio_buffer.data(), audio_buffer.size());
        if (bytes_read < 0) {
            if (SignalGuard::isExitRequested()) {
                break;
            }
            printf("Error: AudioRecord read failed: %zd\n", bytes_read);
            ALOGE("AudioRecord read failed: %zd", bytes_read);
            return -1;
        }
        if (bytes_read == 0) {
            continue;
        }
        total_bytes_read += static_cast<uint64_t>(bytes_read);

        if (audio_file.writeData(audio_buffer.data(), static_cast<size_t>(bytes_read)) !=
            static_cast<size_t>(bytes_read)) {
            printf("Error: Failed to save audio data to file\n");
            ALOGE("Failed to save audio data to file");
            return -1;
        }

        updateLevelMeter(audio_buffer.data(), static_cast<size_t>(bytes_read));
        if (reportProgress("Recording", total_bytes_read, bytes_per_second, last_progress_time)) {
            audio_file.updateHeader();
        }
    }

    printf("Recording finished: Recorded %" PRIu64 " bytes, File saved: %s\n", total_bytes_read,
           audio_file.getFilePath().c_str());

    return 0;
}

/************************** AudioPlayOperation Implementation ******************************/

AudioPlayOperation::AudioPlayOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioPlayOperation::execute() {
    std::unique_ptr<AudioFileBase> audio_file;
    android::sp<android::AudioTrack> audio_track;

    if (!setupAudioFileForPlayback(audio_file)) {
        return -1;
    }
    if (!validateAudioParameters()) {
        return -1;
    }
    if (!initializeAudioTrack(audio_track)) {
        return -1;
    }
    if (!startAudioTrack(audio_track)) {
        audio_param_manager_.setCloseSourceWithUsage(config_.usage);
        return -1;
    }

    int32_t operation_result = playLoop(audio_track, *audio_file);

    stopAudioTrack(audio_track);

    return operation_result;
}

int32_t AudioPlayOperation::playLoop(const android::sp<android::AudioTrack>& audio_track, AudioFileBase& audio_file) {
    const size_t buffer_size = calculateBufferSize(audio_track->frameCount());
    auto audio_buffer = AudioUtils::createAudioBuffer(buffer_size);
    if (audio_buffer.empty()) {
        printf("Error: Failed to create audio buffer\n");
        return -1;
    }

    printf("Playing in progress. Press Ctrl+C to stop\n");
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    auto last_progress_time = std::chrono::steady_clock::now();
    uint64_t total_bytes_played = 0;
    while (!SignalGuard::isExitRequested()) {
        const size_t bytes_read = audio_file.readData(audio_buffer.data(), audio_buffer.size());
        if (bytes_read == 0) {
            printf("End of file reached\n");
            break;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = bytes_read;
        while (bytes_written < bytes_to_write && !SignalGuard::isExitRequested()) {
            const ssize_t written =
                audio_track->write(audio_buffer.data() + bytes_written, bytes_to_write - bytes_written);
            if (written <= 0) {
                if (SignalGuard::isExitRequested()) {
                    break;
                }
                printf("Error: AudioTrack write failed: %zd\n", written);
                ALOGE("AudioTrack write failed: %zd", written);
                return -1;
            }
            bytes_written += static_cast<size_t>(written);
        }
        total_bytes_played += static_cast<uint64_t>(bytes_written);

        updateLevelMeter(audio_buffer.data(), bytes_written);
        reportProgress("Playing", total_bytes_played, bytes_per_second, last_progress_time);
    }

    // On EOF (not Ctrl+C), wait for the playback head to catch up so stop() doesn't cut the tail.
    if (!SignalGuard::isExitRequested()) {
        const size_t bytes_per_frame = config_.channel_count * audio_bytes_per_sample(config_.format);
        const uint64_t frames_written = total_bytes_played / bytes_per_frame;
        while (!SignalGuard::isExitRequested() && audio_track->getPlaybackHeadPosition() < frames_written) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    printf("Playback finished: Total bytes played: %" PRIu64 "\n", total_bytes_played);

    return 0;
}

/************************** AudioLoopbackOperation Implementation ******************************/

AudioLoopbackOperation::AudioLoopbackOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t AudioLoopbackOperation::execute() {
    if (!validateAudioParameters()) {
        return -1;
    }

    std::unique_ptr<AudioFileBase> audio_file;
    android::sp<android::AudioRecord> audio_record;
    android::sp<android::AudioTrack> audio_track;

    if (!setupAudioFileForRecording(audio_file)) {
        return -1;
    }
    if (!initializeAudioRecord(audio_record)) {
        return -1;
    }
    if (!initializeAudioTrack(audio_track)) {
        return -1;
    }
    if (!startAudioRecord(audio_record)) {
        return -1;
    }
    if (!startAudioTrack(audio_track)) {
        audio_param_manager_.setCloseSourceWithUsage(config_.usage);
        stopAudioRecord(audio_record);
        return -1;
    }

    int32_t operation_result = runLoopback(audio_record, audio_track, *audio_file);

    stopAudioRecord(audio_record);
    stopAudioTrack(audio_track);
    audio_file->finalize();

    return operation_result;
}

int32_t AudioLoopbackOperation::runLoopback(const android::sp<android::AudioRecord>& audio_record,
                                            const android::sp<android::AudioTrack>& audio_track,
                                            AudioFileBase& audio_file) {
    if (config_.duration_seconds > 0) {
        printf("Loopback audio started: Duration %d seconds...\n", config_.duration_seconds);
    }
    printf("Loopback audio in progress. Press Ctrl+C to stop\n");

    play_error_.store(false);
    total_bytes_played_.store(0);

    std::thread play_thread(&AudioLoopbackOperation::playThread, this, audio_track);

    const size_t buffer_size = calculateBufferSize(audio_record->frameCount());
    const uint64_t bytes_per_second = calculateBytesPerSecond();
    const uint64_t max_bytes = (config_.duration_seconds > 0)
                                   ? std::min(static_cast<uint64_t>(config_.duration_seconds) * bytes_per_second,
                                              static_cast<uint64_t>(kMaxAudioDataSize))
                                   : static_cast<uint64_t>(kMaxAudioDataSize);

    auto buffer = AudioUtils::createAudioBuffer(buffer_size);
    uint64_t total_recorded = 0;
    bool record_failed = buffer.empty();
    if (record_failed) {
        printf("Error: Failed to create audio buffer\n");
    }
    auto last_progress_time = std::chrono::steady_clock::now();
    while (!record_failed && total_recorded < max_bytes && !SignalGuard::isExitRequested() && !play_error_.load()) {
        const ssize_t bytes_read = audio_record->read(buffer.data(), buffer.size());
        if (bytes_read < 0) {
            if (SignalGuard::isExitRequested()) {
                break;
            }
            printf("Error: AudioRecord read failed: %zd\n", bytes_read);
            ALOGE("AudioRecord read failed: %zd", bytes_read);
            record_failed = true;
            break;
        }
        if (bytes_read == 0) {
            continue;
        }

        total_recorded += static_cast<uint64_t>(bytes_read);
        if (!buffer_queue_.push(std::vector<char>(buffer.data(), buffer.data() + bytes_read))) {
            break;
        }

        if (audio_file.writeData(buffer.data(), static_cast<size_t>(bytes_read)) != static_cast<size_t>(bytes_read)) {
            printf("Error: Failed to save audio data to file\n");
            ALOGE("Failed to save audio data to file");
            record_failed = true;
            break;
        }

        updateLevelMeter(buffer.data(), static_cast<size_t>(bytes_read));
        if (reportProgress("Loopback recording", total_recorded, bytes_per_second, last_progress_time)) {
            audio_file.updateHeader();
        }
    }

    // Stop the queue to unblock/drain playThread, then join; execute() stops audio_record on return.
    buffer_queue_.stop();
    // If the HAL stops consuming, playThread may block in write() and join() hangs (Ctrl+C can't
    // interrupt); SIGKILL is required, same as recordLoop's read().
    play_thread.join();

    const uint64_t final_played = total_bytes_played_.load();
    printf("Loopback audio completed: Total bytes recorded: %" PRIu64 ", Total bytes played: %" PRIu64
           ", File saved: %s\n",
           total_recorded, final_played, audio_file.getFilePath().c_str());

    return (record_failed || play_error_.load()) ? -1 : 0;
}

void AudioLoopbackOperation::playThread(const android::sp<android::AudioTrack>& audio_track) {
    std::vector<char> buffer;

    while (!SignalGuard::isExitRequested() && !play_error_.load()) {
        if (!buffer_queue_.pop(buffer)) {
            break;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = buffer.size();
        while (bytes_written < bytes_to_write && !SignalGuard::isExitRequested()) {
            const ssize_t written = audio_track->write(buffer.data() + bytes_written, bytes_to_write - bytes_written);
            if (written <= 0) {
                if (SignalGuard::isExitRequested()) {
                    break;
                }
                printf("Error: AudioTrack write failed: %zd\n", written);
                ALOGE("AudioTrack write failed: %zd", written);
                play_error_.store(true);
                break;
            }
            bytes_written += static_cast<size_t>(written);
        }
        total_bytes_played_.fetch_add(static_cast<uint64_t>(bytes_written));
    }

    // Unblock a push-blocked producer on any exit path (Ctrl+C or write error).
    buffer_queue_.stop();
}

/************************** SetParamsOperation Implementation ******************************/

SetParamsOperation::SetParamsOperation(const AudioConfig& config) : AudioOperation(config) {}

int32_t SetParamsOperation::execute() {
    if (config_.set_params.empty()) {
        printf("Error: No parameters provided\n");
        return -1;
    }

    printf("SetParams operation started with %zu parameters\n", config_.set_params.size());
    for (size_t i = 0; i < config_.set_params.size(); ++i) {
        printf("  Parameter %zu: %d\n", i + 1, config_.set_params[i]);
    }

    if (config_.set_params.size() < 2) {
        printf("Error: Audio usage parameter is required\n");
        return -1;
    }

    int32_t operation_type = config_.set_params[0];
    audio_usage_t usage = static_cast<audio_usage_t>(config_.set_params[1]);

    switch (operation_type) {
        case 1:
            printf("Setting open_source with usage: %d\n", usage);
            audio_param_manager_.setOpenSourceWithUsage(usage);
            break;
        case 2:
            printf("Setting close_source with usage: %d\n", usage);
            audio_param_manager_.setCloseSourceWithUsage(usage);
            break;
        default:
            printf("Error: Unknown primary parameter %d (1=open_source, 2=close_source)\n", operation_type);
            return -1;
    }

    printf("SetParams operation completed\n");
    return 0;
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
            if (!kEnableSetParams) {
                printf("Error: SetParams is disabled (set kEnableSetParams=true in audio_test_client.h and rebuild)\n");
                return nullptr;
            }
            return std::make_unique<SetParamsOperation>(config);
        default:
            printf("Error: Invalid mode specified: %d\n", static_cast<int>(mode));
            return nullptr;
    }
}

/************************** CommandLineParser Implementation ******************************/

void CommandLineParser::parseArguments(int argc, char** argv, AudioMode& mode, AudioConfig& config) {
    auto safeParseInt = [](const char* str, const char* opt_name, int fallback = 0) -> int {
        char* end = nullptr;
        errno = 0;
        long val = std::strtol(str, &end, 10);
        if (end == str || *end != '\0' || errno == ERANGE || val < INT_MIN || val > INT_MAX) {
            printf("Warning: Invalid value for %s: '%s', using %d\n", opt_name, str, fallback);
            return fallback;
        }
        return static_cast<int>(val);
    };

    std::string file_path_arg;
    int32_t opt = 0;
    while ((opt = getopt(argc, argv, "m:s:r:c:f:I:u:O:F:d:P:T:h")) != -1) {
        switch (opt) {
            case 'm':
                // 模式值不能回退 0：0 恰为 kRecord，笔误会意外启动录音；回退 kInvalid 走报错退出
                mode = static_cast<AudioMode>(safeParseInt(optarg, "-m", -1));
                break;
            case 's':
                config.input_source = static_cast<audio_source_t>(safeParseInt(optarg, "-s"));
                break;
            case 'r':
                config.sample_rate = safeParseInt(optarg, "-r");
                break;
            case 'c':
                config.channel_count = safeParseInt(optarg, "-c");
                break;
            case 'f':
                config.format = AudioUtils::parseFormatOption(safeParseInt(optarg, "-f"));
                break;
            case 'I':
                config.input_flag = static_cast<audio_input_flags_t>(safeParseInt(optarg, "-I"));
                break;
            case 'd':
                config.duration_seconds = safeParseInt(optarg, "-d");
                break;
            case 'u':
                config.usage = static_cast<audio_usage_t>(safeParseInt(optarg, "-u"));
                break;
            case 'O':
                config.output_flag = static_cast<audio_output_flags_t>(safeParseInt(optarg, "-O"));
                break;
            case 'F':
                config.frame_count = static_cast<size_t>(std::max(safeParseInt(optarg, "-F"), 0));
                break;
            case 'P':
                file_path_arg = optarg;
                break;
            case 'T':
                config.record_file_format =
                    (safeParseInt(optarg, "-T") == 1) ? AudioFileFormat::kRawPcm : AudioFileFormat::kWav;
                break;
            case 'h':
                showHelp();
                mode = AudioMode::kHelp;
                return;
            default:
                showHelp();
                mode = AudioMode::kInvalid;
                return;
        }
    }

    if (mode == AudioMode::kSetParams && optind < argc) {
        config.set_params = AudioUtils::parseIntList(argv[optind]);
    }

    if (!file_path_arg.empty()) {
        if (mode == AudioMode::kPlay) {
            config.play_file_path = file_path_arg;
        } else if (mode == AudioMode::kRecord || mode == AudioMode::kLoopback) {
            config.record_file_path = file_path_arg;
        }
    }
}

void CommandLineParser::showHelp() {
    const char* help_text = R"(
Audio Test Client - Record, Play and Loopback Testing Tool
Usage: audio_test_client -m{mode} [options]

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
                       4: AUDIO_FORMAT_PCM_8_24_BIT (24-bit PCM in 32-bit container, 8-bit padding)
                       6: AUDIO_FORMAT_PCM_24_BIT_PACKED (24-bit packed PCM)
  -I{inputFlag}       Set audio input flag (default: 0)
                       0: NONE, 1: FAST (low latency), 2: HW_HOTWORD, 4: RAW, 8: SYNC
                       Note: FAST path defaults to 20ms if -F not specified
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
                       0: NONE, 1: DIRECT, 2: PRIMARY, 4: FAST (low latency), 8: DEEP_BUFFER
                       Note: FAST path defaults to 20ms if -F not specified
                       (See audio-hal-enums.h for full list)

Common Options:
  -F{frameCount}      Set frame count (default: system selected, FAST path: 20ms)
  -P{filePath}        Audio file path (input for play, output for record/loopback)
                       Note: WAV playback requires standard 44-byte header PCM
  -h                  Show this help message

Set Params Options:
  Note: Disabled by default. Set kEnableSetParams=true in audio_test_client.h and rebuild.
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
  Play WAV:     audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.wav
  Play PCM:     audio_test_client -m1 -u1 -O0 -F960 -P/data/audio_test.pcm -r48000 -c2 -f1
  Loopback:     audio_test_client -m2 -s1 -r48000 -c2 -f1 -I0 -u1 -O0 -F960 -d20
  SetParams:    audio_test_client -m100 1,1
)";
    printf("%s", help_text);
}

/************************** Main Function ******************************/

int main(int argc, char** argv) {
    AudioMode mode = AudioMode::kInvalid;
    AudioConfig config;
    SignalGuard signal_guard;

    printf("Audio Test Client %s Start...\n", AUDIO_TEST_CLIENT_VERSION);
    CommandLineParser::parseArguments(argc, argv, mode, config);
    if (mode == AudioMode::kHelp) {
        return 0;
    }
    if (mode == AudioMode::kInvalid) {
        printf("Error: No valid mode specified. Use -h for help.\n");
        return -1;
    }

    std::unique_ptr<AudioOperation> operation = AudioOperationFactory::createOperation(mode, config);
    if (!operation) {
        printf("Error: Failed to create operation for mode %d\n", static_cast<int>(mode));
        return -1;
    }
    return operation->execute();
}
