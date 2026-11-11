#include "window.hpp"
#include "timeline.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace freecut {
using namespace livekit::sdk;

namespace {
std::string new_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

QString string(const std::string& s) {
  return QString::fromStdString(s);
}
}  // namespace

void Window::filter_assets() {
  if (!assets_ || !search_ || !asset_type_) {
    return;
  }
  for (int i = 0; i < assets_->count(); ++i) {
    auto* item = assets_->item(i);
    item->setHidden(!item->text().contains(search_->text(), Qt::CaseInsensitive) ||
                    (asset_type_->currentIndex() &&
                     item->data(Qt::UserRole + 1).toInt() != asset_type_->currentIndex()));
  }
}

void Window::build_effect_properties(QFormLayout* form) {
  property_form_ = form;
  for (int direction : {-1, 1}) {
    auto* button = new QPushButton(
        direction < 0 ? QStringLiteral("轨道下移") : QStringLiteral("轨道上移"), this);
    form->addRow(button);
    connect(button, &QPushButton::clicked, this, [this, direction] {
      auto p = document_.project();
      for (size_t i = 0; i < p.tracks.size(); ++i) {
        if (p.tracks[i].id != selected_track_) {
          continue;
        }
        const int target = int(i) + direction;
        if (target < 0 || target >= int(p.tracks.size()) || p.tracks[i].locked ||
            p.tracks[target].locked) {
          return;
        }
        std::swap(p.tracks[i], p.tracks[target]);
        edited(document_.edit(std::move(p)));
        return;
      }
    });
  }
  auto number = [&](const char* name, const QString& label, double low, double high,
                    double initial) {
    auto* widget = new QDoubleSpinBox(this);
    widget->setObjectName(name);
    widget->setRange(low, high);
    widget->setValue(initial);
    widget->setDecimals(3);
    widget->setSingleStep(.1);
    form->addRow(label, widget);
    effects_[name] = widget;
  };
  number("transition_frames", QStringLiteral("转场时长（帧）"), 2, 3600, 30);
  number("rotation", QStringLiteral("旋转（度）"), -36000, 36000, 0);
  number("speed", QStringLiteral("速度（保留音高）"), .25, 4, 1);
  number("fade_in", QStringLiteral("音频淡入（帧）"), 0, 100000000, 0);
  number("fade_out", QStringLiteral("音频淡出（帧）"), 0, 100000000, 0);
  number("brightness", QStringLiteral("亮度"), -2, 2, 0);
  number("contrast", QStringLiteral("对比度"), 0, 3, 1);
  number("saturation", QStringLiteral("饱和度"), 0, 3, 1);
  number("temperature", QStringLiteral("色温"), -1, 1, 0);
  text_content_ = new QPlainTextEdit(this);
  text_content_->setObjectName("textContent");
  text_content_->setMaximumHeight(100);
  form->addRow(QStringLiteral("文字内容"), text_content_);
  text_font_ = new QLineEdit(this);
  form->addRow(QStringLiteral("字体"), text_font_);
  text_color_ = new QLineEdit(this);
  text_color_->setPlaceholderText("#FFFFFF");
  form->addRow(QStringLiteral("文字颜色"), text_color_);
  text_alignment_ = new QComboBox(this);
  text_alignment_->addItems({QStringLiteral("左下"), QStringLiteral("居中下"),
                             QStringLiteral("右下"), QStringLiteral("左中"), QStringLiteral("居中"),
                             QStringLiteral("右中"), QStringLiteral("左上"),
                             QStringLiteral("居中上"), QStringLiteral("右上")});
  form->addRow(QStringLiteral("对齐"), text_alignment_);
  number("font_size", QStringLiteral("字号"), 8, 512, 48);
  number("outline", QStringLiteral("黑色描边"), 0, 20, 2);
  key_property_ = new QComboBox(this);
  const std::pair<const char*, const char*> properties[] = {
      {"位置 X", "x"},      {"位置 Y", "y"},       {"缩放", "scale"},
      {"旋转", "rotation"}, {"透明度", "opacity"}, {"音量", "volume"}};
  for (const auto& item : properties) {
    key_property_->addItem(QString::fromUtf8(item.first), item.second);
  }
  form->addRow(QStringLiteral("关键帧属性"), key_property_);
  key_interpolation_ = new QComboBox(this);
  key_interpolation_->addItems({QStringLiteral("线性"), QStringLiteral("缓入缓出")});
  form->addRow(QStringLiteral("插值"), key_interpolation_);
  auto* add = new QPushButton(QStringLiteral("在播放头添加 / 更新关键帧"), this);
  add->setObjectName("addKeyframe");
  form->addRow(add);
  connect(add, &QPushButton::clicked, this, [this] {
    auto p = document_.project();
    for (auto& t : p.tracks) {
      for (auto& c : t.clips) {
        if (c.id != selected_clip_ || position_ < c.position ||
            position_ > c.position + c.duration) {
          continue;
        }
        auto name = key_property_->currentData().toString().toStdString();
        double value = name == "x"         ? x_->value()
                       : name == "y"       ? y_->value()
                       : name == "scale"   ? scale_->value()
                       : name == "opacity" ? opacity_->value()
                       : name == "volume"  ? volume_->value()
                                           : effects_["rotation"]->value();
        auto found = std::find_if(c.animations.begin(), c.animations.end(),
                                  [&](const auto& a) { return a.property == name; });
        if (found == c.animations.end()) {
          c.animations.push_back({name, {}});
          found = c.animations.end() - 1;
        }
        const int64_t frame = position_ - c.position;
        auto& keys = found->keys;
        keys.erase(std::remove_if(keys.begin(), keys.end(),
                                  [&](const auto& k) { return k.frame == frame; }),
                   keys.end());
        keys.push_back({frame, value, EditorInterpolation(key_interpolation_->currentIndex())});
        std::sort(keys.begin(), keys.end(),
                  [](const auto& a, const auto& b) { return a.frame < b.frame; });
        edited(document_.edit(std::move(p)));
        return;
      }
    }
  });
  auto* remove = new QPushButton(QStringLiteral("移除所选属性关键帧"), this);
  form->addRow(remove);
  connect(remove, &QPushButton::clicked, this, [this] {
    auto p = document_.project();
    for (auto& t : p.tracks) {
      for (auto& c : t.clips) {
        if (c.id == selected_clip_) {
          const auto name = key_property_->currentData().toString().toStdString();
          c.animations.erase(std::remove_if(c.animations.begin(), c.animations.end(),
                                            [&](const auto& a) { return a.property == name; }),
                             c.animations.end());
        }
      }
    }
    edited(document_.edit(std::move(p)));
  });
}

