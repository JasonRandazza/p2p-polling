import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property var counts: ({ "Apples": 0, "Bananas": 0, "Oranges": 0 })
    property string statusText: "Ready to vote"
    property string errorText: ""

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
            text: "Local vote state today. Logos Delivery broadcast wiring next."
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
                            text: "Vote"
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 116
                            implicitHeight: 38
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

    Component.onCompleted: root.refreshCounts()

    function countFor(option) {
        return Number(root.counts[option] || 0)
    }

    function applyResult(result) {
        if (!result) {
            return
        }

        if (result.ok === false) {
            root.errorText = String(result.error || "Vote failed")
            return
        }

        if (result.counts) {
            root.counts = result.counts
        }

        var total = result.total !== undefined ? Number(result.total) : root.totalVotes()
        root.errorText = ""
        root.statusText = "Total local votes: " + total
    }

    function refreshCounts() {
        callCore("getVoteCounts", [], applyResult)
    }

    function submitVote(option) {
        root.statusText = "Submitting vote for " + option + "..."
        callCore("submitVote", [option], applyResult)
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
            if (logos.callModuleAsync) {
                logos.callModuleAsync("polling_core", method, args, function(result) {
                    onSuccess(result)
                })
                return
            }

            if (logos.callModule) {
                onSuccess(logos.callModule("polling_core", method, args))
                return
            }

            root.errorText = "No Logos module call API available"
        } catch (error) {
            root.errorText = String(error)
        }
    }
}
