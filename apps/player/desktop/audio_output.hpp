#pragma once
#include "livekit/media_core/frame.hpp"
#include <QByteArray>
#include <QString>
#include <memory>

class QAudioSink;
class QIODevice;
namespace player_demo {
// The controller also consumes audio with no device, so decoding never stalls.
class AudioOutput {
 public:
  virtual ~AudioOutput() = default;
  virtual void open() = 0;
  virtual void close() { reset(); }
  virtual bool canAccept() const = 0;
  virtual void enqueue(const livekit::sdk::media::AudioFrame&) = 0;
  virtual void pump() = 0;
  virtual void suspend(bool) = 0;
  virtual void reset() = 0;
  virtual void setVolume(float) = 0;
  virtual bool drained() const = 0;
  virtual QString warning() const = 0;
};
// One bounded PCM frame with byte-accurate partial-write retention.
class PcmBuffer {
 public:
  bool load(const livekit::sdk::media::AudioFrame&, bool int16);
  qint64 write(QIODevice&, qint64 capacity);
  void clear() { data_.clear(); offset_ = 0; }
  bool empty() const { return data_.isEmpty(); }
 private:
  QByteArray data_;
  qsizetype offset_ = 0;
};
class QtAudioOutput final : public AudioOutput {
 public:
  QtAudioOutput();
  ~QtAudioOutput() override;
  void open() override;
  void close() override;
  bool canAccept() const override;
  void enqueue(const livekit::sdk::media::AudioFrame&) override;
  void pump() override;
  void suspend(bool) override;
  void reset() override;
  void setVolume(float) override;
  bool drained() const override;
  QString warning() const override { return warning_; }
 private:
  void fail(const QString&);
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* device_ = nullptr;
  PcmBuffer pending_;
  bool int16_ = false, suspended_ = false;
  float volume_ = .8f;
  QString warning_;
};
}
