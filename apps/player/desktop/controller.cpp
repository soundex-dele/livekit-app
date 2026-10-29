#include "controller.hpp"
#include <algorithm>

namespace player_demo {
using namespace std::chrono_literals;

Controller::Controller(std::unique_ptr<AudioOutput> audio, QObject* parent)
    : QObject(parent), audio_(audio ? std::move(audio) : std::make_unique<QtAudioOutput>()) {
  timer_.setInterval(8);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &Controller::update);
  timer_.start();
}

Controller::~Controller() {
  timer_.stop();
  player_.cancel_pending_io();
}

void Controller::resetOutput(bool clearImage) {
  audio_->reset();
  if (clearImage) {
    image_ = {};
    emit frameChanged(image_);
  }
}

void Controller::command(Operation operation, std::future<void> pending) {
  operation_ = operation;
  pending_ = std::move(pending);
}

void Controller::open(const QString& path, qint64 resumeUs) {
  if (closing_ || path.isEmpty())
    return;
  nextSeek_.reset();
  stopRequested_ = false;
  autoplay_ = false;
  if (busy()) {
    nextOpen_ = std::make_pair(path, resumeUs);
    if (operation_ == Operation::open)
      player_.cancel_pending_io();
  } else
    startOpen(path, resumeUs);
}

void Controller::startOpen(const QString& path, qint64 resumeUs) {
  audio_->close();
  resetOutput(true);
  subtitles.clear();
  subtitleError.clear();
  path_ = path;
  error_.clear();
  failureEmitted_ = false;
  endEmitted_ = false;
  resumeUs_ = std::max<qint64>(0, resumeUs);
  opening_ = player_.open(path.toUtf8().toStdString());
  operation_ = Operation::open;
  emit changed();
}

void Controller::togglePlay() {
  if (closing_ || busy())
    return;
  const auto state = snapshot().state;
  if (state == sdk::PlayerState::playing) {
    audio_->suspend(true);
    command(Operation::pause, player_.pause());
  } else if (state == sdk::PlayerState::ready || state == sdk::PlayerState::paused ||
             state == sdk::PlayerState::ended) {
    if (state == sdk::PlayerState::ended)
      resetOutput(true);
    endEmitted_ = false;
    command(Operation::play, player_.play());
  }
}

void Controller::seek(qint64 positionUs) {
  if (closing_ || !error_.isEmpty() || path_.isEmpty())
    return;
  nextSeek_ = std::max<qint64>(0, positionUs);
}

void Controller::stop() {
  if (closing_ || path_.isEmpty() || !error_.isEmpty())
    return;
  nextSeek_.reset();
  autoplay_ = false;
  stopRequested_ = true;
}

void Controller::close() {
  if (closing_)
    return;
  closing_ = true;
  autoplay_ = false;
  nextOpen_.reset();
  nextSeek_.reset();
  audio_->close();
  resetOutput(true);
  audio_->suspend(true);
  command(Operation::close, player_.close());
}

void Controller::setVolume(int value, bool muted) {
  audio_->setVolume(muted ? 0.f : std::clamp(value, 0, 100) / 100.f);
}

bool Controller::loadSubtitles(const QString& path) {
  if (busy() || closing_ || snapshot().state == sdk::PlayerState::closed || !error_.isEmpty()) {
    subtitleError = QStringLiteral("请先打开媒体文件，并等待操作完成。");
    return false;
  }
  try {
    subtitles.load(path.toUtf8().toStdString());
    subtitleError.clear();
    emit changed();
    return true;
  } catch (const std::exception& e) {
    subtitleError = QString::fromUtf8(e.what());
    return false;
  }
}

bool Controller::drained() const {
  return !busy() && snapshot().state == sdk::PlayerState::ended && audio_->drained();
}

