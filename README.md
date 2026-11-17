# fcitx5-skk

fcitx5-skk is an input method engine for Fcitx5, which uses libskk as its backend.

## このフォークについて

このリポジトリ（`gapul/fcitx5-skk`）は、`fcitx/fcitx5-skk`（GPL-3.0-or-later）の個人用フォークです。`feature/context-server` ブランチは `5.1.11` タグを元にしています。

同じ作者の `gapul/azoo-key-skkserv` が実装している文脈つき辞書要求（opcode `5`）に対応するため、`src/contextserv.cpp` に `FcitxSkkContextServ` という libskk の辞書を追加しています。

### 動作と仕様

- `dictionary_list` の server 辞書の行に `context=true` を付けると `FcitxSkkContextServ` が使われます。
  ```text
  type=server,host=127.0.0.1,port=1178,encoding=UTF-8,context=true
  ```
- `context=true` の行には `okuri=` を指定できます。値は `nasi`（既定）、`ari`、`all` で、その行が答える読みの種類を決めます。`nasi` は送りなし（例: `かがく`）だけ、`ari` は送りあり（例: 「使った」の `つかt`）だけ、`all` は両方に答えます。答えない種類の読みはサーバーに送らず、候補なしを返します。
  - 既定が `nasi` なのは、サーバー（`azoo-key-skkserv`）が送りありの読みを子音だけから変換するのが苦手で（`つかt` で `束`/`付か`/`着か` を返し、`使` を返さない）、他の辞書より前に置くと送りありの変換の先頭がおかしくなるためです。
  - おすすめの `dictionary_list` は、先頭に `context=true`（`okuri=nasi`）の行、次にユーザー辞書と `SKK-JISYO` などのファイル辞書、最後に同じサーバーの `context=true,okuri=ari` の行を置く構成です。こうすると、文脈つきの順番が送りなしの変換の先頭に来て、送りありはファイル辞書の候補が先になり、ファイル辞書にない読み（例: `IreTe` の `いれt`）ではサーバーの候補が出ます。
    ```text
    type=server,host=127.0.0.1,port=1178,encoding=UTF-8,context=true
    type=file,file=$FCITX_CONFIG_DIR/skk/user.dict,mode=readwrite
    type=file,file=/path/to/SKK-JISYO.L,mode=readonly
    type=server,host=127.0.0.1,port=1178,encoding=UTF-8,context=true,okuri=ari
    ```
- サーバーは UTF-8 で応答する必要があります。`encoding` が `UTF-8` でない `context=true` の行は、警告を出して無視します。
- フォーカスのある入力欄が `SurroundingText` を提供しており、かつ `Password` でも `Sensitive` でもないときだけ、カーソルの前後それぞれ最大30文字を文脈として送ります。
- libskk 標準の `SkkServ` と異なり、切れた接続を次の要求でつなぎ直します。また、要求が3秒でタイムアウトした場合は30秒間そのサーバーを使いません。
- 文脈は `127.0.0.1` のサーバーに送る想定です。入力中の文章の一部がサーバーに送られることに注意してください。

### Nix での使い方

Nix では `pkgs.fcitx5-skk.overrideAttrs` で `src` をこのブランチに差し替えて使用できます。

```nix
pkgs.fcitx5-skk.overrideAttrs {
  src = fetchFromGitHub {
    owner = "gapul";
    repo = "fcitx5-skk";
    rev = "feature/context-server";
    hash = "...";
  };
}
```

## Requirements:

 - libskk
 - Qt5 (optional), for rule and dictionary configuration UI.
 - fcitx 5
 - fcitx-qt 5
 - skk-jisyo

### For Ubuntu User

Please install this packages before build this Program.

 - libskk-dev
 - libfcitx5-qt-dev
 - libfcitx5core-dev
 - qtbase5-dev
 - skkdic

    $ sudo apt install libskk-dev libfcitx5-qt-dev libfcitx5core-dev qtbase5-dev skkdic


## Build dependency:

 - cmake
 - C++ Compiler(g++)
 - extra-cmake-modules
 - gettext

You can specify the skk dictionary path by -DSKK_PATH=path_you_want

By default it's /usr/share/skk/

## Installation 

    git clone https://github.com/fcitx/fcitx5-skk.git
    cd fcitx5-skk
    cmake .
    sudo make install
