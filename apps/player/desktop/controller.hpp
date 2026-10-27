#pragma once
#include "audio_output.hpp"
#include "subtitles.hpp"
#include "livekit/player.hpp"
#include <QImage>
#include <QObject>
#include <QTimer>
#include <optional>

namespace player_demo {
namespace sdk = livekit::sdk;
enum class Operation { none, open, play, pause, seek, stop, recover, close };
class Controller final : public QObject {
  Q_OBJECT
 public:
  explicit Controller(std::unique_ptr<AudioOutput> audio = {}, QObject* parent = nullptr);
  ~Controller() override;
  void open(const QString& path, qint64 resumeUs = 0);
  void togglePlay();
  void seek(qint64 positionUs);
  void stop();
  void close();
  void setVolume(int value, bool muted);
  bool loadSubtitles(const QString& path);
  bool busy() const { return operation_ != Operation::none; }
  bool closing() const { return closing_; }
  bool finished() const { return finished_; }
  bool drained() const;
  sdk::PlayerSnapshot snapshot() const { return player_.snapshot(); }
  const QString& path() const { return path_; }
  const QString& error() const { return error_; }
  QString warning() const { return audio_->warning(); }
  const QImage& image() const { return image_; }
  Subtitles subtitles;
  QString subtitleError;
 signals:
  void changed();
  void frameChanged(const QImage&);
  void opened();
  void ended();
  void failed(const QString&);
  void closed();
  void ticked();
 private:
  void update();
  void startOpen(const QString&, qint64);
  void command(Operation, std::future<void>);
  void resetOutput(bool clearImage);
  void reportFailure(const QString&);
  sdk::Player player_;
  std::unique_ptr<AudioOutput> audio_;
  QTimer timer_;
  Operation operation_ = Operation::none;
  std::future<sdk::PlayerMediaInfo> opening_;
  std::future<void> pending_;
  QString path_, error_;
  QImage image_;
  std::optional<std::pair<QString, qint64>> nextOpen_;
  std::optional<qint64> nextSeek_;
  qint64 resumeUs_ = 0;
  uint64_t generation_ = 0;
  bool closing_ = false, finished_ = false, autoplay_ = false, endEmitted_ = false;
  bool stopRequested_ = false, failureEmitted_ = false;
};
}
