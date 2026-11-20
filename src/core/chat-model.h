#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

// Одно сообщение чата: роль ("user"/"assistant") и текст.
// Текст ассистента при стриминге дозаполняется по мере прихода токенов.
struct ChatMessage {
    QString role;
    QString text;
};

class ChatModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,   // кто говорит
        TextRole                       // текст сообщения
    };

    explicit ChatModel(QObject *parent = nullptr);

    // --- QAbstractListModel ---
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool empty() const;

    // --- QML API ---
    Q_INVOKABLE void addMessage(const QString &role, const QString &text);
    Q_INVOKABLE void appendToLast(const QString &piece);   // для стриминга
    Q_INVOKABLE void clear();

signals:
    void emptyChanged();

private:
    std::vector<ChatMessage> m_messages;
};
