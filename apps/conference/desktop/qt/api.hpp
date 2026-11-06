#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <functional>
#include <chrono>

namespace meeting {
class Api : public QObject {
  Q_OBJECT
 public:
  explicit Api(QObject* parent = nullptr) : QObject(parent), network_(this) {}

  QString base = "http://127.0.0.1:8080", token, guest, organization, subject;
  qint64 media_policy = 0;
  using Done = std::function<void(QJsonValue, QString)>;
  void request(QString path, QJsonObject body, Done done, bool get = false,
               QString idempotency = {});
  void cancel();

  uint64_t epoch() const {
    return epoch_;
  }

 signals:
  void error(QString message);

 private:
  QNetworkAccessManager network_;
  QSet<QNetworkReply*> replies_;
  uint64_t epoch_ = 0;
  QMap<QByteArray, QString> retryKeys_;
  QMap<QString, std::chrono::steady_clock::time_point> mediaDeadlines_;
};

QString error_text(QString code);
}  // namespace meeting
