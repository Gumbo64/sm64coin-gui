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
// SM64COIN: the hash-mining page of upstream monero-gui (solo mining / P2Pool) is replaced by this
// informational page. SM64Coin blocks are sealed by playing Super Mario 64, not by hashing.
import QtQuick 2.9
import QtQuick.Layouts 1.1
import "../components" as MoneroComponents

Rectangle {
    id: root
    color: "transparent"
    property alias miningHeight: mainLayout.height
    // Kept so that main.qml (which was written against the upstream mining page) keeps working.
    property bool stopMiningEnabled: false
    function update() {}

    ColumnLayout {
        id: mainLayout
        Layout.fillWidth: true
        anchors.margins: 20
        anchors.topMargin: 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        spacing: 20

        MoneroComponents.Label {
            fontSize: 24
            text: qsTr("Mining") + translationManager.emptyString
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("SM64Coin is not mined by hashing. A block is sealed by a controller input sequence that, replayed on an emulated Nintendo 64 from power-on, makes Mario collect a star. The built-in CPU hash miner of this wallet does not exist: mining is done by playing.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("Play in the browser with the web miner:") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        MoneroComponents.StandardButton {
            small: true
            text: "http://localhost:5181/mine"
            onClicked: Qt.openUrlExternally("http://localhost:5181/mine")
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("Or run the automated miner from the command line (it needs a running node and the address of your wallet):") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        MoneroComponents.LineEditMulti {
            Layout.fillWidth: true
            readOnly: true
            wrapMode: Text.WrapAnywhere
            text: "automine --address " + (currentWallet ? currentWallet.address(0, 0) : "<your SM64 address>")
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("Mined coinbase outputs unlock after 10 blocks (about 50 minutes).") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.dimmedFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
    }
}
