import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Один пузырь сообщения. Подстраивается под роль: пользователь — справа,
// ассистент — слева.
Rectangle {
    id: root

    required property string role
    required property string text

    readonly property bool isUser: role === "user"

    // Ширина пузыря — по содержимому, но не больше 80% ленты
    width: Math.min(parent.width * 0.8, content.implicitWidth + 24)

    radius: 12
    color: isUser ? "#2f6fed" : "#3a3a3e"

    Label {
        id: content
        anchors.fill: parent
        anchors.margins: 10
        text: root.text
        wrapMode: Label.Wrap
        color: "white"
        font.pointSize: 11
        // Перенос длинных слов без пробелов (ссылки, код)
        onLinkActivated: Qt.openUrlExternally(link)
    }
}
