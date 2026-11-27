
import QtQuick 2.15

// ChatBubble.qml
Item {
    id: root

    property bool isUser
    property string messageText
    property string reasoningText
    property bool reasoningExpanded: false

    implicitHeight: bubble.implicitHeight

    Rectangle {
        id: bubble

        anchors.left: root.isUser ? undefined : parent.left
        anchors.right: root.isUser ? parent.right : undefined

        width: root.reasoningText.length > 0
            ? parent.width * 0.8
            : Math.min(parent.width * 0.8,
                        Math.max(120, msg.implicitWidth + 24))

        implicitHeight: content.implicitHeight + 20

        color: root.isUser ? "#e8efff" : "#f3f3f0"
        radius: 10

        Column {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12

            spacing: 8

            // Сворачиваемый заголовок reasoning
            Item {
                width: content.width
                height: 28

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Text {
                        text: root.reasoningExpanded ? "▼" : "▶"
                        color: "#777777"
                    }

                    Text {
                        text: "Размышления"
                        color: "#777777"
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.reasoningExpanded = !root.reasoningExpanded
                }
            }
            // Содержимое reasoning
            TextEdit {
                visible: root.reasoningExpanded &&
                         root.reasoningText.length > 0

                readOnly: true
                selectByMouse: true
                textFormat: Text.MarkdownText
                wrapMode: TextEdit.Wrap

                width: content.width
                height: implicitHeight

                text: root.reasoningText
                color: "#777777"
                font.italic: true
            }

            // Обычный ответ
            TextEdit {
                id: msg

                readOnly: true
                selectByMouse: true
                textFormat: Text.MarkdownText
                wrapMode: TextEdit.Wrap

                width: content.width
                height: implicitHeight

                text: root.messageText
                color: root.isUser ? "#1f4fa3" : "#26251f"
            }
        }
    }
}
