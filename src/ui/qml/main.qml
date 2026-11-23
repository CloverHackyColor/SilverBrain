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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12

        // ================= Панель настроек =================
        ColumnLayout {
            Layout.minimumWidth: 200
            Layout.maximumWidth: 200
            Layout.fillHeight: true
            Layout.fillWidth: false
            spacing: 10


            Label { text: "Настройки"; font.bold: true; color: "#26251f" }

            Button {
                id: stopButton
                text: "Stop"
                Layout.fillWidth: true
                enabled: llamaEngine.busy
                onClicked: llamaEngine.stopGeneration()
                background: Rectangle {
                    radius: 6
                    color: parent.enabled ? (parent.hovered ? "#d94343" : "#c62828")
                                          : "#f0eeea"
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "#ffffff" : "#a09d94"
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Label { text: "Размер контекста"; color: "#26251f" }
            SpinBox {
                id: ctxSpin
                Layout.fillWidth: true
                from: 10                  // 2^10 = 1024
                to: 17                    // 2^17 = 131072
                stepSize: 1
                editable: true
                value: Math.round(Math.log2(llamaEngine.contextSize))
                onValueModified: llamaEngine.contextSize = Math.pow(2, value)

                validator: RegularExpressionValidator { regularExpression: /\d{3,6}/ }

                // На экране — сам размер, внутри — экспонента
                textFromValue: function(value, locale) {
                    return (Math.pow(2, value)).toLocaleString(Qt.locale(), "f", 0)
                }
                valueFromText: function(text, locale) {
                    var n = Number.fromLocaleString(Qt.locale(), text.replace(/\s/g, ""))
                    if (isNaN(n)) return value
                    return Math.min(to, Math.max(from, Math.round(Math.log2(n))))
                }
            }

            FileDialog {
                id: saveDialog
                fileMode: FileDialog.SaveFile
                title: "Save history"
                nameFilters: ["SilverBrain chat (*.sbc)"]
                defaultSuffix: "sbc"
                currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                onAccepted: {
                    var path = selectedFile.toString()
                    if (path.startsWith("file://"))
                        path = decodeURIComponent(path.substring(7))
                    llamaEngine.saveHistoryTo(path)
                }
            }
            FileDialog {
                id: loadDialog
                fileMode: FileDialog.OpenFile
                title: "Load history"
                nameFilters: ["SilverBrain chat (*.sbc)"]
                currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                onAccepted: {
                    var path = selectedFile.toString()
                    if (path.startsWith("file://"))
                        path = decodeURIComponent(path.substring(7))
                    llamaEngine.loadHistoryFrom(path)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Save ctx"
                    onClicked: saveDialog.open()
                }
                Button {
                    text: "Load ctx"
                    enabled: !llamaEngine.busy && !llamaEngine.loading
                    onClicked: loadDialog.open()
                }
            }

            Label { text: "Temperature: " + llamaEngine.temperature.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: llamaEngine.temperature
                onMoved: llamaEngine.temperature = value
            }

            Label { text: "Repeat penalty: " + llamaEngine.repeatPenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 1; to: 2; stepSize: 0.05
                value: llamaEngine.repeatPenalty
                onMoved: llamaEngine.repeatPenalty = value
            }

            Label { text: "Presence: " + llamaEngine.presencePenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: llamaEngine.presencePenalty
                onMoved: llamaEngine.presencePenalty = value
            }

            Label { text: "Frequency: " + llamaEngine.frequencyPenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: llamaEngine.frequencyPenalty
                onMoved: llamaEngine.frequencyPenalty = value
            }

            Item { Layout.fillHeight: true }   // распорка прижимает всё вверх
        }

        // Разделитель
        Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: "#d9d6cf" }

        // ================= Существующий интерфейс =================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                    FileDialog {
                        id: mmprojDialog
                        title: "Выберите mmproj"
                        nameFilters: ["GGUF проектор (*.gguf)"]
                        onAccepted: {
                            var path = selectedFile.toString()
                            if (path.startsWith("file://"))
                                path = decodeURIComponent(path.substring(7))
                            llamaEngine.mmprojPath = path
                        }
                    }
                    Button { text: "mmproj"; onClicked: mmprojDialog.open() }

                    Label { 
                        id: statusLabel; 
                        color: statusLabel.text.startsWith("Ошибка")   ? "#c62828"
                                : statusLabel.text.startsWith("Загрузка") ? "#2f6feb"
                                : "#2e7d32"
                    }
                }

                ProgressBar {
                    id: loadBar
                    visible: llamaEngine.loading
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    from: 0
                    to: 1
                    value: llamaEngine.loadProgress
                    background: null                    // убираем подложку и её отступы
                    contentItem: Item {
                        Rectangle {
                            width: loadBar.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: "#2f6feb"
                        }
                    }
                }
                ProgressBar {
                    id: promptBar
                    visible: false
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    from: 0
                    to: 100
                    background: null
                    contentItem: Item {
                        Rectangle {
                            width: promptBar.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: "#2e7d32"
                        }
                    }
                }
                Label {
                    id: liveStats
                    visible: text !== ""
                    font.pixelSize: 11
                    color: "#8a877e"
                }
                Label {
                    id: freeLabel
                    visible: text !== ""
                    font.pixelSize: 11
                    color: "#8a877e"
                }


                ListView {
                    id: chatList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    anchors.margins: 16
                    model: chatModel  
                    clip: true
                    spacing: 8
                    

                    delegate: Loader {
                        required property var model
                        width: chatList.width - 32
                        sourceComponent: model.role === "stats" ? statsRow : bubble
                        Component {
                            id: bubble
                            ChatBubble {
                                width: parent.width
                                isUser: model.role === "user"
                                messageText: model.text
                            }
                        }
                        Component {
                            id: statsRow
                            Label {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 11
                                color: "#8a877e"
                                text: model.text
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }


                    // Автопрокрутка вниз при новых токенах
                    onCountChanged: positionViewAtEnd()

                    ScrollIndicator.vertical: ScrollIndicator {}
                }

                RowLayout {
                    Layout.fillWidth: true

                    TextArea {
                        id: inputField
                        wrapMode: TextArea.Wrap
                        Layout.fillWidth: true
                        Layout.maximumHeight: 160  
                        placeholderTextColor: "#a09d94"
                        color: "#26251f"
                        background: Rectangle {
                            radius: 6
                            color: "#ffffff"
                            border.color: "#d9d6cf"
                        }
                        enabled: !llamaEngine.busy
                        placeholderText: "Type your message..."
                          // Отправка — Cmd+Enter / Ctrl+Enter или кнопкой
                        Keys.onPressed: (event) => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) &&
                                (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier))) {
                                sendMessage()
                                event.accepted = true
                            }
                        }                      
                    }
                    FileDialog {
                        id: attachDialog
                        title: "Attach image"
                        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.webp)"]
                        onAccepted: {
                            var path = selectedFile.toString()
                            if (path.startsWith("file://"))
                                path = decodeURIComponent(path.substring(7))
                            llamaEngine.imagePath = path
                        } 
                    }
                    Button {
                        text: "📎"
                        enabled: !llamaEngine.busy
                        onClicked: attachDialog.open()
                    }
                    Label {
                        text: llamaEngine.imagePath !== "" ? "🖼 " + llamaEngine.imagePath.split("/").pop() : ""
                        visible: text !== ""
                        color: "#2f6feb"
                        MouseArea { anchors.fill: parent; onClicked: llamaEngine.imagePath = "" }
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
        function onLoadProgressChanged() {
            if (llamaEngine.loading)
                statusLabel.text = "Загрузка модели: " +
                    Math.round(llamaEngine.loadProgress * 100) + "%"
        }

        function onModelLoaded(ok, error) {
            statusLabel.text = ok ? "Модель загружена" : ("Ошибка: " + error)
        }
        function onTokenReceived(piece) {
            // Дописываем в последнее сообщение (пустое, созданное в sendMessage)
            chatModel.appendToLast(piece)
        }
        function onGenerationFinished(fullText) {
            promptBar.visible = false
            liveStats.text = ""
        }
        function onGenerationError(message) {
            chatModel.appendToLast("[Ошибка] " + message)
        }
        function onHistoryRestored(msgs) {
            chatModel.clear()
            for (var i = 0; i < msgs.length; i++)
                chatModel.append({ role: msgs[i].role, text: msgs[i].text })
        }
        function onPromptProgress(p) {
            promptBar.visible = true
            promptBar.value = p
        }
        function onGenSpeed(tps) {
            liveStats.text = "Скорость: " + Number(tps).toFixed(1) + " ток/с"
        }
        function onContextFree(cells) {
            freeLabel.text = "Свободно контекста: " + cells + " ток."
        }
        function onStatsAppended(line) {
            chatModel.addMessage("stats", line)
        }

        function onHistorySaved(ok) {
            statusLabel.text = ok ? qsTr("История сохранена") : qsTr("Ошибка сохранения");
        }

    }
}
