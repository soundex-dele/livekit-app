#pragma once
#include <QAbstractListModel>
#include <QStringList>
#include <QUuid>

namespace player_demo {
enum class RepeatMode { Sequential = 0, One = 1, All = 2 };

struct PlaylistItem {
  QString id, path, error;
};

class PlaylistModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  explicit PlaylistModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex& parent = {}) const override {
    return parent.isValid() ? 0 : items_.size();
  }

  QVariant data(const QModelIndex&, int role) const override;
  Qt::ItemFlags flags(const QModelIndex&) const override;
  QStringList mimeTypes() const override;
  QMimeData* mimeData(const QModelIndexList&) const override;
  bool dropMimeData(const QMimeData*, Qt::DropAction, int, int, const QModelIndex&) override;

  Qt::DropActions supportedDropActions() const override {
    return Qt::MoveAction | Qt::CopyAction;
  }

  bool moveRows(const QModelIndex&, int, int, const QModelIndex&, int) override;
  bool removeRows(int row, int count, const QModelIndex& parent = {}) override;
  void append(const QStringList&);
  void replace(const QStringList&);
  QStringList paths() const;
  PlaylistItem item(int row) const;
  int rowForId(const QString&) const;
  void setCurrent(const QString& id);
  void markError(const QString& id, const QString& error);

  QString currentId() const {
    return currentId_;
  }

  int nextRow(RepeatMode mode, bool failed = false) const;
 signals:
  void filesDropped(const QStringList&);

 private:
  QList<PlaylistItem> items_;
  QString currentId_;
};
}  // namespace player_demo
