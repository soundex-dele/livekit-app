#include "api.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QNetworkCookie>
#include <QTimer>
#include <algorithm>

namespace meeting {
QString error_text(QString c) {
  static const QMap<QString, QString> texts = {
      {"unauthorized", "登录已失效或账号密码不正确，请重新登录"},
      {"invalid_account", "用户名需为 3–32 位字母、数字或下划线，密码需为 10–128 字节"},
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
  return texts.value(c, c.isEmpty() ? QString("连接失败，请检查服务器设置与网络") : c);
}

void Api::cancel() {
  ++epoch_;
  retryKeys_.clear();
  mediaDeadlines_.clear();
  auto pending = replies_;
  replies_.clear();
  for (auto* r : pending)
    r->abort();
}

void Api::request(QString path, QJsonObject body, Done done, bool get, QString key) {
  const QString legacy = "/api/v1/conference/";
  if (path.startsWith(legacy)) {
    path.replace(0, legacy.size(), "/api/v1/av/conference/");
    if (path.endsWith("/share")) {
      path.chop(5);
      path += body.value("enabled").toBool() ? "share-start" : "share-stop";
      body = {};
    }
    if (path.endsWith("/credentials")) {
      path.chop(11);
      const auto connect_path = path + "connect";
      request(connect_path, {},
              [this, done = std::move(done)](QJsonValue v, QString error) mutable {
                if (!error.isEmpty()) {
                  done({}, error);
                  return;
                }
                auto grant = v.toObject().value("grant_id").toString();
                request("/api/v1/av/rtc-grants/" + grant + "/credentials", {}, std::move(done));
              });
      return;
    }
    if (path.endsWith("/presence")) {
      path.chop(9);
      const auto snapshot_path = path;
      request(
          snapshot_path, {},
          [this, snapshot_path, done = std::move(done)](QJsonValue v, QString error) mutable {
            if (!error.isEmpty()) {
              done({}, error);
              return;
            }
            const auto status = v.toObject().value("status").toString();
            if (status == "removed" || status == "rejected" || status == "left") {
              done(v, {});
              return;
            }
            request(snapshot_path + "/heartbeat", {},
                    [done = std::move(done), v](QJsonValue, QString e) { done(v, e); });
          },
          true);
      return;
    }
  }
  QUrl url(base + path);
  if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
    done({}, "服务器地址必须是 HTTP 或 HTTPS 地址");
    return;
  }
  QNetworkRequest req(url);
  int timeout_ms = 10000;
  if (path.contains("/rtc-grants/")) {
    const auto now = std::chrono::steady_clock::now();
    if (!mediaDeadlines_.contains(path)) {
      mediaDeadlines_[path] = now + std::chrono::seconds(12);
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(mediaDeadlines_[path] - now).count();
    if (remaining <= 0) {
      mediaDeadlines_.remove(path);
      done({}, "媒体授权准备超时，请稍后重试");
      return;
    }
    timeout_ms = static_cast<int>(std::min<int64_t>(timeout_ms, remaining));
  }
  req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  req.setRawHeader("X-Foundation-Request", "1");
  if (!organization.isEmpty() && guest.isEmpty()) {
    req.setRawHeader("X-Organization-ID", organization.toUtf8());
  }
  if (!token.isEmpty())
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
  if (!guest.isEmpty())
    req.setRawHeader("X-AV-Resource-Credential", guest.toUtf8());
  auto signature =
      QCryptographicHash::hash(path.toUtf8() + QJsonDocument(body).toJson(QJsonDocument::Compact),
                               QCryptographicHash::Sha256);
  const bool sensitive =
      path.contains("/rtc-grants/") || path.endsWith("/join") || path.startsWith("/api/v1/auth/");
  if (!get && !sensitive) {
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
  req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  req.setTransferTimeout(timeout_ms);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  auto* reply = get ? network_.get(req)
                    : network_.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
  replies_.insert(reply);
  const auto epoch = epoch_;
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, done = std::move(done), epoch, signature, path, body, get, key]() mutable {
        replies_.remove(reply);
        if (epoch != epoch_) {
          reply->deleteLater();
          return;
        }
        auto data = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto doc = QJsonDocument::fromJson(data);
        QString error;
        if (status < 200 || status >= 300 || reply->error() != QNetworkReply::NoError) {
          auto v = doc.object().value("error");
          error = error_text(v.isString() ? v.toString() : v.toObject().value("code").toString());
        } else if (doc.isNull())
          error = "服务器响应格式不正确";
        if (path.contains("/rtc-grants/") && status == 409 &&
            doc.object().value("error").toObject().value("code") == "media_preparing" &&
            std::chrono::steady_clock::now() < mediaDeadlines_.value(path)) {
          reply->deleteLater();
          QTimer::singleShot(250, this,
                             [this, path, body, done = std::move(done), epoch, get, key]() mutable {
                               if (epoch == epoch_) {
                                 request(path, body, std::move(done), get, key);
                               }
                             });
          return;
        }
        mediaDeadlines_.remove(path);
        auto object = doc.object();
        if (status >= 200 && status < 300 && path.startsWith("/api/v1/auth/")) {
          for (const auto& cookie : QNetworkCookie::parseCookies(reply->rawHeader("Set-Cookie"))) {
            if (cookie.name() == "pf_session") {
              token = QString::fromUtf8(cookie.value());
              object["session_token"] = token;
            }
          }
          if (object.contains("user")) {
            subject = object.value("user").toObject().value("id").toString();
          }
        }
        if (status >= 200 && status < 300 && path == "/api/v1/av/conference/join") {
          subject = object.value("subject_id").toString();
          if (object.contains("credential")) {
            guest = object.value("credential").toString();
          }
        }
        if (status >= 200 && status < 300 && path.contains("/rtc-grants/")) {
          media_policy = qint64(object.value("policy_version").toDouble());
        }
        if (object.contains("meeting")) {
          auto merged = object.value("meeting").toObject();
          for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key() != "meeting") {
              merged[it.key()] = it.value();
            }
          }
          merged["self"] = subject;
          QJsonArray members;
          for (const auto& value : object.value("members").toArray()) {
            auto member = value.toObject();
            member["id"] = member.value("subject_id");
            member["status"] = member.value("state");
            member["online"] = member.value("state") == "active";
            if (member.value("subject_id").toString() == subject) {
              merged["status"] = member.value("state");
              merged["self_policy"] = member.value("policy_version");
            }
            members.append(member);
          }
          merged["members"] = members;
          if (merged.value("sharing_member").toString() == subject &&
              qint64(merged.value("self_policy").toDouble()) == media_policy) {
            merged["operations"] = QJsonArray{
                QJsonObject{{"kind", "share-on"}, {"member", subject}, {"state", "done"}}};
          }
          object = merged;
        }
        reply->deleteLater();
        if (epoch == epoch_ && status >= 200 && status < 500)
          retryKeys_.remove(signature);
        if (epoch == epoch_)
          done(path == "/api/v1/av/conference/meetings" && get ? object.value("items")
               : doc.isArray()                                 ? QJsonValue(doc.array())
                                                               : QJsonValue(object),
               error);
      });
}
}  // namespace meeting
