import QtQuick 2.15

Rectangle {
    property bool isUser: false
    property string messageText: ""

    width: 200
    height: bubbleText.implicitHeight + 24
    radius: 10
    color: isUser ? "#dceaf7" : "#ffffff"
    border.color: "#e2dfd9"

    anchors.right: isUser ? parent.right : undefined
    anchors.left: isUser ? undefined : parent.left

    TextEdit {
        id: msgText
        readOnly: true
        selectByMouse: true              // выделение мышью — и Cmd+C заработал
        textFormat: Text.MarkdownText    // **жирный**, код, списки, заголовки
        wrapMode: TextEdit.Wrap
        color: model.role === "user" ? "#1f4fa3" : "#26251f"
        text: model.text
    }
}
