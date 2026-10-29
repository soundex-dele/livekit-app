#pragma once
#include <QSettings>
#include <memory>
#include <QJsonArray>
#include "playlist.hpp"

namespace player_demo {
class Settings {
 public:
  explicit Settings(const QString& file = {});

  QVariant value(const QString& key, const QVariant& fallback = {}) const {
    return settings_->value(key, fallback);
  }

  void set(const QString& key, const QVariant& value) {
    settings_->setValue(key, value);
  }

  void sync() {
    settings_->sync();
  }

  void remember(const QString& path, qint64 position, qint64 duration, bool ended);
  qint64 resumePosition(const QString& path) const;
  void clearHistory();

  int historySize() const {
    return history_.size();
  }

 private:
  static QString keyFor(const QString&);
  std::unique_ptr<QSettings> settings_;
  QJsonArray history_;
};
}  // namespace player_demo
