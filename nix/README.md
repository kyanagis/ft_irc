# nix 環境

`nix/flake.nix` で同じバージョンのツールを使う．
`nix/flake.lock` を commit しているので，誰の環境でも clang 21.1.8 / llvm 21.1.8 / cppcheck 2.18.3 で固定される．
perl や python が入っていない環境でも動く．必要なものは全部 nix が持ってくる．

## 初回インストール

- **macOS (Apple Silicon)**
  ```sh
  curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install
  ```
- **WSL2 (Ubuntu)** 
  ```sh
  curl --proto '=https' --tlsv1.2 -sSf -L https://install.determinate.systems/nix | sh -s -- install
  ```
- インストール後は**ターミナルを開き直す**（PATH に `/nix/var/nix/profiles/default/bin` が入る）
- 確認
  ```sh
  nix --version   # 2.31 以上
  ```

flake 機能が無効だと言われたら `~/.config/nix/nix.conf` に以下を追記する（Determinate インストーラなら既定で有効）．

```
experimental-features = nix-command flakes
```

## 使い方

- 環境に入る（リポジトリのルートで実行．flake が `nix/` にあるので `./nix` が要る）
  ```sh
  nix develop ./nix
  ```
- 抜ける
  ```sh
  exit
  ```
- 入らずに 1 コマンドだけ実行する
  ```sh
  nix develop ./nix --command make re
  ```
- 初回は数分かかる（irssi / weechat / llvm を取ってくる）．2 回目以降は一瞬

## direnv（任意だけど楽になるよ）

`cd` するだけで環境が有効になり，出ると自動で外れる．

- 入れる
  ```sh
  nix profile install nixpkgs#direnv
  ```
- shell に hook を追加（bash なら `~/.bashrc`，zsh なら `~/.zshrc`）
  ```sh
  eval "$(direnv hook bash)"   # zsh の場合は bash を zsh に
  ```
- リポジトリのルートで 1 回だけ許可する
  ```sh
  direnv allow
  ```

## この環境で動くコマンド

- 提出物ビルド
  ```sh
  make re && ./ircserv 6667 pass
  ```
- 検証ハーネス（ASan + UBSan）
  ```sh
  make -C verify verify && ./verify/verify all
  ```
- ファジング（30 秒）
  ```sh
  make -C verify fuzz && ./verify/fuzz -max_total_time=30 verify/corpus
  ```
- 結合テスト（サニタイザ版サーバ）
  ```sh
  make -C verify ircserv_asan
  IRCSERV_BIN=verify/ircserv_asan IRC_TEST_REGISTRATION=1 python3 verify/integration.py
  ```
- 静的解析
  ```sh
  cppcheck --enable=warning,performance,portability --std=c++03 -I include src
  clang-tidy $(find src -name '*.cpp' | sort) -- -std=c++98 -I include -I include/commands
  ```
- 手動テスト用クライアント
  ```sh
  irssi -c 127.0.0.1 -p 6667 -w pass
  weechat
  ```

## メンテ

- パッケージを更新する（`nix/flake.lock` が書き換わるので commit する）
  ```sh
  nix flake update --flake ./nix
  ```
- ツールを足す — `nix/flake.nix` の `packages` に 1 行足すだけ．パッケージ名は https://search.nixos.org/packages で探す
- 使わなくなった分を消す（`/nix/store` が肥大したら）
  ```sh
  nix store gc
  ```

## misc

- **flake は git 管理下のファイルしか見ない**．`nix/flake.nix` を新規追加した直後は `git add` しないと "path does not exist" になる
- 引数なしの `nix develop` は `not part of a flake` で落ちる．`./nix` を必ず付ける
- `nix develop ./nix` は**リポジトリのルート**で実行する．環境が効くのは flake の場所ではなく，コマンドを打ったディレクトリ
- ファジングを走らせると libFuzzer が `verify/corpus/` に新しい入力を書き足す．commit する前に `git status` を見て，意図しない分は消す

## CI との差分

ローカルの nix と GitHub Actions ではツール版が違うため，以下 2 つはローカルでだけ出る．コードの欠陥ではない．

- `cppcheck` 2.18 の `normalCheckLevelMaxBranches`（解析打ち切りの通知．CI の 2.13 では出ない）
- `clang-tidy` の `bugprone-exception-escape`（macOS は libc++，CI は libstdc++ という標準ライブラリ差）

CI 側も nix を使えばこの差は消えるので，多分直す
