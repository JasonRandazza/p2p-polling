import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var counts: ({ "Apples": 0, "Bananas": 0, "Oranges": 0 })
    property string statusText: "Ready to vote"
    property string errorText: ""
    property string votingFor: ""
    property string toastMessage: ""
    property bool toastVisible: false

    readonly property var options: ["Apples", "Bananas", "Oranges"]
    readonly property color backgroundColor: "#20242a"
    readonly property color surfaceColor: "#2c323a"
    readonly property color borderColor: "#444c57"
    readonly property color textColor: "#f4f7fb"
    readonly property color mutedTextColor: "#aeb8c5"
    readonly property color accentColor: "#6ee7b7"
    readonly property color warningColor: "#f97373"

    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 680)
        spacing: 18

        Text {
            text: "P2P Polling"
            color: root.textColor
            font.pixelSize: 38
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        Text {
            text: "Votes sync through Logos Delivery when peers are online."
            color: root.mutedTextColor
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 14
            Layout.fillWidth: true
            Layout.topMargin: 12

            Repeater {
                model: root.options

                delegate: Rectangle {
                    required property string modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 170
                    radius: 8
                    color: root.surfaceColor
                    border.color: root.borderColor
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10

                        Text {
                            text: modelData
                            color: root.textColor
                            font.pixelSize: 21
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            text: String(root.countFor(modelData))
                            color: root.accentColor
                            font.pixelSize: 42
                            font.weight: Font.Bold
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Button {
                            text: root.votingFor === modelData ? "Sending..." : "Vote"
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 116
                            implicitHeight: 38
                            enabled: root.votingFor === ""
                            onClicked: root.submitVote(modelData)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: 8
            color: root.errorText.length > 0 ? "#3a262a" : "#26352f"
            border.color: root.errorText.length > 0 ? "#7f3b45" : "#3e6657"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: root.errorText.length > 0 ? root.errorText : root.statusText
                color: root.errorText.length > 0 ? root.warningColor : root.accentColor
                font.pixelSize: 14
            }
        }
    }

    Timer {
        id: toastTimer
        interval: 3000
        repeat: false
        onTriggered: root.toastVisible = false
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.toastVisible ? 20 : -56
        width: toastLabel.implicitWidth + 40
        height: 44
        z: 100
        radius: 22
        color: "#1e3a2f"
        border.color: root.accentColor
        border.width: 1
        opacity: root.toastVisible ? 1.0 : 0.0

        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Text {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastMessage
            color: root.accentColor
            font.pixelSize: 13
        }
    }

    Component.onCompleted: {
        if (typeof logos !== "undefined" && logos.onModuleEvent)
        {
            logos.onModuleEvent("polling_core", "voteSubmitted")
            logos.onModuleEvent("polling_core", "networkStatusChanged")
        }

        root.refreshCounts()
    }

    Connections {
        target: typeof logos !== "undefined" ? logos : null
        function onModuleEventReceived(moduleName, eventName, data) {
            if (moduleName !== "polling_core")
                return

            if (eventName === "networkStatusChanged" && data && data.length > 0) {
                root.applyResult(data[0])
                return
            }

            if (eventName === "voteSubmitted" && data && data.length > 1) {
                var result = root.normalizeResult(data[1])
                root.applyResult(data[1])
                if (result && result.source === "network") {
                    root.showToast("New vote received for " + String(data[0]) + "!")
                }
            } else {
                root.refreshCounts()
            }
        }
    }

    function countFor(option) {
        return Number(root.counts[option] || 0)
    }

    function applyResult(result) {
        result = root.normalizeResult(result)

        if (!result) {
            return
        }

        if (result.ok === false) {
            root.errorText = String(result.error || "Vote failed")
            return
        }

        if (result.counts) {
            root.counts = {
                "Apples": Number(result.counts.Apples || 0),
                "Bananas": Number(result.counts.Bananas || 0),
                "Oranges": Number(result.counts.Oranges || 0)
            }
        }

        var total = result.total !== undefined ? Number(result.total) : root.totalVotes()
        root.errorText = ""
        root.statusText = "Total votes: " + total
        if (result.networkStatus)
            root.statusText += " | " + String(result.networkStatus)
    }

    function refreshCounts() {
        callCore("getVoteCounts", [], applyResult)
    }

    function submitVote(option) {
        root.votingFor = option
        root.statusText = "Submitting vote for " + option + "..."
        callCore("submitVote", [option], function(result) {
            root.votingFor = ""
            root.applyResult(result)
        })
    }

    function totalVotes() {
        return root.countFor("Apples") + root.countFor("Bananas") + root.countFor("Oranges")
    }

    function callCore(method, args, onSuccess) {
        root.errorText = ""

        if (typeof logos === "undefined") {
            root.errorText = "Logos bridge not available"
            return
        }

        try {
            if (logos.callModule) {
                onSuccess(logos.callModule("polling_core", method, args))
                return
            }

            if (logos.callModuleAsync) {
                logos.callModuleAsync("polling_core", method, args, function(result) {
                    if (result !== undefined && result !== null)
                        onSuccess(result)
                })
                return
            }

            root.errorText = "No Logos module call API available"
        } catch (error) {
            root.errorText = String(error)
        }
    }

    function showToast(message) {
        root.toastMessage = message
        root.toastVisible = true
        toastTimer.restart()
    }

    function normalizeResult(result) {
        if (typeof result === "string") {
            try {
                return JSON.parse(result)
            } catch (error) {
                root.errorText = result
                return null
            }
        }

        return result
    }
}
