// Copyright (c) 2014-2024, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import QtQml.Models 2.2
// SM64COIN: leaderboard of participating miners, served by an (optional) leaderboard service.
import QtQuick 2.9
import QtQuick.Layouts 1.1
import "../components" as MoneroComponents

Rectangle {
    id: root
    color: "transparent"
    property int panelHeight: mainLayout.height
    property string walletAddress: currentWallet ? currentWallet.address(0, 0) : ""
    property string statusText: ""
    property bool statusIsError: false
    property int nextId: 1
    property var pending: ({})

    function base() { return persistentSettings.sm64LeaderboardUrl.replace(/\/+$/, ""); }
    function call(method, path, body, cb) {
        var id = nextId++;
        pending[id] = cb;
        sm64Http.request(id, method, base() + path, body ? JSON.stringify(body) : "");
    }
    Connections {
        target: sm64Http
        onFinished: {
            var cb = root.pending[id];
            if (!cb) return;
            delete root.pending[id];
            var data = null;
            try { data = JSON.parse(text); } catch (e) {}
            cb(status, data, text);
        }
    }
    function setStatus(t, err) { statusText = t; statusIsError = err; }
    function reply(okMsg) {
        return function(status, data, text) {
            if (status === 200) setStatus(okMsg, false);
            else setStatus((data && data.error) ? data.error : (text || "HTTP " + status), true);
        };
    }
    function join() {
        if (!walletAddress) { setStatus(qsTr("Open a wallet first."), true); return; }
        persistentSettings.sm64LeaderboardName = nameField.text;
        call("POST", "/register", { name: nameField.text, address: walletAddress, view_key: currentWallet.secretViewKey },
             reply(qsTr("Joined the leaderboard.")));
    }
    function leave() {
        if (!walletAddress) { setStatus(qsTr("Open a wallet first."), true); return; }
        call("POST", "/unregister", { address: walletAddress, view_key: currentWallet.secretViewKey },
             reply(qsTr("Left the leaderboard.")));
    }
    // The standings themselves are on the website (read-only, no keys).
    function standingsUrl() {
        return persistentSettings.sm64WebMinerUrl.replace(/\/mine\/?$/, "").replace(/\/+$/, "") + "/leaderboard";
    }

    ColumnLayout {
        id: mainLayout
        anchors.margins: 20
        anchors.rightMargin: 50
        anchors.topMargin: 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        spacing: 14

        MoneroComponents.Label {
            fontSize: 24
            text: qsTr("Leaderboard") + translationManager.emptyString
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("Opt in to the public SM64Coin leaderboard with this wallet. The standings are on the website.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            MoneroComponents.LineEdit {
                Layout.fillWidth: true
                labelText: qsTr("Leaderboard service URL") + translationManager.emptyString
                text: persistentSettings.sm64LeaderboardUrl
                onTextChanged: persistentSettings.sm64LeaderboardUrl = text
            }
            MoneroComponents.StandardButton {
                small: true
                primary: false
                text: qsTr("View standings") + translationManager.emptyString
                onClicked: Qt.openUrlExternally(standingsUrl())
            }
        }

        MoneroComponents.Label {
            fontSize: 18
            Layout.topMargin: 10
            text: qsTr("This wallet") + translationManager.emptyString
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            MoneroComponents.LineEdit {
                id: nameField
                Layout.fillWidth: true
                labelText: qsTr("Display name") + translationManager.emptyString
                text: persistentSettings.sm64LeaderboardName
            }
            MoneroComponents.StandardButton {
                small: true
                text: qsTr("Join") + translationManager.emptyString
                enabled: nameField.text.length > 0 && walletAddress !== ""
                onClicked: join()
            }
            MoneroComponents.StandardButton {
                small: true
                primary: false
                text: qsTr("Leave") + translationManager.emptyString
                enabled: walletAddress !== ""
                onClicked: leave()
            }
        }
        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            objectName: "boardStatus"
            visible: statusText !== ""
            text: statusText
            wrapMode: Text.Wrap
            color: statusIsError ? MoneroComponents.Style.errorColor : MoneroComponents.Style.green
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            text: qsTr("Privacy: joining sends this wallet's address and secret view key to the leaderboard service, so its operator can see all incoming payments to this wallet (but cannot spend). Anyone can run their own leaderboard service; set its URL above. Leave removes the entry. Your display name and block count are public.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.orange
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
    }
}
