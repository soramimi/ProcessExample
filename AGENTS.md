# ProcessExample — Agent Notes

このドキュメントは、ProcessExample プロジェクトの**現在の実験的実装**をまとめたものです。人間向けの概覧は `README.md`、ConPTY 実験の詳細は `ConPTY.md` を参照してください。

## プロジェクト概要

- **目的**: 子プロセス（主に Git）を起動し、その入出力を確実に取得・制御する実験コード。もともと Windows ConPTY (`CreatePseudoConsole`) 専用だったが、**POSIX (Linux/macOS) 対応を追加し、クロスプラットフォームビルドに対応した**。
- **主要な実験シナリオ（Windows 側）**: `known_hosts` を削除した状態で `git fetch` を実行し、SSH のホスト鍵確認プロンプト (`Are you sure you want to continue connecting...`) を検出して `no\n` を送信し、フェッチを拒否する一連の動作を確認する。
- **ターゲットプラットフォーム**: Windows（ConPTY, Win32 API）と POSIX（Linux/macOS, `fork`/`exec`/`pipe`/`posix_openpt`）の両方。`process-example.pro` は `win32:` スコープで Windows 専用ファイルを分離し、非 Windows 環境では `BasicProcessPosix.h/cpp` のみをビルドする。

## ビルド方法

qmake でビルドします。プラットフォームに応じて Windows 専用ファイル / POSIX 専用ファイルが自動的に選択されます。

Windows（MSVC / nmake）:

```powershell
qmake process-example.pro
nmake
```

Linux / macOS:

```sh
qmake process-example.pro
make
```

実行バイナリは `_bin/process-example`（Windows は `.exe` 付き）に出力されます。

## プロジェクト構造

```
process-example.pro         — qmake プロジェクトファイル（win32: スコープで Windows 専用ファイルを分離）
main.cpp                    — エントリポイント。#ifdef _WIN32 で Windows 版 main / POSIX 版 main を切り替え

AbstractProcess.h           — プロセスの抽象基底クラス（AbstractProcess, AbstractPtyProcess）
AbstractProcess.cpp         — AbstractProcess 実装（getMessage / notify_completed 等）
BasicProcessPosix.h/cpp     — POSIX プロセス実装（PosixProcess, PosixPtyProcess）。全プラットフォーム共通でビルド対象（内部は #ifdef _WIN32 で Windows 時は無効化）
BasicProcessWin.h/cpp       — Windows プロセス実装（BasicProcessWin, BasicProcessWinConPTY）。win32 のみビルド
ProcessWin.h/cpp            — AbstractProcess / AbstractPtyProcess 継承ラッパー（ProcessWin, ProcessWinConPty, ProcessWinPty）。win32 のみビルド
ProcessConPtyWithWorker.h/cpp — worker/subprocess 構成の ConPTY 実装（AbstractPtyProcess 継承）。win32 のみビルド

misc.h/cpp                  — ユーティリティ（UTF-8/Wide 変換、コマンドライン解析、find_windows_openssh は win32 限定）
base64.h                    — Base64 エンコード/デコード（ヘッダオンリー）
winpty/                     — バンドルされた winpty ライブラリ（現在未使用）
ConPTY.md                   — ConPTY 実験メモ（躓きポイント、設計判断の記録）
README.md                   — 人間向けのプロジェクト概覧（変更禁止）
```

## 主要クラス

### 基本実装（BasicProcessWin.h）

| クラス | 役割 |
|---|---|
| `BasicProcessWin` | 匿名パイプ + `CreateProcessW` で子プロセスを起動。入出力双方向パイプ、蓄積出力バッファ (`std::string`)、プロンプト検索 (`wait_for_output`) を持つ。 |
| `BasicProcessWinConPTY` | ConPTY を直接所有するワーカー実装。入出力転送スレッド、VT シーケンス除去 (`VtStripper`) を内包する。 |

### POSIX 実装（BasicProcessPosix.h、AbstractProcess / AbstractPtyProcess 直接継承）

| クラス | 基底クラス | 実装方式 | 特徴 |
|---|---|---|---|
| `PosixProcess` | `AbstractProcess` | `pipe()` + `fork()` + `execvp()` | 標準入出力・標準エラーをそれぞれ別パイプで取得。`OutputReaderThread` が stdout/stderr を個別スレッドで読み取り、`UnixProcessThread` が子プロセスの起動・入力書き込み・終了待機を担当する。`stderr_bytes()` は Windows 版と異なり分離されて取得できる。 |
| `PosixPtyProcess` | `AbstractPtyProcess` | `posix_openpt` + `fork()` + `execvp()`（疑似端末） | PTY マスタ/スレーブを生成し、子プロセスの標準入出力をスレーブ側に接続する（`forkpty` 相当を手動実装）。`select()` でマスタ側 fd を監視し出力を読み取る。ConPTY 版と違い VT ストリッピングは行わない。 |

