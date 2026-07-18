# ProcessExample - Agent Notes

このドキュメントは、ProcessExample の現在の実装を、作業するエージェント向けにまとめたものです。
人間向けの元々の概覧は `README.md`、Windows ConPTY 実験の経緯は `ConPTY.md` を参照してください。

## 現在の方向性

- もともとは Windows ConPTY (`CreatePseudoConsole`) を使って Git などの対話プロセスを制御する実験コードだった。
- 現在は、アプリ組み込み用のマルチプラットフォーム汎用プロセス起動ライブラリへ作り替えている途中。
- Windows では通常パイプ、ConPTY、winpty、ConPTY worker 分離構成を持つ。
- POSIX (Linux/macOS) では通常パイプと PTY (`posix_openpt`) の実装を持つ。
- `main.cpp` はまだ実験/動作確認用のエントリポイント。ライブラリ本体として見る場合は `AbstractProcess2.h`、`BasicProcess*.h/cpp`、`Process*.h/cpp` が中心。
- `README.md` は変更禁止。現状の作業メモとしては、この `AGENTS.md` と `ConPTY.md` を優先する。

## ビルド

qmake プロジェクトは 2 つある。

Windows / MSVC:

```powershell
qmake process-example.pro
nmake

qmake conpty-worker.pro
nmake
```

Linux / macOS:

```sh
qmake process-example.pro
make
```

`ProcessConPtyWithWorker` を使う Windows 構成では、`process-example.exe` と同じ `_bin` ディレクトリに `conpty-worker.exe` が必要。worker は `conpty-worker.pro` で `CONPTY_WORKER` を定義してビルドする。

## プロジェクト構造

```text
process-example.pro              - 通常の実験/サンプル実行用 qmake プロジェクト
conpty-worker.pro                - Windows ConPTY worker 用 qmake プロジェクト
main.cpp                         - 実験用エントリポイント。Windows/POSIX と worker mode を分岐

AbstractProcess2.h/cpp           - 抽象インターフェース。AbstractProcess / AbstractPtyProcess
BasicProcessWin.h/cpp            - Windows 低レベル実装。通常パイプ / ConPTY
ProcessWin.h/cpp                 - Windows 抽象インターフェース適合ラッパー。通常パイプ / ConPTY / winpty
ProcessConPtyWithWorker.h/cpp    - ConPTY を別 worker exe に分離する Windows 実装
BasicProcessPosix.h/cpp          - POSIX 実装。通常パイプ / PTY

base64.h                         - ConPTY worker へのコマンド受け渡し用 Base64
misc.h/cpp                       - Windows OpenSSH 探索などの補助関数
wstring.h/cpp                    - UTF-8 と Windows wide string / POSIX UTF-16 の変換
unicode_conversion.h/cpp         - UTF-8/UTF-16 変換ユーティリティ

ConPTY.md                        - ConPTY 実験の躓きポイントと設計判断
README.md                        - 元々のプロジェクト概覧。変更禁止
winpty/                          - バンドルされた winpty ライブラリ
```

## レイヤ構成

### 抽象インターフェース

`AbstractProcess2.h` が現在の抽象インターフェース。

- `AbstractProcess`: 通常の stdin/stdout/stderr パイプ型プロセス。
- `AbstractPtyProcess`: 疑似端末型プロセス。`read_output()` キューと `stdout_bytes()` 結果バッファを持つ。
- `AbstractPtyProcess::getMessage()` は deprecated。
- `APP_GUITAR` / `QT_VERSION` が定義される環境では、完了通知やカレントディレクトリ変更用の Qt 連携フックが有効になる。

確定した出力は基本的に `wait()` 後に `stdout_bytes()` / `stderr_bytes()` から読む。実行中の逐次出力は `read_output()` または実装固有の `wait_for_output()` を使う。

### Windows 低レベル実装

`BasicProcessWin.h/cpp` に `_AbstractBasicProcess` と Windows 実装がある。