void Controller::reportFailure(const QString& message) {
  error_ = message;
  autoplay_ = false;
  nextSeek_.reset();
  stopRequested_ = false;
  resetOutput(true);
  // Release failed or consumer-blocked graphs before a queued retry.
  command(Operation::recover, player_.close());
  if (!failureEmitted_) {
    failureEmitted_ = true;
    emit failed(message);
  }
}

void Controller::update() {
  if (finished_)
    return;
  try {
    if (operation_ == Operation::open && opening_.wait_for(0ms) == std::future_status::ready) {
      const auto media = opening_.get();
      operation_ = Operation::none;
      if (!nextOpen_) {
        generation_ = snapshot().generation;
        if (media.has_audio)
          audio_->open();
        // Validate stored positions against the file's current duration.
        if (!nextSeek_ && resumeUs_ > 0 && media.duration_us > 0 &&
            resumeUs_ < media.duration_us - 10000000)
          nextSeek_ = resumeUs_;
        autoplay_ = !stopRequested_;
        emit opened();
      }
    } else if (busy() && operation_ != Operation::open &&
               pending_.wait_for(0ms) == std::future_status::ready) {
      const auto completed = operation_;
      pending_.get();
      operation_ = Operation::none;
      if (completed == Operation::close) {
        finished_ = true;
        timer_.stop();
        emit closed();
        return;
      }
      if (generation_ != snapshot().generation) {
        resetOutput(true);
        generation_ = snapshot().generation;
      }
      if (completed == Operation::play)
        audio_->suspend(false);
    }
    if (!busy() && !closing_ && nextOpen_) {
      const auto next = *nextOpen_;
      nextOpen_.reset();
      startOpen(next.first, next.second);
    }
    if (!busy() && !closing_ && error_.isEmpty()) {
      if (stopRequested_) {
        stopRequested_ = false;
        resetOutput(true);
        command(Operation::stop, player_.stop());
      } else if (nextSeek_) {
        auto position = *nextSeek_;
        nextSeek_.reset();
        const auto duration = snapshot().media.duration_us;
        if (duration > 0)
          position = std::min<qint64>(position, duration);
        resetOutput(true);
        endEmitted_ = false;
        command(Operation::seek, player_.seek(std::chrono::microseconds(position)));
      } else if (autoplay_) {
        autoplay_ = false;
        command(Operation::play, player_.play());
      }
    }
    if (!busy() && !closing_ && error_.isEmpty()) {
      const auto state = snapshot();
      if (state.state == sdk::PlayerState::failed)
        reportFailure(QString::fromStdString(state.error));
      else {
        if (generation_ != state.generation) {
          resetOutput(true);
          generation_ = state.generation;
        }
        audio_->pump();
        // A finite budget also keeps silent/no-device output responsive.
        for (int i = 0; i < 64 && audio_->canAccept(); ++i) {
          auto frame = player_.poll_audio();
          if (!frame)
            break;
          if (frame->generation == generation_)
            audio_->enqueue(*frame);
        }
        std::optional<sdk::media::VideoFrame> latest;
        for (int i = 0; i < 8; ++i) {
          auto frame = player_.poll_video();
          if (!frame)
            break;
          if (frame->generation == generation_)
            latest = std::move(frame);
        }
        if (latest && latest->rgba) {
          // A detached QImage survives SDK generation changes and retained UI paints.
          image_ = QImage(latest->rgba->data(), latest->width, latest->height, latest->stride,
                          QImage::Format_RGBA8888)
                       .copy();
          emit frameChanged(image_);
        }
        if (drained() && !endEmitted_) {
          endEmitted_ = true;
          emit ended();
        }
      }
    }
  } catch (const std::exception& e) {
    operation_ = Operation::none;
    if (closing_) {
      finished_ = true;
      timer_.stop();
      emit closed();
      return;
    }
    if (!nextOpen_)
      reportFailure(QString::fromUtf8(e.what()));
  }
  emit changed();
  emit ticked();
}
}  // namespace player_demo
