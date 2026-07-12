# Windows ConPTY 実験メモ

## この実験の目的

Windows ConPTY (`CreatePseudoConsole`) 上で Git を起動し、次の動作を確認する。

- `git --version` のような非対話コマンドの出力を欠落なく取得する
- `git fetch` が要求するSSHホスト鍵確認を対話的に扱う
- ConPTYが出力するVT/ANSI制御シーケンスを除去し、テキストだけを取得する

このワークスペースは実験用にWindows ConPTY関連だけを残した状態であり、複数プラットフォームやwinptyについて説明している `README.md` とは内容が一致していない。

## 最終的な構成

現在の `main.cpp` は対話版だけを実装している。通常パイプを使う非対話版と、対話・非対話を切り替える `bool interactive` は削除した。

処理の流れは次のとおり。

1. ConPTY入力用と出力用の匿名パイプを作る
2. `CreatePseudoConsole()` で疑似コンソールを作る
3. `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` を設定する
4. `CreateProcessW()` でGitをサスペンド状態で起動する
5. 出力読み取りスレッドを開始する
6. 子プロセスを再開する
7. 親コンソールの入力をConPTYへ転送する
8. 子プロセス終了後にConPTYを閉じ、出力スレッドを終了する
9. 開始結果、終了コード、生出力、テキスト出力を `ExecResult` に保存する

`ExecResult` の役割は次のとおり。

| メンバー | 内容 |
|---|---|
| `started` | 子プロセスを開始できたか |
| `exit_code` | 子プロセスの終了コード |
| `error_code` | Windows API呼び出しのエラーコード |
| `raw_output` | ConPTYから届いたVTシーケンスを含む生データ |
| `text_output` | VTシーケンスを除去したテキスト |

## 主な変更内容

### パイプとConPTYの寿命を修正

初期実装では、ConPTYへ渡したパイプ端を早く閉じすぎていた。また、ConPTY入力の書き込み端も子プロセス実行前に閉じていた。

ConPTY入力を先に閉じると、ConPTY側では入力チャネルの終了として扱われ、`git --version` が本来の出力を書き込む前に終了する場合がある。入力を行わないコマンドでも、入力書き込み端は子プロセス終了まで保持するように変更した。

`CreatePseudoConsole()` に渡した `hPipeInRead` と `hPipeOutWrite` は、ConPTYへ接続する子プロセスを `CreateProcessW()` で作成した後に閉じる。

### 出力を専用スレッドで継続的に読み取る

初期実装では、子プロセス終了を待ってから `ReadFile()` を始めていた。この順序では次の問題がある。

- 出力がパイプ容量を超えると、子プロセスと親プロセスが相互待ちになる
- `ClosePseudoConsole()` のタイミングによって、終了直前の出力を失う可能性がある
- 古いWindowsでは、出力パイプを排出せずに `ClosePseudoConsole()` するとデッドロックする可能性がある

現在は子プロセス再開前に出力スレッドを開始し、ConPTYの実行中から終了処理完了までパイプを排出する。

終了順序は次のようにした。

1. 子プロセスの終了を確認する
2. ConPTY入力の書き込み端を閉じる
3. `ClosePseudoConsole()` を呼ぶ
4. 出力スレッドを `join()` する
5. 出力読み取り端を閉じる

MicrosoftもConPTYの通信チャネルを別スレッドで処理し、終了処理中も出力を排出することを推奨している。

