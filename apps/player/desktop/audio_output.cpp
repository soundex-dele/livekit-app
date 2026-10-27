#include "audio_output.hpp"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace player_demo {
QtAudioOutput::QtAudioOutput() = default;
QtAudioOutput::~QtAudioOutput() = default;
void QtAudioOutput::fail(const QString& reason) {
  warning_ = QStringLiteral("音频设备不可用，将静音播放：") + reason;
  if (sink_) sink_->reset();
  device_ = nullptr; sink_.reset(); pending_.clear();
}
void QtAudioOutput::close() {
  if (sink_) sink_->reset();
  device_ = nullptr; sink_.reset(); pending_.clear(); warning_.clear(); suspended_ = false;
}
void QtAudioOutput::open() {
  sink_.reset(); device_ = nullptr; pending_.clear();
  warning_.clear(); suspended_ = false;
  const auto device = QMediaDevices::defaultAudioOutput();
  if (device.isNull()) { fail(QStringLiteral("没有输出设备")); return; }
  QAudioFormat format;
  format.setSampleRate(48000); format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Float); int16_ = false;
  if (!device.isFormatSupported(format)) {
    format.setSampleFormat(QAudioFormat::Int16); int16_ = true;
    if (!device.isFormatSupported(format)) { fail(QStringLiteral("不支持 48 kHz 双声道 PCM")); return; }
  }
  sink_ = std::make_unique<QAudioSink>(device, format);
  sink_->setBufferSize(format.bytesForDuration(120000));
  sink_->setVolume(volume_);
  device_ = sink_->start();
  if (!device_) fail(QStringLiteral("无法启动输出"));
}
bool QtAudioOutput::canAccept() const {
  return !sink_ || (!suspended_ && pending_.empty() && sink_->bytesFree() > 0);
}
bool PcmBuffer::load(const livekit::sdk::media::AudioFrame& frame, bool int16) {
  if (!empty() || !frame.pcm || frame.pcm->size() > 256 * 1024) return false;
  if (int16) {
    data_.resize(static_cast<qsizetype>(frame.pcm->size() * sizeof(qint16)));
    for (size_t i = 0; i < frame.pcm->size(); ++i) {
      const float sample = std::isfinite((*frame.pcm)[i]) ? (*frame.pcm)[i] : 0.f;
      const auto value = static_cast<qint16>(std::lround(std::clamp(sample, -1.f, 1.f) * 32767.f));
      std::memcpy(data_.data() + i * sizeof(value), &value, sizeof(value));
    }
  } else {
    data_ = QByteArray(reinterpret_cast<const char*>(frame.pcm->data()),
                      static_cast<qsizetype>(frame.pcm->size() * sizeof(float)));
  }
  offset_ = 0; return true;
}
qint64 PcmBuffer::write(QIODevice& device, qint64 capacity) {
  const auto count = std::min<qint64>(capacity, data_.size() - offset_);
  if (count <= 0) return 0;
  const auto written = device.write(data_.constData() + offset_, count);
  if (written < 0) return written;
  offset_ += written;
  if (offset_ == data_.size()) clear();
  return written;
}
void QtAudioOutput::enqueue(const livekit::sdk::media::AudioFrame& frame) {
  if (!sink_) return;
  if (!pending_.load(frame, int16_)) { fail(QStringLiteral("音频帧超过输出缓存限制")); return; }
  pump();
}
void QtAudioOutput::pump() {
  if (!sink_) return;
  const auto error = sink_->error();
  if (error == QtAudio::OpenError || error == QtAudio::IOError || error == QtAudio::FatalError) {
    fail(QStringLiteral("输出设备断开或写入失败")); return;
  }
  if (suspended_ || pending_.empty()) return;
  if (pending_.write(*device_, sink_->bytesFree()) < 0) fail(QStringLiteral("PCM 写入失败"));
}
void QtAudioOutput::suspend(bool paused) {
  suspended_ = paused;
  if (sink_) { if (paused) sink_->suspend(); else sink_->resume(); }
}
void QtAudioOutput::reset() {
  pending_.clear(); suspended_ = false;
  if (sink_) {
    sink_->reset(); device_ = sink_->start();
    if (!device_) fail(QStringLiteral("重置输出失败"));
  }
}
void QtAudioOutput::setVolume(float volume) {
  volume_ = std::clamp(volume, 0.f, 1.f);
  if (sink_) sink_->setVolume(volume_);
}
bool QtAudioOutput::drained() const {
  return !sink_ || (pending_.empty() && sink_->state() == QtAudio::IdleState);
}
}
