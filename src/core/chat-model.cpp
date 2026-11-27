#include "chat-model.h"

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(m_messages.size());
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
  if (!hasIndex(index.row(), 0, QModelIndex()))
    return {};

  const ChatMessage &msg = m_messages[index.row()];
  switch (role) {
  case RoleRole:
    return msg.role;
  case TextRole:
    return msg.text;
  }
  return {};
}

QHash<int, QByteArray> ChatModel::roleNames() const {
  return {{RoleRole, "role"}, {TextRole, "text"}};
}

bool ChatModel::empty() const { return m_messages.empty(); }

void ChatModel::addMessage(const QString &role, const QString &text) {
  const int row = static_cast<int>(m_messages.size());
  beginInsertRows({}, row, row);
  m_messages.push_back({role, text});
  endInsertRows();
  emit emptyChanged();
}

void ChatModel::appendToLast(const QString &piece) {
  if (m_messages.empty())
    return;

  m_messages.back().text += piece;
  const QModelIndex idx = index(static_cast<int>(m_messages.size()) - 1);
  emit dataChanged(idx, idx, {TextRole});
}

void ChatModel::clear() {
  beginResetModel();
  m_messages.clear();
  endResetModel();
  emit emptyChanged();
}
