import QtQuick 2.15

Rectangle {
    property bool isUser: false
    property string messageText: ""

    width: 200
    height: bubbleText.implicitHeight + 24
    radius: 10
    color: isUser ? "#090d10" : "#58585b"

    anchors.right: isUser ? parent.right : undefined
    anchors.left: isUser ? undefined : parent.left

    Text {
        id: bubbleText
        anchors.fill: parent
        anchors.margins: 12
        wrapMode: Text.Wrap
        color: isUser ? "black" : "#464c70fd"
        text: messageText
    }
}
