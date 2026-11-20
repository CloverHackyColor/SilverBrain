import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    visible: true
    width: 1280
    height: 800
    title: "Silver Brain"
    color: "#f6f5f2"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            TextField {
                id: modelPathField
                Layout.fillWidth: true
                placeholderTextColor: "#a09d94"
                color: "#26251f"
                background: Rectangle {
                    radius: 6
                    color: "#ffffff"
                    border.color: "#d9d6cf"
                }
                placeholderText: "Название GGUF-модели..."
            }
            FileDialog {
                id: modelFileDialog
                title: "Выберите файл модели"
                nameFilters: ["GGUF модели (*.gguf)"]
                //selectedNameFilterExtensions: ["*.gguf"]
                onAccepted: {
                    // currentFile — локальный URL выбранного файла
                    var path = selectedFile.toString()
                    if (path.startsWith("file://"))
                    path = decodeURIComponent(path.substring(7))   // срезаем "file://" (на маке один слэш)
                    modelPathField.text = path 
                    llamaEngine.loadModel(path)
                }
            }
            Button {
                text: "Load model"
                background: Rectangle { radius: 6; color: "#e7e4de" }
                enabled: !llamaEngine.busy
                onClicked: modelFileDialog.open()
            }
            Label { 
                id: statusLabel; 
                color: statusLabel.text.startsWith("Ошибка") ? "#c62828" : "#2e7d32"
            }
        }

        ListView {
            id: chatList
            Layout.fillWidth: true
            Layout.fillHeight: true
            anchors.margins: 16
            model: chatModel  
            clip: true
            spacing: 8
            

            delegate: ChatBubble {
                width: chatList.width - 32
                isUser: model.role === "user"   // model доступен ЗДЕСЬ, внутри делегата
                messageText: model.text
            }

            // Автопрокрутка вниз при новых токенах
            onCountChanged: positionViewAtEnd()

            ScrollIndicator.vertical: ScrollIndicator {}
        }

        RowLayout {
            Layout.fillWidth: true

            TextField {
                id: inputField
                Layout.fillWidth: true
                placeholderTextColor: "#a09d94"
                color: "#26251f"
                background: Rectangle {
                    radius: 6
                    color: "#ffffff"
                    border.color: "#d9d6cf"
                }
                enabled: !llamaEngine.busy
                placeholderText: "Type your message..."
                onAccepted: sendMessage()
            }
            Button {
                text: "Send"
                background: Rectangle {
                    radius: 6
                    color: parent.pressed ? "#2564d0"
                            : parent.hovered ? "#3b7bf1" : "#2f6feb"
                    }
                contentItem: Text { text: parent.text; color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter }
                enabled: !llamaEngine.busy
                onClicked: sendMessage()
            }
            Button {
                text: "Clear"
                background: Rectangle { radius: 6; color: "#e7e4de" }
                enabled: !llamaEngine.busy
                onClicked: {
                    chatModel.clear()
                    llamaEngine.clearHistory()
                }
            }
        }
    }

    function sendMessage() {
        var message = inputField.text
        if (message.trim() === "")
            return

        chatModel.addMessage("user", message)
        // Пустой ответ ассистента: в него будет стримиться текст по токенам
        chatModel.addMessage("assistant", "")

        inputField.text = ""
        llamaEngine.generateAsync(message)
    }

    Connections {
        target: llamaEngine

        function onModelLoaded(ok, error) {
            statusLabel.text = ok ? "Модель загружена" : ("Ошибка: " + error)
        }
        function onTokenReceived(piece) {
            // Дописываем в последнее сообщение (пустое, созданное в sendMessage)
            chatModel.appendToLast(piece)
        }
        function onGenerationFinished(fullText) {
            // Текст уже досыпал токенами; здесь ничего дополнять не нужно.
            // Место под будущие пост-обработки (подсветку, автосохранение и т.п.)
        }
        function onGenerationError(message) {
            chatModel.appendToLast("[Ошибка] " + message)
        }
    }
}