- [Creating a Pseudoconsole Session](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)
- [ClosePseudoConsole](https://learn.microsoft.com/en-us/windows/console/closepseudoconsole)

### 親stdinのEOFでConPTYを閉じない

入力転送処理の途中で、親側stdinがEOFになったときにConPTY入力も閉じる実装を試した。しかし、この動作では入力を必要としない `git --version` まで途中終了した。

親側から入力できなくなった場合は入力転送だけを終了し、ConPTY入力パイプ自体は子プロセス終了まで保持するようにした。

### 実行結果を対話表示と同時に保存

対話モードでは出力を逐次表示するだけで、完了後に利用できる結果を返していなかった。

現在は次の処理を同時に行う。

- 制御シーケンス除去前のデータを `raw_output` に保存する
- 除去後のテキストをstdoutへ逐次表示する
- 同じテキストを `text_output` に保存する

これにより、対話用ConPTY上で非対話コマンドを実行した場合も、画面表示と結果取得の両方ができる。

### VTシーケンス除去をストリーミング対応に変更

ConPTYからは、例えば次のような制御シーケンスが返る。

```text
ESC [ ? 9001 h
ESC [ ? 1004 h
ESC ] 0 ; <title> BEL
```

`ReadFile()` のチャンク境界はVTシーケンスの境界と一致しない。例えば、ある読み取りが `ESC[?` で終わり、次の読み取りが `9001h` から始まることがある。このため、各チャンクを独立して除去すると後半の `9001h` が通常文字として残る。

状態を読み取り間で保持する `VtStripper` を実装し、次の形式を処理するようにした。

- CSI (`ESC [`)
- OSC (`ESC ]`、BELまたはSTで終了)
- DCS、SOS、PM、APCなどの文字列シーケンス
- 中間バイトを持つESCシーケンス

また、デバッグ用に非表示文字を `\x1b` として表示した直後、同じ生データを `fwrite()` していたため、制御シーケンスが二重に見える問題もあった。デバッグ表示を削除し、`writeOutput` には除去済みテキストだけを渡すようにした。

### `git fetch` のSSHホスト鍵確認を表示

`known_hosts` を削除した状態では、当初次のプロンプトを期待していた。

```text
Are you sure you want to continue connecting (yes/no/[fingerprint])?
```

実際には次のエラーになった。

```text
Host key verification failed.
fatal: Could not read from remote repository.
```

切り分け結果は次のとおり。

- Windows OpenSSHをConPTYの直接の子として起動するとプロンプトが出る
- 通常の `git fetch` ではプロンプトが出ず、即座に失敗する
- Windows OpenSSHをGitへ明示すると、`git fetch` でもプロンプトが出る

この検証環境では、Git for Windows同梱のMSYS版SSHは、GitがSSHの標準入出力をGitプロトコル用パイプへ接続した状態で、ConPTYを確認入力用TTYとして再取得できなかった。対話版ではWindows標準OpenSSHを明示することで解決した。

実行コマンドの形は次のとおり。

```text
git -c core.sshCommand="C:/Windows/System32/OpenSSH/ssh.exe" fetch
```

実際のパスは `GetSystemDirectoryW()` を使って取得し、`OpenSSH/ssh.exe` の存在も確認する。

Windows標準OpenSSHとGit同梱SSHでは、設定ファイルや `known_hosts` の参照先が異なる可能性がある。検証時は、実際にGitへ指定したSSHが参照する `known_hosts` を確認する必要がある。

GitHub接続時に表示されたED25519 fingerprintは、GitHub公式値と一致することを確認した。

- [GitHub's SSH key fingerprints](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints)

## 躓きポイント一覧

| 症状 | 原因 | 対応 |
|---|---|---|
| 最初のESCシーケンスしか取得できない | ConPTY入力を子プロセス実行前に閉じていた | 入力書き込み端を子プロセス終了まで保持 |
| Gitの出力が欠落する | 子プロセス終了後まで出力を読んでいなかった | 出力専用スレッドで実行中から読み取る |
| 大量出力時に停止する可能性がある | `WaitForSingleObject()` とパイプ満杯による相互待ち | 待機と出力読み取りを別スレッドに分離 |
| stdinがEOFだと非対話コマンドも終了する | EOFに合わせてConPTY入力を閉じていた | 入力パイプは子プロセス終了まで維持 |
| `9001h1004h` がテキストに残る | VTシーケンスが読み取りチャンクをまたいだ | 状態を保持するストリーミングVTフィルターを使用 |
| ESCシーケンスが二重に見える | デバッグ表現と生データを両方出力していた | 生データの直接表示をやめ、除去後テキストだけを表示 |
| `git fetch` でホスト鍵確認が出ない | この環境ではGit同梱MSYS版SSHが確認用TTYを取得できなかった | `core.sshCommand` でWindows OpenSSHを明示 |
| 診断用 `git fetch` が実行前に失敗する | 保護された `.git/FETCH_HEAD` へ書き込めなかった | ワークスペース内の一時Gitリポジトリで検証 |

## 検証内容

次の動作を確認した。

- 対話ConPTY上の `git --version` が途中終了せず、バージョン文字列を取得できる
- VTシーケンス除去後は `git version ...` だけが表示される
- `known_hosts` がない状態の `git fetch` でSSHホスト鍵確認プロンプトが表示される
- 検証時には `yes` を送信せず、`known_hosts` を変更していない
- 最終的な対話専用コードがMSVCで警告なしにビルドできる

## 現在の制約

- Windows 10 version 1809以降または対応するWindows Serverが必要
- Windows OpenSSH Clientがインストールされている必要がある
- ConPTYではstdoutとstderrが同じ端末出力として統合される
- `VtStripper` は制御シーケンスを除去するが、完全な端末エミュレーターではない
- `\r` による行上書き、バックスペース、カーソル移動後の最終画面状態までは再構成しない
- コンソール入力転送は文字入力を中心としており、矢印キーなどを完全なVT入力へ変換していない
- 現在のコードは実験用に `git fetch` とWindows OpenSSHを前提としている

## 最終的に削除したもの

実験の目標達成後、次を削除した。

- 通常パイプを使う `exec_win()`
- 対話・非対話を選択する `bool interactive`
- モード切り替え用の `ExecOptions`
- 非対話分岐と未使用の環境変数構築処理
- 重複していたUTF-8からUTF-16への変換関数
- 不要になったヘッダーと一時的なラッパー構造

最終コードは、Windows ConPTYを使ったGitの対話実行に限定している。
