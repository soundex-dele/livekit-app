#include "api.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
namespace meeting {
QString error_text(QString c) {
  static const QMap<QString, QString> texts = {
      {"unauthorized", "登录已失效或账号密码不正确，请重新登录"},
      {"invalid_account",
       "用户名需为 3–32 位字母、数字或下划线，密码需为 10–128 字节"},
      {"account_unavailable", "该用户名不可用"},
      {"meeting_not_found", "没有找到这个会议，请检查会议号"},
      {"incorrect_password", "会议密码不正确"},
      {"meeting_full", "会议已达到 25 人上限"},
      {"meeting_locked", "主持人已锁定会议"},
      {"not_admitted", "正在等待主持人批准"},
      {"admission_denied", "本次入会申请已被拒绝或移出"},
      {"meeting_not_active", "会议尚未开始"},
      {"meeting_ended", "会议已经结束或取消"},
      {"sharing_busy", "已有其他成员正在共享"},
      {"membership_cleanup_pending", "上次离会仍在处理中，请稍后重新加入"},
      {"transfer_or_end_required", "请先转让主持人或结束会议"},
      {"invalid_host", "只能转让给在线的已登录成员"},
      {"forbidden", "你没有执行此操作的权限"},
      {"rate_limited", "操作过于频繁，请稍后重试"},
      {"service_unavailable", "服务暂时不可用，请稍后重试"}};
  return texts.value(
      c, c.isEmpty() ? QString("连接失败，请检查服务器设置与网络") : c);
}
void Api::cancel() {
  ++epoch_;
  retryKeys_.clear();
  auto pending = replies_;
  replies_.clear();
  for (auto *r : pending)
    r->abort();
}
void Api::request(QString path, QJsonObject body, Done done, bool get,
                  QString key) {
  QUrl url(base + path);
  if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
    done({}, "服务器地址必须是 HTTP 或 HTTPS 地址");
    return;
  }
  QNetworkRequest req(url);
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("X-AV-Client", "desktop");
  if (!token.isEmpty())
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
  if (!guest.isEmpty())
    req.setRawHeader("X-AV-Guest", guest.toUtf8());
  auto signature = QCryptographicHash::hash(
      path.toUtf8() + QJsonDocument(body).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256);
  if (!get) {
    if (key.isEmpty()) {
      key = retryKeys_.value(signature);
      if (key.isEmpty())
        key = QUuid::createUuid().toString(QUuid::WithoutBraces);
      if (retryKeys_.size() > 128)
        retryKeys_.clear();
      retryKeys_[signature] = key;
    }
    req.setRawHeader("Idempotency-Key", key.toUtf8());
  }
  req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                   QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                   QNetworkRequest::Manual);
  req.setTransferTimeout(10000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::ManualRedirectPolicy);
  auto *reply =
      get ? network_.get(req)
          : network_.post(req,
                          QJsonDocument(body).toJson(QJsonDocument::Compact));
  replies_.insert(reply);
  const auto epoch = epoch_;
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, done = std::move(done), epoch, signature] {
        replies_.remove(reply);
        if (epoch != epoch_) {
          reply->deleteLater();
          return;
        }
        auto data = reply->readAll();
        int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto doc = QJsonDocument::fromJson(data);
        QString error;
        if (status < 200 || status >= 300 ||
            reply->error() != QNetworkReply::NoError) {
          auto v = doc.object().value("error");
          error =
              error_text(v.isString() ? v.toString()
                                      : v.toObject().value("code").toString());
        } else if (doc.isNull())
          error = "服务器响应格式不正确";
        reply->deleteLater();
        if (epoch == epoch_ && status >= 200 && status < 500)
          retryKeys_.remove(signature);
        if (epoch == epoch_)
          done(doc.isArray() ? QJsonValue(doc.array())
                             : QJsonValue(doc.object()),
               error);
      });
}
} // namespace meeting
