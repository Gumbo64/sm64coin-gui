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
// SM64COIN: the mining leaderboard. Every node keeps the top 50 by blocks
// mined and shares it with its peers over p2p (sm64_board.h); this page
// reads and joins it through the node's RPC, so it works wherever the node does.
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

    function daemonUrl() {
        var a = (typeof currentDaemonAddress !== "undefined" && currentDaemonAddress) ? currentDaemonAddress : "127.0.0.1:16481";
        return a.indexOf("://") >= 0 ? a : "http://" + a;
    }
    function rpc(method, params, cb) {
        var id = nextId++;
        pending[id] = cb;
        sm64Http.request(id, "POST", daemonUrl() + "/json_rpc", JSON.stringify({ jsonrpc: "2.0", id: "0", method: method, params: params || {} }));
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
    // The standings are on the website (it reads the same board from a node).
    function standingsUrl() {
        return persistentSettings.sm64WebMinerUrl.replace(/\/mine\/?$/, "").replace(/\/+$/, "") + "/leaderboard";
    }
    function join() {
        if (!walletAddress) { setStatus(qsTr("Open a wallet first."), true); return; }
        var name = nameField.text.trim();
        persistentSettings.sm64LeaderboardName = name;
        // Signed with this wallet's spend key: only the owner can name the address.
        // A later timestamp renames it.
        var ts = Math.floor(Date.now() / 1000);
        var sig = currentWallet.signMessage("sm64coin-board:" + ts + ":" + name, false);
        rpc("sm64_board_join", { name: name, address: walletAddress, view_key: currentWallet.secretViewKey, timestamp: ts, signature: sig },
            function(status, data, text) {
                if (status === 200 && data && data.result) {
                    if (data.result.joined) setStatus(qsTr("On the board as \"%1\". Your node shares it with its peers.").arg(name), false);
                    else setStatus(qsTr("Not on the board: ") + data.result.reason, true);
                } else setStatus((data && data.error) ? data.error.message : (text || "HTTP " + status), true);
            });
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
            text: qsTr("Put this wallet on the SM64Coin leaderboard: the top 50 miners by blocks mined. Every node keeps the board, counts the blocks itself and shares it with its peers, so joining works wherever your node does. Only addresses that have mined a block can join.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
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
                text: qsTr("Join / rename") + translationManager.emptyString
                enabled: nameField.text.trim().length > 0 && walletAddress !== ""
                onClicked: join()
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

        MoneroComponents.StandardButton {
            Layout.topMargin: 10
            text: qsTr("View the leaderboard") + translationManager.emptyString
            onClicked: Qt.openUrlExternally(standingsUrl())
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            text: qsTr("Privacy: joining gives this wallet's address and secret view key to every node on the network (so each can count your blocks itself). Anyone can then see all incoming payments to this wallet (they cannot spend). Use a dedicated mining address, not your main wallet. Your name and block count are public, and an entry can't be removed once shared; you can rename it.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.orange
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
    }
}
