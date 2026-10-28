#include "settings.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

namespace player_demo {
Settings::Settings(const QString& file) {
  if (file.isEmpty()) settings_ = std::make_unique<QSettings>("LiveKit", "LocalPlayer");
  else settings_ = std::make_unique<QSettings>(file, QSettings::IniFormat);
  history_ = QJsonDocument::fromJson(settings_->value("history").toByteArray()).array();
  while (history_.size() > 100) history_.removeLast();
}
QString Settings::keyFor(const QString& path) {
  QFileInfo info(path); auto key = info.canonicalFilePath();
  if (key.isEmpty()) key = info.absoluteFilePath();
#ifdef Q_OS_WIN
  key = key.toCaseFolded();
#endif
  return key;
}
void Settings::remember(const QString& path, qint64 position, qint64 duration, bool ended) {
  if (path.isEmpty()) return;
  const auto key = keyFor(path); QFileInfo info(path);
  for (int i = history_.size() - 1; i >= 0; --i) if (history_[i].toObject()["path"].toString() == key) history_.removeAt(i);
  const qint64 resume = !ended && duration > 10000000 && position < duration - 10000000 ? std::max<qint64>(0, position) : 0;
  history_.prepend(QJsonObject{{"path", key}, {"position", QString::number(resume)},
    {"size", QString::number(info.size())}, {"modified", QString::number(info.lastModified().toMSecsSinceEpoch())}});
  while (history_.size() > 100) history_.removeLast();
  set("history", QJsonDocument(history_).toJson(QJsonDocument::Compact));
}
qint64 Settings::resumePosition(const QString& path) const {
  if (!value("resumeEnabled", true).toBool()) return 0;
  const auto key = keyFor(path); QFileInfo info(path);
  for (const auto& value : history_) {
    const auto entry = value.toObject();
    if (entry["path"].toString() == key && entry["size"].toString() == QString::number(info.size()) &&
        entry["modified"].toString() == QString::number(info.lastModified().toMSecsSinceEpoch()))
      return entry["position"].toString().toLongLong();
  }
  return 0;
}
void Settings::clearHistory() { history_ = {}; settings_->remove("history"); settings_->sync(); }
}