void Window::update_effect_properties(const EditorClip* clip, bool enabled) {
  for (auto& entry : effects_) {
    entry.second->setEnabled(enabled);
  }
  for (QWidget* w :
       std::vector<QWidget*>{text_content_, text_font_, text_color_, text_alignment_}) {
    w->setEnabled(enabled && clip && !clip->text.content.empty());
  }
  const bool is_text = clip && !clip->text.content.empty();
  for (QWidget* w : std::vector<QWidget*>{text_content_, text_font_, text_color_, text_alignment_,
                                          effects_["font_size"], effects_["outline"]}) {
    property_form_->setRowVisible(w, is_text);
  }
  const auto& project = document_.project();
  auto asset = std::find_if(project.assets.begin(), project.assets.end(),
                            [&](const auto& a) { return clip && a.id == clip->asset_id; });
  const bool has_audio = asset != project.assets.end() && asset->audio;
  const bool has_video = asset != project.assets.end() && asset->video;
  for (const char* name : {"fade_in", "fade_out"}) {
    property_form_->setRowVisible(effects_[name], has_audio);
  }
  for (const char* name : {"brightness", "contrast", "saturation", "temperature"}) {
    property_form_->setRowVisible(effects_[name], has_video);
  }
  property_form_->setRowVisible(effects_["transition_frames"], has_video);
  property_form_->setRowVisible(effects_["speed"],
                                clip && !is_text && asset != project.assets.end() && !asset->image);
  if (!clip) {
    return;
  }
  const auto c =
      editor_evaluate(*clip, std::clamp<int64_t>(position_ - clip->position, 0, clip->duration));
  effects_["transition_frames"]->setValue(c.transition.duration > 0 ? c.transition.duration : 30);
  effects_["rotation"]->setValue(c.rotation);
  effects_["speed"]->setValue(c.speed);
  effects_["fade_in"]->setValue(c.fade_in);
  effects_["fade_out"]->setValue(c.fade_out);
  effects_["brightness"]->setValue(c.color.brightness);
  effects_["contrast"]->setValue(c.color.contrast);
  effects_["saturation"]->setValue(c.color.saturation);
  effects_["temperature"]->setValue(c.color.temperature);
  effects_["font_size"]->setValue(c.text.size);
  effects_["outline"]->setValue(c.text.outline);
  text_content_->setPlainText(string(c.text.content));
  text_font_->setText(string(c.text.font));
  text_color_->setText(QString("#%1").arg(c.text.color, 6, 16, QChar('0')));
  text_alignment_->setCurrentIndex(c.text.alignment - 1);
}

