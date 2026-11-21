import QtQuick 2.15

// ChatBubble.qml
    Item {
        id: root
        property bool isUser
        property string messageText
        // width приходит из делегата ListView (chatList.width - 32)

        implicitHeight: bubble.implicitHeight   // делегат растёт от пузыря

        Rectangle {
            id: bubble
            anchors.left: root.isUser ? undefined : parent.left
            anchors.right: root.isUser ? parent.right : undefined
            width: Math.min(parent.width * 0.8, msg.implicitWidth + 24)
            implicitHeight: msg.implicitHeight + 20   // 20 = два отступа

            TextEdit {
                id: msg
                readOnly: true
                selectByMouse: true
                textFormat: Text.MarkdownText
                wrapMode: TextEdit.Wrap
                width: bubble.width - 24
                height: implicitHeight      // <-- ключевая строка
                text: root.messageText
                color: root.isUser ? "#1f4fa3" : "#26251f"
            }
        }
    }
