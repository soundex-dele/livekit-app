#include "controller.hpp"
#include <algorithm>

namespace player_demo {
using namespace std::chrono_literals;

Controller::Controller(std::unique_ptr<AudioOutput> audio, QObject* parent)
    : QObject(parent), audio_(audio ? std::move(audio) : std::make_unique<QtAudioOutput>()) {
  dispatch_state_->owner = this;
  dispatch_ = [state = dispatch_state_](std::function<void()> task) {
    std::lock_guard lock(state->mutex);
    if (!state->owner ||
        !QMetaObject::invokeMethod(state->owner, std::move(task), Qt::QueuedConnection)) {
      throw sdk::SDKError(sdk::ErrorDomain::lifecycle, sdk::ErrorCode::cancelled,
                          "Player controller was destroyed");
    }
  };
  sdk::PlayerCallbacks callbacks;
  callbacks.video = [output = output_](const auto& frame) {
    std::lock_guard lock(output->mutex);
    output->video = frame;
  };
  callbacks.audio = [output = output_](const auto& frame) {
    std::lock_guard lock(output->mutex);
    output->audio = frame;
    output->requested = false;
  };
  player_.set_callbacks(std::move(callbacks), {sdk::PlayerAudioDelivery::on_demand});
  timer_.setInterval(8);
  timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &Controller::update);
  timer_.start();
}

Controller::~Controller() {
  timer_.stop();
  {
    std::lock_guard lock(dispatch_state_->mutex);
    dispatch_state_->owner = nullptr;
  }
  player_.set_callbacks({});
  player_.cancel_pending_io();
}

void Controller::resetOutput(bool clearImage) {
  audio_->reset();
  {
    // Preserve a new-generation paused preview which may arrive before the
    // control completion reaches Qt. Old-generation frames never reach devices.
    const auto generation = player_.snapshot().generation;
    std::lock_guard lock(output_->mutex);
    if (output_->video && output_->video->generation != generation) {
      output_->video.reset();
    }
    if (output_->audio && output_->audio->generation != generation) {
      output_->audio.reset();
    }
  }
  if (clearImage) {
    image_ = {};
    emit frameChanged(image_);
  }
}

void Controller::command(Operation operation, sdk::AsyncResult<void> pending) {
  operation_ = operation;
  completion_ready_ = false;
  const auto serial = ++operation_serial_;
  std::move(pending).then(dispatch_, [this, serial](sdk::AsyncResult<void> ready) {
    if (serial != operation_serial_) {
      return;
    }
    pending_ = std::move(ready);
    completion_ready_ = true;
    update();
  });
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
  operation_ = Operation::open;
  completion_ready_ = false;
  const auto serial = ++operation_serial_;
  player_.open(path.toUtf8().toStdString())
      .then(dispatch_, [this, serial](sdk::AsyncResult<sdk::PlayerMediaInfo> ready) {
        if (serial != operation_serial_) {
          return;
        }
        opening_ = std::move(ready);
        completion_ready_ = true;
        update();
      });
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
  std::lock_guard lock(output_->mutex);
  return !busy() && snapshot().state == sdk::PlayerState::ended && !output_->audio &&
         audio_->drained();
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
    if (operation_ == Operation::open && completion_ready_) {
      completion_ready_ = false;
      const auto media = opening_.get();
      {
        std::lock_guard lock(output_->mutex);
        output_->requested = false;
      }
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
    } else if (busy() && operation_ != Operation::open && completion_ready_) {
      completion_ready_ = false;
      const auto completed = operation_;
      pending_.get();
      operation_ = Operation::none;
      if (completed == Operation::pause || completed == Operation::seek ||
          completed == Operation::stop || completed == Operation::recover ||
          completed == Operation::close) {
        std::lock_guard lock(output_->mutex);
        output_->requested = false;
      }
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
        std::optional<sdk::media::AudioFrame> audio;
        std::optional<sdk::media::VideoFrame> latest;
        {
          std::lock_guard lock(output_->mutex);
          if (output_->audio && output_->audio->generation != generation_) {
            output_->audio.reset();
          }
          if (output_->video && output_->video->generation != generation_) {
            output_->video.reset();
          }
          if (audio_->canAccept()) {
            audio = std::move(output_->audio);
            output_->audio.reset();
          }
          latest = std::move(output_->video);
          output_->video.reset();
        }
        if (audio) {
          audio_->enqueue(*audio);
        }
        if (state.state == sdk::PlayerState::playing && audio_->canAccept()) {
          std::lock_guard lock(output_->mutex);
          if (!output_->audio && !output_->requested) {
            output_->requested = player_.request_audio(generation_);
          }
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