void Window::apply_effect_properties(EditorClip& c) {
  if (c.transition.duration > 0) {
    c.transition.duration = int64_t(effects_["transition_frames"]->value());
  }
  c.rotation = effects_["rotation"]->value();
  c.fade_in = int64_t(effects_["fade_in"]->value());
  c.fade_out = int64_t(effects_["fade_out"]->value());
  const double rate = effects_["speed"]->value();
  if (rate != c.speed) {
    const double scale = c.speed / rate;
    c.duration = std::max<int64_t>(1, std::llround(c.duration * scale));
    editor_retime(c, 0, scale);
    c.speed = rate;
  }
  c.color = {effects_["brightness"]->value(), effects_["contrast"]->value(),
             effects_["saturation"]->value(), effects_["temperature"]->value()};
  if (!c.text.content.empty()) {
    c.text.content = text_content_->toPlainText().toStdString();
    c.text.font = text_font_->text().toStdString();
    c.text.size = int(effects_["font_size"]->value());
    c.text.outline = effects_["outline"]->value();
    bool ok = false;
    auto color = text_color_->text().remove('#').toUInt(&ok, 16);
    if (ok && color <= 0xFFFFFF) {
      c.text.color = color;
    }
    c.text.alignment = text_alignment_->currentIndex() + 1;
  }
}

void Window::add_text() {
  auto p = document_.project();
  EditorTrack track;
  track.id = new_id();
  track.name = "文本";
  EditorClip clip;
  clip.id = new_id();
  clip.position = position_;
  clip.duration = std::max<int64_t>(1, 3LL * p.rate.numerator / p.rate.denominator);
  clip.text.content = "输入文字";
  track.clips.push_back(clip);
  selected_clip_ = clip.id;
  selected_track_ = track.id;
  p.tracks.push_back(std::move(track));
  edited(document_.edit(std::move(p)));
  timeline_->set_selected(clip.id);
}