### AbstractProcess / AbstractPtyProcess 継承ラッパー（ProcessWin.h）

| クラス | 基底クラス | ラップ対象 | 特徴 |
|---|---|---|---|
| `ProcessWin` | `AbstractProcess` | `BasicProcessWin` | 標準パイプ版。`start(command, use_input)`。`stdout_bytes`/`stderr_bytes` を `std::vector<char>` で提供。stderr は stdout と統合されるため空。 |
| `ProcessWinConPty` | `AbstractPtyProcess` | `BasicProcessWinConPTY` | ConPTY 版。`start(command, env, use_input)` で環境変数文字列も受け取る。`stderr_bytes()` は空。 |
| `ProcessWinPty` | `AbstractPtyProcess` | winpty | winpty 版（現在未使用）。 |
| `ProcessConPtyWithWorker` | `AbstractPtyProcess` | `BasicProcessWin` + `BasicProcessWinConPTY` | **監督プロセス/ワーカープロセスの 2 段階構成**。自分自身を subprocess タグ付きで再起動し、ConPTY は分離したワーカー内で所有する。 |

### AbstractProcess / AbstractPtyProcess インターフェース

```cpp
class AbstractProcess {
public:
    virtual void start(const std::string &command, bool use_input) = 0;
    virtual int wait() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;
    virtual int get_exit_code() const = 0;
    virtual void write_input(char const *ptr, int len) = 0;
    virtual void close_input() = 0;
    virtual std::vector<char> const &stdout_bytes() const = 0;
    virtual std::vector<char> const &stderr_bytes() const = 0;
};

class AbstractPtyProcess {
    // ...
    virtual void start(std::string const &cmd, std::string const &env, bool use_input) = 0;
    virtual int wait() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;
    virtual int get_exit_code() const = 0;
    virtual void write_input(char const *ptr, int len) = 0;
    virtual int read_output(char *ptr, int len) = 0;
    virtual void close_input() = 0;
    // stdout_bytes / stderr_bytes も同様
};
```

- `stdout_bytes()` は実行中でも呼べるよう、内部バッファを逐次更新する。`const` メソッドだが `mutable` なキャッシュを更新するラッパー実装がある。

## ProcessConPtyWithWorker の動作フロー

1. **エントリポイント (`main`)**:
   - まず `ProcessConPtyWithWorker::run_worker(argc, argv)` を呼び出す。`--conpty-subprocess--` 引数があればワーカーモード。
   - ワーカーモード: Base64 デコードしたコマンドを `BasicProcessWinConPTY` で実行し、終了コードを返して即終了。
   - 監督モード: `main_win_conpty_with_worker` へ進む。

2. **監督モード**:
   - `start(cmd, {}, true)`（シグネチャは `start(command, env, use_input)`）で自分自身を subprocess タグ付きで再起動。`BasicProcessWin` でパイプ接続。
   - `wait_for_output("Are you sure you want to continue connecting")` でプロンプト検出。
   - 検出後 `write_input("no\n", 3)` を送信。
   - `close_input()` → `wait()` で終了待ち。

3. **ワーカーモード**:
   - 親からパイプで接続された stdin/stdout を ConPTY 入出力パイプに中継。
   - 出力スレッドで VT シーケンスを除去し、親へ逐次転送。

## 注意点・制約

- **実験的コード**: エラーハンドリングやタイムアウトは最小限。本番利用を意図していない。
- **Windows 限定**: ConPTY (`CreatePseudoConsole`) は Windows 10 version 1809 以降が必要。
- **SSH の種類**: Git for Windows 同梱の MSYS 版 SSH は ConPTY 環境で確認用 TTY を再取得できない場合がある。Windows OpenSSH (`C:\Windows\System32\OpenSSH\ssh.exe`) を明示的に指定する (`misc::find_windows_openssh`)。
- **プロセス構成**: `ProcessConPtyWithWorker` は「Qt Creator など外側の疑似端末」と「内側の ConPTY」の寿命干渉を避けるため、プロセスを分離している。
- **入出力統合**: ConPTY では stdout と stderr が統合されるため、`stderr_bytes()` は基本的に空。
- **VT シーケンス**: `BasicProcessWinConPTY` の出力スレッド内で `VtStripper` が状態を保持しつつ除去する。`ReadFile` のチャンク境界をまたぐシーケンスにも対応。
- **POSIX 対応**: `BasicProcessPosix.h/cpp` に `PosixProcess`（パイプ）/ `PosixPtyProcess`（疑似端末）を追加し、Linux/macOS でもビルド・実行できるようにした。`main.cpp` は `#ifdef _WIN32` で Windows 版 `main` と POSIX 版 `main`（`main_basic_posix` / `main_basic_posix_pty` を切り替え）を分離している。`process-example.pro` は `win32:` スコープを使い、Windows 専用ソース（`BasicProcessWin.*` 等）と共通ソース（`BasicProcessPosix.*` 等）を分けてビルドする。
- **POSIX 側の制約**: `PosixPtyProcess` は VT ストリッピングを行わない（生の PTY 出力をそのまま返す）。`misc::convert_str_to_wstr` / `convert_wstr_to_str` の POSIX 版は未実装（空実装）。`PosixProcess::parseArgs` / POSIX 側 `make_argv` は Windows 側と別実装で、コマンドライン解析ロジックが重複している。

