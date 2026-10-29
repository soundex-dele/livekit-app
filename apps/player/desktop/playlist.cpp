#include "playlist.hpp"
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QColor>

namespace player_demo {
QVariant PlaylistModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= items_.size())
    return {};
  const auto& entry = items_[index.row()];
  if (role == Qt::DisplayRole)
    return QString(entry.id == currentId_ ? "正在播放 · " : "     ") +
           QFileInfo(entry.path).fileName();
  if (role == Qt::ToolTipRole)
    return entry.path + (entry.error.isEmpty() ? "" : "\n" + entry.error);
  if (role == Qt::AccessibleTextRole)
    return QFileInfo(entry.path).fileName() + (entry.error.isEmpty() ? "" : "，无法播放");
  if (role == Qt::ForegroundRole && !entry.error.isEmpty())
    return QColor("#ff9292");
  if (role == Qt::UserRole)
    return entry.path;
  return {};
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex& index) const {
  return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
                         : Qt::ItemIsDropEnabled;
}

QStringList PlaylistModel::mimeTypes() const {
  return {"application/x-livekit-player-item", "text/uri-list"};
}

QMimeData* PlaylistModel::mimeData(const QModelIndexList& indices) const {
  auto* data = new QMimeData;
  if (!indices.isEmpty())
    data->setData(mimeTypes()[0], item(indices[0].row()).id.toUtf8());
  return data;
}

bool PlaylistModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int,
                                 const QModelIndex& parent) {
  if (action == Qt::IgnoreAction)
    return true;
  if (data->hasFormat(mimeTypes()[0])) {
    const int source = rowForId(QString::fromUtf8(data->data(mimeTypes()[0])));
    const int destination = row >= 0 ? row : parent.isValid() ? parent.row() : rowCount();
    // Handled ourselves; returning false avoids QListView removing the source again.
    moveRows({}, source, 1, {}, destination);
    return false;
  }
  QStringList paths;
  for (const auto& url : data->urls())
    if (url.isLocalFile())
      paths << url.toLocalFile();
  if (paths.isEmpty())
    return false;
  emit filesDropped(paths);
  return true;
}

bool PlaylistModel::moveRows(const QModelIndex& sourceParent, int source, int count,
                             const QModelIndex& destParent, int destination) {
  if (sourceParent.isValid() || destParent.isValid() || count != 1 || source < 0 ||
      source >= items_.size() || destination < 0 || destination > items_.size() ||
      destination == source || destination == source + 1)
    return false;
  if (!beginMoveRows({}, source, source, {}, destination))
    return false;
  items_.move(source, destination > source ? destination - 1 : destination);
  endMoveRows();
  return true;
}

bool PlaylistModel::removeRows(int row, int count, const QModelIndex& parent) {
  if (parent.isValid() || row < 0 || count <= 0 || row + count > items_.size())
    return false;
  beginRemoveRows({}, row, row + count - 1);
  items_.remove(row, count);
  endRemoveRows();
  if (rowForId(currentId_) < 0)
    currentId_.clear();
  return true;
}

void PlaylistModel::append(const QStringList& paths) {
  if (paths.isEmpty())
    return;
  beginInsertRows({}, items_.size(), items_.size() + paths.size() - 1);
  for (const auto& path : paths)
    items_.append({QUuid::createUuid().toString(QUuid::WithoutBraces),
                   QFileInfo(path).absoluteFilePath(),
                   {}});
  endInsertRows();
}

void PlaylistModel::replace(const QStringList& paths) {
  beginResetModel();
  items_.clear();
  currentId_.clear();
  endResetModel();
  append(paths);
}

QStringList PlaylistModel::paths() const {
  QStringList paths;
  for (const auto& entry : items_)
    paths << entry.path;
  return paths;
}

PlaylistItem PlaylistModel::item(int row) const {
  return row >= 0 && row < items_.size() ? items_[row] : PlaylistItem{};
}

int PlaylistModel::rowForId(const QString& id) const {
  for (int i = 0; i < items_.size(); ++i)
    if (items_[i].id == id)
      return i;
  return -1;
}

void PlaylistModel::setCurrent(const QString& id) {
  currentId_ = id;
  if (!items_.isEmpty())
    emit dataChanged(index(0), index(items_.size() - 1));
}

void PlaylistModel::markError(const QString& id, const QString& error) {
  const int row = rowForId(id);
  if (row < 0)
    return;
  items_[row].error = error;
  emit dataChanged(index(row), index(row));
}

int PlaylistModel::nextRow(RepeatMode mode, bool failed) const {
  if (items_.isEmpty())
    return -1;
  const int current = rowForId(currentId_);
  if (current < 0)
    return 0;
  if (mode == RepeatMode::One && !failed)
    return current;
  if (current + 1 < items_.size())
    return current + 1;
  return mode == RepeatMode::All ? 0 : -1;
}
}  // namespace player_demo