void Window::set_transition(const std::string& effect) {
  auto p = document_.project();
  for (auto& t : p.tracks) {
    std::sort(t.clips.begin(), t.clips.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
    for (size_t i = 0; i < t.clips.size(); ++i) {
      auto& c = t.clips[i];
      if (c.id != selected_clip_) {
        continue;
      }
      if (effect.empty()) {
        c.transition = {};
      } else if (i + 1 < t.clips.size()) {
        c.transition = {t.clips[i + 1].id, effect, int64_t(effects_["transition_frames"]->value())};
      } else {
        notice_->setText(QStringLiteral("请选中同轨相邻片段中的前一个片段。"));
        return;
      }
      edited(document_.edit(std::move(p)));
      return;
    }
  }
}

void Window::copy_clips() {
  clipboard_.clear();
  clipboard_tracks_.clear();
  const auto selected = timeline_->selection();
  int first_track = -1;
  const auto& tracks = document_.project().tracks;
  for (size_t i = 0; i < tracks.size(); ++i) {
    for (const auto& c : tracks[i].clips) {
      if (selected.count(c.id) || (selected.empty() && c.id == selected_clip_)) {
        if (first_track < 0) {
          first_track = int(i);
        }
        clipboard_.push_back(c);
        clipboard_tracks_.push_back(int(i) - first_track);
      }
    }
  }
}

void Window::paste_clips() {
  if (clipboard_.empty()) {
    return;
  }
  auto p = document_.project();
  auto destination = std::find_if(p.tracks.begin(), p.tracks.end(),
                                  [&](const auto& t) { return t.id == selected_track_; });
  if (destination == p.tracks.end()) {
    return;
  }
  const size_t base = size_t(destination - p.tracks.begin());
  const auto first =
      std::min_element(clipboard_.begin(), clipboard_.end(), [](const auto& a, const auto& b) {
        return a.position < b.position;
      })->position;
  for (size_t i = 0; i < clipboard_.size(); ++i) {
    const size_t target = base + clipboard_tracks_[i];
    if (target >= p.tracks.size()) {
      notice_->setText(QStringLiteral("请先添加足够的目标轨道，以保留复制片段的轨道关系。"));
      return;
    }
    auto c = clipboard_[i];
    c.id = new_id();
    c.position = position_ + c.position - first;
    c.transition = {};
    p.tracks[target].clips.push_back(std::move(c));
  }
  edited(document_.edit(std::move(p)));
}

void Window::import_subtitles() {
  const auto path =
      QFileDialog::getOpenFileName(this, QStringLiteral("导入字幕"), {}, "SRT (*.srt)");
  if (!path.isEmpty()) {
    import_subtitles_path(path);
  }
}

bool Window::import_subtitles_path(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024) {
    notice_->setText(QStringLiteral("无法读取字幕文件"));
    return false;
  }
  QString content = QString::fromUtf8(file.readAll());
  content.remove(QChar(0xfeff));
  content.replace("\r\n", "\n");
  QRegularExpression pattern(
      "^(\\d+):(\\d{2}):(\\d{2})[,\\.](\\d{3})\\s*-->\\s*(\\d+):(\\d{2}):(\\d{2})[,\\.](\\d{3})$");
  auto p = document_.project();
  EditorTrack track;
  track.id = new_id();
  track.name = "字幕";
  for (const auto& block : content.split(QRegularExpression("\n\\s*\n"), Qt::SkipEmptyParts)) {
    auto lines = block.split('\n');
    if (lines.size() < 3) {
      notice_->setText(QStringLiteral("字幕格式无效"));
      return false;
    }
    auto match = pattern.match(lines[1].trimmed());
    if (!match.hasMatch()) {
      notice_->setText(QStringLiteral("字幕时间格式无效"));
      return false;
    }
    for (int offset : {1, 5}) {
      bool ok = false;
      const auto hours = match.captured(offset).toLongLong(&ok);
      if (!ok || hours > 27777 || match.captured(offset + 1).toInt() > 59 ||
          match.captured(offset + 2).toInt() > 59) {
        notice_->setText(QStringLiteral("字幕时间超出范围"));
        return false;
      }
    }
    auto frame = [&](int index) {
      return int64_t(std::llround(
          ((match.captured(index).toLongLong() * 3600 + match.captured(index + 1).toInt() * 60 +
            match.captured(index + 2).toInt()) *
               1000 +
           match.captured(index + 3).toInt()) *
          double(p.rate.numerator) / (1000 * p.rate.denominator)));
    };
    EditorClip c;
    c.id = new_id();
    c.position = frame(1);
    c.duration = frame(5) - c.position;
    c.text.content = lines.mid(2).join('\n').trimmed().toStdString();
    track.clips.push_back(c);
  }
  if (track.clips.empty()) {
    notice_->setText(QStringLiteral("字幕文件为空"));
    return false;
  }
  p.tracks.push_back(std::move(track));
  auto result = document_.edit(std::move(p));
  const bool success = bool(result);
  edited(std::move(result));
  return success;
}

void Window::export_subtitles() {
  const auto path =
      QFileDialog::getSaveFileName(this, QStringLiteral("导出字幕"), {}, "SRT (*.srt)");
  if (!path.isEmpty()) {
    export_subtitles_path(path);
  }
}

bool Window::export_subtitles_path(const QString& path) {
  std::vector<EditorClip> clips;
  const auto& p = document_.project();
  for (const auto& t : p.tracks) {
    for (const auto& c : t.clips) {
      if (!c.text.content.empty()) {
        clips.push_back(c);
      }
    }
  }
  std::sort(clips.begin(), clips.end(),
            [](const auto& a, const auto& b) { return a.position < b.position; });
  auto stamp = [&](int64_t frame) {
    auto ms = editor_time_us(frame, p.rate) / 1000;
    return QString("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QChar('0'))
        .arg(ms / 60000 % 60, 2, 10, QChar('0'))
        .arg(ms / 1000 % 60, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
  };
  QByteArray output;
  int i = 0;
  for (const auto& c : clips) {
    output += (QString::number(++i) + "\n" + stamp(c.position) + " --> " +
               stamp(c.position + c.duration) + "\n" + string(c.text.content) + "\n\n")
                  .toUtf8();
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) {
    notice_->setText(QStringLiteral("字幕导出失败"));
    return false;
  }
  return true;
}
}  // namespace freecut