| クラス | 方式 | 主な特徴 |
|---|---|---|
| `BasicProcessWin` | 匿名パイプ + `CreateProcessW` | stdout/stderr を同じパイプに統合。`Options` で stdout へ中継、結果ベクタ蓄積、逐次キューを切り替える。`wait_for_output()` を持つ。 |
| `BasicProcessWinConPTY` | `CreatePseudoConsole` + `CreateProcessW` | ConPTY を直接所有。入力転送スレッド、出力読み取りスレッド、状態保持型 `VtStripper` を持つ。`Options::vt_stripped` で VT シーケンス除去を切り替える。`Options::no_window` / `set_no_window()` で `CREATE_NO_WINDOW` を切り替える。 |

`BasicProcessWinConPTY::is_conpty_available()` は `kernel32.dll` の `CreatePseudoConsole` を確認する。Windows 10 version 1809 以降が前提。

### Windows ラッパー

`ProcessWin.h/cpp` が `AbstractProcess` / `AbstractPtyProcess` に適合する Windows 側ラッパー。

| クラス | 基底 | ラップ対象 | 備考 |
|---|---|---|---|
| `ProcessWin` | `AbstractProcess` | 独自の `ProcessWinThread` | 通常パイプ版。stdout/stderr を別キューで取得する。環境変数ブロックをキャッシュし、`LANG=en_US.UTF8` を追加する。 |
| `ProcessWinConPty` | `AbstractPtyProcess` | `BasicProcessWinConPTY` | ConPTY 直接所有版。stderr は空扱い。`set_no_window()` を持つ。 |
| `ProcessWinPty` | `AbstractPtyProcess` | winpty | 互換用。現在の主経路ではない。 |
| `ProcessConPtyWithWorker` | `AbstractPtyProcess` | `BasicProcessWin` + `conpty-worker.exe` | ConPTY を別プロセスへ分離する構成。Qt Creator など外側の疑似端末との干渉回避が目的。 |

### ConPTY worker 分離構成

`ProcessConPtyWithWorker` は、監督プロセスと worker プロセスの 2 段階構成。

1. 監督側は `conpty-worker.exe --conpty-worker-- <base64-command>` を `BasicProcessWin` で起動する。
2. worker 側は `ProcessConPtyWithWorker::run_worker(argc, argv)` で worker mode を判定する。
3. worker オプションとして `--no-window` があり、`BasicProcessWinConPTY::Options::no_window` に反映される。
4. コマンド文字列は Base64 で渡す。worker 側は `Base64::decode_checked()` で検証し、空コマンドや NUL 混入を拒否する。
5. worker 側は `BasicProcessWinConPTY` で実コマンドを起動する。
6. worker の stdin/stdout は監督側のパイプにつながる。
7. 監督側は `wait_for_output()` でプロンプトを検出し、`write_input()` で worker 経由の ConPTY へ入力を送れる。

重要な前提:

- `ProcessConPtyWithWorker` は `GetModuleFileNameW(nullptr, ...)` で現在の exe のディレクトリを取り、その隣の `conpty-worker.exe` を起動する。
- worker mode の識別タグは `--conpty-worker--`。
- worker の起動引数が不正な場合や、worker 側で子プロセスを開始できなかった場合は終了コード `128` を返す方針。
- Windows OpenSSH を使う実験では `misc::find_windows_openssh()` で `C:/Windows/System32/OpenSSH/ssh.exe` を探す。

### POSIX 実装

`BasicProcessPosix.h/cpp` は非 Windows 環境でビルドされる。

| クラス | 基底 | 方式 | 主な特徴 |
|---|---|---|---|
| `ProcessPosix` | `AbstractProcess` | `pipe()` + `fork()` + `execvp()` | stdout/stderr を別パイプで取得する。入力はキュー経由で子 stdin へ書き込む。`SIGPIPE` は無視する。 |
| `ProcessPosixPty` | `AbstractPtyProcess` | `posix_openpt()` + `fork()` + `execvp()` | PTY master/slave を使う。`select()` で master fd を監視する。VT ストリッピングは行わない。 |

POSIX 側の注意:

