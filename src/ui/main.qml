import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    visible: true
    width: 1280
    height: 800
    title: "Silver Brain"
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        
        TextArea {
            id: chatArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: true
            wrapMode: TextArea.Wrap
        }
        
        RowLayout {
            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: "Type your message..."
                onAccepted: sendMessage()
            }
            
            Button {
                text: "Send"
                onClicked: sendMessage()
            }
        }
    }
    
    function sendMessage() {
        var message = inputField.text
        chatArea.text += "\nYou: " + message
        inputField.text = ""
        
        // Вызов C++ функции
        llamaEngine.generateAsync(message, function(response) {
            chatArea.text += "\nAssistant: " + response
        })
    }
}