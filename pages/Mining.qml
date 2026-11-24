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
// SM64COIN: control page for mining. SM64Coin blocks are sealed by playing Super Mario 64, not by
// hashing: this page starts/stops the automated miner (child process) and opens the browser miner.
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

    property string walletAddress: currentWallet ? currentWallet.address(0, 0) : ""
    property var logLines: []
    property string lastError: ""
    property int curHeight: 0
    property string refProgress: ""
    property int attempts: 0
    property int wins: 0
    property string lastEvent: ""

    function daemonUrl() {
        var a = (typeof currentDaemonAddress !== "undefined" && currentDaemonAddress) ? currentDaemonAddress : "127.0.0.1:16481";
        return a.indexOf("://") >= 0 ? a : "http://" + a;
    }
    // Settings left empty mean "automatic". The defaults of builds before 0.2.2 were paths of the
    // developer's checkout; they count as empty.
    readonly property var oldDefaults: ["/workspaces/sm64-combined/sm64-crypto3/bin/sm64coin-automine",
                                        "/workspaces/sm64-combined/sm64-crypto3/baserom.us.z64",
                                        "/workspaces/sm64-combined/sm64-crypto3/run/mined"]
    function setting(v) { return (v && oldDefaults.indexOf(v) < 0) ? v : ""; }
    function minerPath() { return setting(persistentSettings.sm64MinerPath) || minerProcess.defaultMinerPath(); }
    function romPath() { return setting(persistentSettings.sm64RomPath) || minerProcess.defaultRomPath(); }
    function saveDir() { return setting(persistentSettings.sm64SaveDir) || minerProcess.defaultSaveDir(); }
    function minerThreads() { return persistentSettings.sm64MinerThreads > 0 ? persistentSettings.sm64MinerThreads : minerProcess.defaultThreads(); }
    Component.onCompleted: {
        // Clear the old defaults so the fields show the automatic values.
        ["sm64MinerPath", "sm64RomPath", "sm64SaveDir"].forEach(function(k) {
            if (oldDefaults.indexOf(persistentSettings[k]) >= 0) persistentSettings[k] = "";
        });
        if (["http://localhost:5181/mine", "https://gumbo64.github.io/sm64coin/mine"].indexOf(persistentSettings.sm64WebMinerUrl) >= 0)
            persistentSettings.sm64WebMinerUrl = "";
    }
    function startMiner() {
        if (!walletAddress) { lastError = qsTr("Open a wallet first."); return; }
        var miner = minerPath(), rom = romPath();
        if (!miner) { lastError = qsTr("The automated miner (sm64coin-automine) is not next to the wallet app: give its path below."); return; }
        if (!rom || !minerProcess.fileExists(rom)) {
            lastError = rom ? qsTr("ROM not found: ") + rom
                            : qsTr("No US Super Mario 64 ROM found (next to sm64coin, or in ~/.sm64coin/): give its path below.");
            return;
        }
        logLines = []; curHeight = 0; refProgress = ""; attempts = 0; wins = 0; lastEvent = ""; lastError = "";
        var args = ["--address", walletAddress, "--daemon", daemonUrl(), "--rom", rom, "--save-dir", saveDir()];
        // The campaign miner's threads, and its scratch (step files, the campaign's memory across
        // blocks, its roadmap): the same folder the launcher's --mine uses, so it lasts between sessions.
        args.push("--rh-threads", String(minerThreads()));
        args.push("--rrt-scratch", setting(persistentSettings.sm64SaveDir) ? persistentSettings.sm64SaveDir + "/campaign" : minerProcess.defaultScratchDir());
        var route = minerProcess.defaultRoute();
        if (route) args.push("--route", route);
        var err = minerProcess.start(miner, args, minerThreads());
        if (err) lastError = err;
    }
    function stopMiner() { minerProcess.stop(); }
    // The site: this computer's (the launcher's) when it answers, GitHub Pages as the last resort;
    // a URL in the setting below is used as is.
    function play() { sm64Http.openSite(persistentSettings.sm64WebMinerUrl, "/mine?address=" + encodeURIComponent(walletAddress)); }

    Connections {
        target: sm64Http
        onSiteOpened: playOpened = url
        onSiteError: lastEvent = qsTr("Local site unavailable (%1): opened the GitHub Pages site").arg(message)
    }
    property string playOpened: ""

    Connections {
        target: minerProcess
        onOutput: {
            var l = logLines.slice(-14); l.push(line); logLines = l;
            var m;
            if ((m = /^block (\d+) on /.exec(line))) { curHeight = parseInt(m[1]); attempts += 1; }
            else if ((m = /BLOCK (\d+) ACCEPTED/.exec(line))) { wins += 1; lastEvent = qsTr("Block %1 accepted").arg(m[1]); }
            else if (/giving up/.test(line)) lastEvent = qsTr("Gave up an attempt, trying a new template");
            // The campaign miner's steps (castle / course trees, checkpoints).
            else if ((m = /^step (\d+): (castle at frame \d+[^;]*|\S+ at frame \d+)/.exec(line))) {
                refProgress = qsTr("step %1: %2").arg(m[1]).arg(m[2]);
            }
            else if ((m = /checkpoint (\d+) at node/.exec(line))) {
                refProgress = qsTr("checkpoint %1").arg(m[1]);
            }
            // The route's stages (the RH miner), or the old beam miner's reference index.
            else if ((m = /frame\s+(\d+)\s+obj\s+(\d+)\/(\d+)/.exec(line))) {
                refProgress = qsTr("stage %1 of %2, frame %3").arg(m[2]).arg(m[3]).arg(m[1]);
            }
            else if ((m = /objective (\d+) met at frame (\d+)/.exec(line))) {
                refProgress = qsTr("stage %1 done at frame %2").arg(m[1]).arg(m[2]);
            }
            else if ((m = /step\s+\d+\s+ref\s+(\d+)\/(\d+)/.exec(line))) {
                refProgress = Math.round(100 * parseInt(m[1]) / Math.max(1, parseInt(m[2]))) + "%";
            }
        }
        onFinished: lastEvent = qsTr("Miner exited with code %1").arg(exitCode)
    }

    ColumnLayout {
        id: mainLayout
        Layout.fillWidth: true
        anchors.margins: 20
        anchors.rightMargin: 50
        anchors.topMargin: 0
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.right: parent.right
        spacing: 16

        MoneroComponents.Label {
            fontSize: 24
            text: qsTr("Mining") + translationManager.emptyString
        }

        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("SM64Coin is mined by playing Super Mario 64, not by hashing. A block is sealed by a controller input sequence that, replayed on an emulated Nintendo 64 from power-on, makes Mario get a star while the controller is randomly poked. Blocks come about every 5 minutes; a mined reward unlocks after 4 more blocks (about 20 minutes).") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }

        // ---------------- manual ----------------
        MoneroComponents.Label {
            fontSize: 18
            text: qsTr("Manual mining") + translationManager.emptyString
        }
        MoneroComponents.StandardButton {
            id: playButton
            Layout.preferredWidth: 280
            Layout.preferredHeight: 56
            fontSize: 20
            text: qsTr("Play to mine ▶") + translationManager.emptyString
            enabled: walletAddress !== ""
            onClicked: play()
        }
        MoneroComponents.TextPlain {
            visible: playOpened !== ""
            text: qsTr("Opened %1 in your browser.").arg(playOpened.split("?")[0]) + translationManager.emptyString
            color: MoneroComponents.Style.dimmedFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
        MoneroComponents.LineEdit {
            Layout.fillWidth: true
            labelText: qsTr("Web miner URL") + translationManager.emptyString
            placeholderText: qsTr("automatic: this computer's site (started with the wallet), else gumbo64.github.io") + translationManager.emptyString
            text: persistentSettings.sm64WebMinerUrl
            onTextChanged: persistentSettings.sm64WebMinerUrl = text
        }

        // ---------------- automatic ----------------
        MoneroComponents.Label {
            fontSize: 18
            text: qsTr("Automatic mining") + translationManager.emptyString
        }
        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            text: qsTr("Warning: the automated miner searches for a winning input sequence by simulating the game and uses a lot of CPU (the configured number of threads at 100%).") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.orange
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
        RowLayout {
            spacing: 16
            MoneroComponents.StandardButton {
                small: true
                text: qsTr("Start") + translationManager.emptyString
                enabled: !minerProcess.running && walletAddress !== ""
                onClicked: startMiner()
            }
            MoneroComponents.StandardButton {
                small: true
                primary: false
                text: qsTr("Stop") + translationManager.emptyString
                enabled: minerProcess.running
                onClicked: stopMiner()
            }
            MoneroComponents.TextPlain {
                text: (minerProcess.running ? qsTr("Status: running (pid %1)").arg(minerProcess.pid) : qsTr("Status: stopped")) + translationManager.emptyString
                color: minerProcess.running ? MoneroComponents.Style.green : MoneroComponents.Style.dimmedFontColor
                font.family: MoneroComponents.Style.fontRegular.name
                font.pixelSize: 14
            }
        }
        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            visible: lastError !== ""
            text: lastError
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.errorColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            objectName: "progressLine"
            text: qsTr("Block height: %1   Progress: %2   Attempts: %3   Wins this session: %4").arg(curHeight || "-").arg(refProgress || "-").arg(attempts).arg(wins) + (lastEvent ? "   |   " + lastEvent : "")
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.defaultFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 15 * 16 + 12
            color: "transparent"
            border.color: MoneroComponents.Style.inputBorderColorInActive
            border.width: 1
            radius: 4
            Text {
                objectName: "minerLog"
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                text: logLines.join("\n")
                color: MoneroComponents.Style.dimmedFontColor
                font.family: "monospace"
                font.pixelSize: 11
                elide: Text.ElideRight
                lineHeight: 16
                lineHeightMode: Text.FixedHeight
            }
        }
        MoneroComponents.LineEdit {
            Layout.fillWidth: true
            labelText: qsTr("Automine program") + translationManager.emptyString
            placeholderText: qsTr("automatic: ") + (minerProcess.defaultMinerPath() || qsTr("not found: give the path of sm64coin-automine"))
            text: persistentSettings.sm64MinerPath
            onTextChanged: persistentSettings.sm64MinerPath = text
        }
        MoneroComponents.LineEdit {
            Layout.fillWidth: true
            labelText: qsTr("US Super Mario 64 ROM") + translationManager.emptyString
            placeholderText: qsTr("automatic: ") + (minerProcess.defaultRomPath() || qsTr("not found: give the path of your US .z64"))
            text: persistentSettings.sm64RomPath
            onTextChanged: persistentSettings.sm64RomPath = text
        }
        MoneroComponents.LineEdit {
            Layout.fillWidth: true
            labelText: qsTr("Save mined proofs to (directory)") + translationManager.emptyString
            placeholderText: qsTr("automatic: ") + (minerProcess.defaultSaveDir())
            text: persistentSettings.sm64SaveDir
            onTextChanged: persistentSettings.sm64SaveDir = text
        }
        MoneroComponents.LineEdit {
            Layout.preferredWidth: 200
            labelText: qsTr("Simulation threads") + translationManager.emptyString
            placeholderText: qsTr("automatic: ") + minerProcess.defaultThreads()
            text: persistentSettings.sm64MinerThreads > 0 ? persistentSettings.sm64MinerThreads : ""
            input.inputMethodHints: Qt.ImhDigitsOnly
            onTextChanged: { var n = parseInt(text); persistentSettings.sm64MinerThreads = (n > 0 && n <= 256) ? n : 0; }
        }
        MoneroComponents.TextPlain {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            text: qsTr("Uses the daemon this wallet is connected to and pays the rewards to this wallet's primary address. Leave a field empty for the automatic value. Changes apply the next time you press Start.") + translationManager.emptyString
            wrapMode: Text.Wrap
            color: MoneroComponents.Style.dimmedFontColor
            font.family: MoneroComponents.Style.fontRegular.name
            font.pixelSize: 14
        }
    }
}
