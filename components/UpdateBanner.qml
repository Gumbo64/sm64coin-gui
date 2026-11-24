// SM64COIN: dismissible "SM64Coin X.Y.Z is available" bar. Asks GitHub's public releases API
// (unauthenticated, cached 6 h, silent on any failure) at startup and every 24 h.
import QtQuick 2.9
import QtQuick.Layouts 1.1

import "../js/UpdateCheck.js" as UpdateCheck
import "../version.js" as Version
import "." as MoneroComponents

Rectangle {
    id: root
    property var release: null
    property bool dismissed: false
    // The running version: the one this build was made as; SM64COIN_VERSION_OVERRIDE (testing) wins.
    readonly property string runningVersion: sm64Http.versionOverride() || Version.SM64COIN_VERSION
    readonly property int requestId: 987654

    visible: release !== null && !dismissed && persistentSettings.checkSm64Updates
    height: visible ? 34 : 0
    color: release && release.required ? "#8a1c1c" : (MoneroComponents.Style.blackTheme ? "#1f2a44" : "#dbe6ff")

    Text {
        id: msg
        anchors.centerIn: parent
        width: parent.width - 60
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        textFormat: Text.RichText
        font.pixelSize: 14
        color: release && release.required ? "white" : MoneroComponents.Style.defaultFontColor
        linkColor: release && release.required ? "white" : MoneroComponents.Style.orange
        text: release ? ("<style>a { color: " + (release.required ? "white" : MoneroComponents.Style.orange) + "; }</style>" + (release.required ? "<b>" + qsTr("Update required:") + "</b> " : "")
                         + qsTr("SM64Coin %1 is available").arg(release.version)
                         + " — <a href=\"" + release.url + "\">" + qsTr("release notes") + "</a>")) : ""
        onLinkActivated: Qt.openUrlExternally(link)
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        text: "×"
        font.pixelSize: 20
        color: msg.color
        MouseArea {
            anchors.fill: parent
            anchors.margins: -8
            cursorShape: Qt.PointingHandCursor
            onClicked: root.dismissed = true
        }
    }

    function check() {
        if (!persistentSettings.checkSm64Updates || !UpdateCheck.parseVersion(runningVersion))
            return
        var url = sm64Http.updateUrl()
        var cached = null
        try { cached = JSON.parse(persistentSettings.sm64LatestRelease) } catch (e) {}
        if (cached && cached.url === url && Date.now() - cached.at < UpdateCheck.CACHE_MS) {
            apply(cached.json)
            return
        }
        sm64Http.request(requestId, "GET", url, "")
    }
    function apply(jsonText) {
        var r = UpdateCheck.newerRelease(jsonText, runningVersion)
        if (r === null || (release !== null && release.version === r.version)) { release = r; return }
        release = r
        dismissed = false
    }
    Connections {
        target: sm64Http
        onFinished: {
            if (id !== root.requestId || status !== 200)
                return
            persistentSettings.sm64LatestRelease = JSON.stringify({ at: Date.now(), url: sm64Http.updateUrl(), json: text })
            root.apply(text)
        }
    }
    Timer { interval: 4000; running: true; onTriggered: root.check() }   // after startup settles
    Timer { interval: 24 * 3600 * 1000; running: true; repeat: true; onTriggered: root.check() }
}
