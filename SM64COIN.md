# SM64Coin Wallet (monero-gui fork)

monero-gui v0.18.5.4 (branch `sm64coin`), built against the SM64Coin daemon fork.
All upstream edits are marked `SM64COIN` (`grep -rn SM64COIN . --exclude-dir=build`).

## Layout

- `gui/monero` is a **symlink to `../monero`** (the sm64coin daemon fork), not the upstream submodule.
  The top-level CMake sets `MANUAL_SUBMODULES` since the submodule hash check cannot apply.
- `gui/external/quirc` is the upstream quirc checkout (a plain clone; run
  `git clone https://github.com/dlbeer/quirc external/quirc && git -C external/quirc checkout 927d680904dc95fdff4cd9d022eb374b438ff8f2` if missing).
- Building compiles libwallet and the daemon *again* inside `gui/build` (it also builds the Rust
  `sm64pow` library through the daemon's CMake) and copies `monerod` next to the GUI binary.

## Build (Ubuntu 26.04, GCC 15, Boost 1.90)

```sh
sudo apt-get install -y qtbase5-dev qtdeclarative5-dev qtbase5-dev-tools qttools5-dev qttools5-dev-tools \
  libqt5svg5-dev qtmultimedia5-dev libqt5multimedia5-plugins qtquickcontrols2-5-dev libqt5xmlpatterns5-dev \
  libqt5opengl5-dev qml-module-qtquick2 qml-module-qtquick-controls qml-module-qtquick-controls2 \
  qml-module-qtquick-dialogs qml-module-qtquick-layouts qml-module-qtquick-window2 \
  qml-module-qt-labs-platform qml-module-qt-labs-settings qml-module-qt-labs-folderlistmodel \
  qml-module-qtgraphicaleffects qml-module-qtmultimedia qml-module-qtqml-models2 \
  libgcrypt20-dev libgpg-error-dev libsodium-dev libhidapi-dev libudev-dev libunbound-dev libprotobuf-dev
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DMANUAL_SUBMODULES=1 -DUSE_DEVICE_TREZOR=OFF -DWITH_UPDATER=OFF \
      -DWITH_DESKTOP_ENTRY=OFF -DWITH_SCANNER=OFF -DBUILD_TESTS=OFF ..
make -j8
```

Binary: `build/bin/monero-wallet-gui` (name kept from upstream to keep the merge surface small).

## Run

Desktop, against a node on the same machine (start it with `make node` in the project root):

```sh
build/bin/monero-wallet-gui
```

In the wizard choose "Advanced / remote node" style setup and enter `127.0.0.1:16481`, or let the GUI
start its own `monerod` (local node). Default RPC port is 16481 (testnet 26481, stagenet 36481).
Wallets are stored in `~/SM64Coin/wallets`, settings in `~/.config/sm64coin/sm64coin-wallet.conf`.

Headless smoke test:

```sh
QT_QPA_PLATFORM=offscreen build/bin/monero-wallet-gui --log-level 2 --daemon-address 127.0.0.1:16481
```

## Changes vs upstream

- Identity: app/org name, wallet dir, desktop entry, window title, "XMR" -> "SM64", user-visible "Monero" -> "SM64Coin".
- Default daemon RPC ports 16481/26481/36481.
- Price fetching (kraken/coinpaprika/frankfurter) and its settings removed/hidden; update checks off
  (QML timer off, `WITH_UPDATER=OFF`); donation button hidden (Monero address).
- `pages/Mining.qml` replaced (SM64Coin is mined by playing): "Play to mine" opens the web miner with `?address=<wallet>`;
  Start/Stop runs `sm64coin-automine` as a child process (`MinerProcess`, `src/main/sm64helpers.*`) with the wallet's
  address and daemon, RAYON_NUM_THREADS from settings; paths/URLs persisted as `sm64*` settings in main.qml. Empty paths are automatic
  (`sm64paths` in `sm64helpers.cpp`): the bundle's `sm64coin-automine`, the ROM from `$SM64COIN_ROM` (the launcher sets it and
  `$SM64COIN_DATA_DIR`) or next to the bundle / in `~/.sm64coin`, SHA-1 checked; saves in `<data dir>/mined`, campaign scratch
  `<data dir>/miner` (the launcher's `--mine` uses the same).
- First run = local node: desktop `useRemoteNode` already defaults to false and the wizard's mode selection shows only
  Advanced mode (the simple modes need remote/bootstrap nodes); users who chose a remote node keep it. `main.qml` sets an empty
  `blockchainDataDir` to `<data dir>/node` (`~/.sm64coin/node`) at startup, the wizard's "Reset" returns to it, and the wizard no
  longer turns pruning on: SM64Coin blocks are small and every node replays all of them, so pruning saves nothing and a full node
  serves the whole history to new peers.
- Missing ROM: `startDaemon` (main.qml) calls `askForRom` when `findRom()` finds nothing: a dialog explains what is needed (US ROM,
  .z64, SHA-1, not included), a file picker follows, `MinerProcess.installRom(path)` checks size and SHA-1 and copies the file to
  `<data dir>/baserom.us.z64` (wrong file: error dialog, then asks again), then the node starts. Skipped if the daemon flags carry `--sm64-rom`.
- Node start failures: the daemon is no longer started with `--detach`; it is a child of the wallet (stopped on wallet exit as
  before), so `DaemonManager::onDaemonFinished` sees an early exit and shows the exit code, a hint for port in use / ROM / locked
  database, and the last lines the node printed. `--rpc-bind-port` in the daemon flags is honoured by the wallet (its own
  node address) and by the status/exit commands sent to the node.
- Mining page uses the wallet's daemon address (`currentDaemonAddress`, the local node 127.0.0.1:16481 by default).
- Local node: `DaemonManager` runs the bundle's `sm64coind` (macOS: next to the .app) with `--sm64-rom` from the same search.
- New `pages/Leaderboard.qml` (Advanced > Leaderboard): lists `GET <url>/leaderboard`, Join/Leave POST address + secret
  view key to /register, /unregister. HTTP goes through `Sm64Http` (QML's own network access is blocked by the fork).
- Address validation is libwallet's (`addressValid`), which uses the fork's SM64 prefixes.
- Trezor off by default; desktop-entry prompt off.

## Known gaps

- Payment URIs still use the `monero:` scheme because libwallet (`wallet2::make_uri/parse_uri`) does.
- Translations/images/icons are still upstream Monero ones; "SharedRingDB" page is unchanged.

## Rebrand pass 2

User-visible Monero/XMR text, translations (.ts, source+translation), images (appicons, ico/icns, cards, mining badge, wordmarks, installer bitmaps) replaced with original SM64Coin artwork; Wizard.js restore-height maps from the 2026-11-21 chain start (5 min blocks). Internal identifiers unchanged.