## 変更履歴（このセッションで実施）

1. `BasicProcessWin` / `BasicProcessWinConPTY` に `isRunning()`, `getOutput()`/`stdout_bytes()`, `getExitCode()` を追加し、メモリバッファリング機構を導入。
2. `ProcessWin` / `ProcessWinConPty`（`AbstractProcess` / `AbstractPtyProcess` 継承ラッパー）を新規作成。
3. `ProcessConPtyWithWorker`（`AbstractPtyProcess` 継承）を新規作成。worker/subprocess 分離構成をクラス化。
4. `misc` に `find_windows_openssh()` を移動し、再利用可能に。
5. `main.cpp` を `ProcessConPtyWithWorker` を使う形に整理。エントリポイントで worker モードを判定するように変更。
6. `process-example.pro` に新規ファイルを追加。
7. **`main.h` を削除、`AbstractProcess.cpp` / `misc.cpp` / `base64.h` を追加。** クラス構成を `BasicProcessWin.h` と `ProcessWin.h` に整理した現状に合わせて `AGENTS.md` を更新。
8. **POSIX (Linux/macOS) 対応を追加。** `BasicProcessPosix.h/cpp`（`PosixProcess` / `PosixPtyProcess`）を新規作成し、`fork`/`execvp`/パイプ/`posix_openpt` ベースの実装をクロスプラットフォームで利用可能にした。`main.cpp` / `misc.cpp` / `AbstractProcess.h/cpp` を `#ifdef _WIN32` で Windows/POSIX 分岐するように整理し、`process-example.pro` を `win32:` スコープで Windows 専用ファイルと共通ファイルに分割。これによりプロジェクトは Windows 専用実験から Windows/POSIX 両対応のクロスプラットフォームコードベースになった。
9. **POSIX 実装のエラー処理・堅牢性を修正。** 実機（Linux, GCC 16）で再現・検証した上で以下を修正:
   - 子プロセス終了後に `write_input()` すると `SIGPIPE` でアプリ全体が終了していたのを、`SIG_IGN` 設定と `write()` 失敗時のフォールバックで修正（`BasicProcessPosix.cpp`）。
   - `PosixPtyProcess` で `execvp` が失敗すると、forkした子プロセスがそのままスレッド関数を抜けて終了コード `0`（成功）を返してしまっていたのを、`_exit(127)` で明示的に失敗させるよう修正。`posix_openpt`/`grantpt`/`unlockpt`/`ptsname`/`open` の戻り値チェックも追加。
   - `pipe()`/`fork()` がリソース枯渇等で失敗した際、ワーカースレッド内から `exit(1)` を呼びホストアプリ全体を巻き込んで終了させていたのを、失敗を呼び出し元へ返すだけに修正。
   - forkした子プロセス側の `execvp` 失敗時に `exit()` ではなく `_exit()` を使うよう修正（親のCライブラリバッファを二重にflushしないため）。
   - `PosixProcess::get_exit_code()` が `wait()` 後は常に `-1`（内部状態のリセットで終了コードを喪失）を返していたのを、`wait()` 側でキャッシュするよう修正。
   - `AbstractProcess.h` に不足していた `#include <string>` / `<vector>`、`BasicProcessPosix.cpp` に不足していた `#include <atomic>` を追加（`-std=c++17` 明示時のみ顕在化するビルド失敗だった。macOS の libc++ 等でも壊れる可能性があったため、`process-example.pro` に `CONFIG += c++17` を追加して常にこの水準でコンパイルされるようにした）。
   - `misc::convert_str_to_wstr`/`convert_wstr_to_str` の POSIX版が空の関数本体で戻り値なし（未定義動作）だったのを、明示的に空値を返すよう修正（現状 POSIX 側からの呼び出しはなく、未実装であることをコメントで明記）。

## 参考

- `ConPTY.md` — 各種躓きポイント、設計判断、終了順序の詳細。
- `README.md` — オリジナルのプロジェクト概要（複数プラットフォーム比較の文脈で記述されている。**変更禁止**）。POSIX 対応後の現状は本ファイルの記述が最新。
