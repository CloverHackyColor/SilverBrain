import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.platform 1.1

ApplicationWindow {
    visible: true
    width: 1280
    height: 800
    title: "Silver Brain"
    color: "#f6f5f2"
    property string selectedModelPath: ""

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


            Label { text: "Settings"; font.bold: true; color: "#26251f" }

            Button {
                id: stopButton
                text: "Stop"
                Layout.fillWidth: true
                enabled: silverBrain.busy
                onClicked: silverBrain.stopGeneration()
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

            Label { text: "Context size"; color: "#26251f" }
            SpinBox {
                id: ctxSpin
                Layout.fillWidth: true
                from: 10                  // 2^10 = 1024
                to: 17                    // 2^17 = 131072
                stepSize: 1
                editable: true
                value: Math.round(Math.log2(silverBrain.contextSize))
                onValueModified: silverBrain.contextSize = Math.pow(2, value)

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
                //currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                folder: Qt.resolvedUrl(StandardPaths.writableLocation(StandardPaths.DocumentsLocation))
                onAccepted: {
                    var path = selectedFile.toLocalFile()
                    silverBrain.saveHistoryAs(path)
                    console.log("Saving history to:", path)
                }
            }
            FileDialog {
                id: loadDialog
                fileMode: FileDialog.OpenFile
                title: "Load history"
                nameFilters: ["SilverBrain chat (*.sbc)"]
                //currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                folder: Qt.resolvedUrl(StandardPaths.writableLocation(StandardPaths.DocumentsLocation))
                onAccepted: {
                    var path = selectedFile.toLocalFile()
                    silverBrain.loadHistoryFrom(path)
                    console.log("Load history from:", path)
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
                    enabled: !silverBrain.busy && !silverBrain.loading
                    onClicked: loadDialog.open()
                }
            }

            Label { text: "Temperature: " + silverBrain.temperature.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: silverBrain.temperature
                onMoved: silverBrain.temperature = value
            }

            Label { text: "Repeat penalty: " + silverBrain.repeatPenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 1; to: 2; stepSize: 0.05
                value: silverBrain.repeatPenalty
                onMoved: silverBrain.repeatPenalty = value
            }

            Label { text: "Presence: " + silverBrain.presencePenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: silverBrain.presencePenalty
                onMoved: silverBrain.presencePenalty = value
            }

            Label { text: "Frequency: " + silverBrain.frequencyPenalty.toFixed(2); color: "#26251f" }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 2; stepSize: 0.05
                value: silverBrain.frequencyPenalty
                onMoved: silverBrain.frequencyPenalty = value
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
                        placeholderText: "Name of GGUF-model..."
                    }
                    FileDialog {
                        id: modelFileDialog
                        title: "Choose model file"
                        nameFilters: ["GGUF model (*.gguf)"]
                        //selectedNameFilterExtensions: ["*.gguf"]
                        onAccepted: {
                            // currentFile — локальный URL выбранного файла
                            var path = QUrl(selectedFile).toLocalFile()
                        //    if (path.startsWith("file://"))
                        //        path = decodeURIComponent(path.substring(7))   // срезаем "file://" (на маке один слэш)
                            selectedModelPath = path
                            modelPathField.text = path.substring(
                                Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\")) + 1)
                            console.log("Model loading: ", selectedModelPath)
                            silverBrain.loadModel(path)  // mmproj будет по-умолчанию ""
                        }                                    
                    }
                    Button {
                        text: "Load model"
                        background: Rectangle { radius: 6; color: "#e7e4de" }
                        onClicked: {
                            if (silverBrain.busy) {
                                console.log("Engine busy")
                                return
                            }
                            modelFileDialog.open()
                        }
                        enabled: !silverBrain.busy
                    }
        /*            FileDialog {
                        id: mmprojDialog
                        title: "Choose mmproj"
                        nameFilters: ["GGUF projector (*.gguf)"]
                        onAccepted: {
                            var path = selectedFile.toString()
                            if (path.startsWith("file://"))
                                path = decodeURIComponent(path.substring(7))
                            silverBrain.mmprojPath = path
                        }
                    }
                    */
                    //Button { text: "mmproj"; onClicked: mmprojDialog.open() }

                    Label { 
                        id: statusLabel; 
                        color: statusLabel.text.startsWith("Error")   ? "#c62828"
                                : statusLabel.text.startsWith("Loading...") ? "#2f6feb"
                                : "#2e7d32"
                    }
                }

                ProgressBar {
                    id: loadBar
                    visible: silverBrain.loading
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    from: 0
                    to: 1
                    value: silverBrain.loadProgress
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
                        enabled: !silverBrain.busy
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
                        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
                        onAccepted: {
                            var path = selectedFile.toString()
                            if (path.startsWith("file://"))
                                path = decodeURIComponent(path.substring(7))
                            silverBrain.imagePath = path
                        } 
                    }
                    Button {
                        text: "📎"
                        enabled: !silverBrain.busy
                        onClicked: attachDialog.open()
                    }
                    Label {
                        text: {
                            if (!silverBrain.imagePath || silverBrain.imagePath === "") return ""
                            var parts = silverBrain.imagePath.split("/")
                            return "🖼 " + parts[parts.length - 1]
                        }
                        visible: text !== ""
                        color: "#2f6feb"
                        MouseArea { anchors.fill: parent; onClicked: silverBrain.imagePath = "" }
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
                        enabled: !silverBrain.busy
                        onClicked: sendMessage()
                    }
                    Button {
                        text: "Clear"
                        background: Rectangle { radius: 6; color: "#e7e4de" }
                        enabled: !silverBrain.busy
                        onClicked: {
                            chatModel.clear()
                            silverBrain.clearHistory()
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
        silverBrain.generateAsync(message)
    }

    Connections {
        target: silverBrain
        function onLoadProgressChanged() {
            if (silverBrain.loading)
                statusLabel.text = "Model loading...: " +
                    Math.round(silverBrain.loadProgress * 100) + "%"
        }

        function onModelLoaded(ok, error) {
            statusLabel.text = ok ? "Model loaded" : ("Ошибка: " + error)
        }
        function onTokenReceived(piece) {
            // Дописываем в последнее сообщение (пустое, созданное в sendMessage)
            chatModel.appendToLast(piece)
        }
        function onGenerationFinished(fullText) {
            promptBar.visible = false
            promptBar.value = 0
            liveStats.text = ""
        }
        function onGenerationError(message) {
            chatModel.appendToLast("[Error] " + message)
            promptBar.visible = false
            promptBar.value = 0
        }
        function onHistoryRestored(msgs) {
            chatModel.clear()
            for (var i = 0; i < msgs.length; i++)
                chatModel.addMessage(msgs[i].role, msgs[i].text)
        }
        function onPromptProgress(p) {
            promptBar.visible = true
            promptBar.value = p
        }
        function onGenSpeed(tps) {
            liveStats.text = "Speed: " + Number(tps).toFixed(1) + " ток/с"
        }
        function onContextFree(cells) {
            freeLabel.text = "Free context: " + cells + " ток."
        }
        function onStatsAppended(line) {
            chatModel.addMessage("stats", line)
        }

        function onHistorySaved(ok) {
            statusLabel.text = ok ? qsTr("History is saved") : qsTr("Saving error");
        }

    }
}