- `ProcessPosix` と `ProcessPosixPty` は、それぞれ別のコマンドライン分割実装を持つ。
- 子プロセスのシグナル終了コードは `128 + signal` に寄せている。
- `ProcessPosixPty::close_input()` は現状空実装。
- `ProcessPosixPty` は `env` 文字列を `putenv()` に渡す簡易実装で、環境ブロック全体の一般的な表現にはまだなっていない。
- `wstring.cpp` の POSIX 側変換は手書き UTF-8/UTF-16 変換を持つ。別途 `unicode_conversion.*` も存在し、変換系は整理途中。

## main.cpp の現状

`main.cpp` はライブラリ API の完成形ではなく、実装確認用のサンプル/実験コード。

Windows:

- `CONPTY_WORKER` 定義時は `ProcessConPtyWithWorker::run_worker(argc, argv)` だけを呼ぶ worker exe 用 main になる。
- 通常ビルドでは固定 `select = 0` により `BasicProcessWin` で `git --version` を実行する。
- `select = 1` で `BasicProcessWinConPTY`、`select = 3` で `ProcessWinConPty`、`select = 4` で worker 分離構成を試す。
- `main_win_conpty_with_worker()` は現在 `git --version` を使う。`git fetch` と SSH ホスト鍵確認プロンプト検出のコードはコメントアウトされている。

POSIX:

- 固定 `select = 0` により `ProcessPosix` で `/usr/bin/git --version` を実行する。
- `select = 1` で `ProcessPosixPty` を使う。

## 重要な設計判断

- ConPTY では stdout/stderr が端末出力として統合されるため、ConPTY 系の `stderr_bytes()` は基本的に空。
- ConPTY 出力には VT シーケンスが混ざるため、`BasicProcessWinConPTY` は `VtStripper` で状態を保持しながら除去する。`ReadFile` のチャンク境界をまたぐシーケンスにも対応する。
- ConPTY の入力転送スレッドは `PeekNamedPipe()` で stdin 側のデータ有無を見てから `ReadFile()` する。終了時に `ReadFile()` で永久待ちになることを避けるため。
- `ProcessConPtyWithWorker` は、外側の疑似端末や IDE 組み込み端末と内側の ConPTY の寿命・ハンドル干渉を避けるため、ConPTY 所有プロセスを分離している。
- Git for Windows 同梱の MSYS ssh は、ConPTY 環境で確認用 TTY を再取得できず `Host key verification failed.` になる場合がある。ホスト鍵確認プロンプトの実験では Windows OpenSSH を明示的に使う。
- `BasicProcessWin` と `BasicProcessWinConPTY` は `Options` で、標準出力への中継、結果バッファ、逐次キューを個別に有効化する。

## 作業時の注意

- このリポジトリは実験からライブラリへ移行中で、古いコメントや文字化けコメントがソース内に残っている。挙動はソースコードを優先して確認する。
- `AbstractProcess.h` ではなく、現行は `AbstractProcess2.h` を使う。
- `main.h` は存在しない。
- `process-example.pro` は通常実験用、`conpty-worker.pro` は worker exe 用。Windows で worker 分離構成を試すなら両方ビルドする。
- `README.md` は変更しない。
- ConPTY の詳細な経緯や既知の躓きポイントは `ConPTY.md` に追記する。
- コード変更時は Windows/POSIX の両方に影響するヘッダ分岐を確認する。特に `_WIN32`、`APP_GUITAR`、`QT_VERSION`、`CONPTY_WORKER` の条件に注意する。

## 既知の未整理点

- `process-example.pro` の `HEADERS` に `wstring.h` が重複している。
- `unicode_conversion.*` と `wstring.cpp` の POSIX 側 UTF 変換が併存している。
- Windows 側には `BasicProcessWin` と `ProcessWinThread` の 2 系統の通常パイプ実装がある。
- `ProcessWinPty` は残っているが、現在の主経路は ConPTY と ConPTY worker 分離構成。
- `main.cpp` は実験選択用の固定 `select` を持つため、ライブラリ利用時の正式なサンプルとしてはまだ整理途中。
- 一部のソースコメントが文字化けしている。必要に応じて、意味を確認しながら段階的に直す。
