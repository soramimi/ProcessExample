# Windows ConPTY 実験メモ

## 目的

Windows ConPTY (`CreatePseudoConsole`) 上でGitを起動し、次の動作を確認する。

- `git --version` の出力を欠落なく取得する
- `git fetch` のSSHホスト鍵確認を対話的に扱う
- ConPTYが出力するVT/ANSI制御シーケンスを除去する
- Qt Creatorなど、外側にも疑似端末がある環境で安定して動作させる

このワークスペースはWindows ConPTY実験用に整理されており、複数プラットフォームやwinptyについて説明している `README.md` とは内容が一致していない。

## 現在のプロセス構成

Qt Creatorの内蔵端末から直接ConPTYを所有すると、外側と内側の疑似端末の寿命や入出力処理が干渉し、出力がタイミング依存になることがあった。このため、現在は同じ実行ファイルを監督プロセスとConPTYワーカーの2段階で実行する。

```text
Qt Creator内蔵端末
  └─ 監督プロセス
       ├─ exec_git_win()
       └─ 通常パイプ
            └─ ConPTYワーカープロセス
                 ├─ exec_git_conpty()
                 └─ ConPTY
                      └─ Git
```

### 監督プロセス

通常起動されたプロセスは次の処理を行う。

1. 実行対象のGitコマンドを組み立てる
2. コマンド文字列をBase64でエンコードする
3. 自分自身を `--conpty-subprocess--` 付きで再起動する
4. ワーカーのstdout/stderrを通常の匿名パイプで受け取る
5. パイプがEOFになるまで全出力を回収する
6. ワーカー終了後、結果をQt Creator側へまとめて表示する

Base64を使用する理由は、Gitコマンド、引用符、Windows OpenSSHのパスなどを、自己再実行用コマンドラインへ安全に渡しやすくするためである。

### ConPTYワーカープロセス

`--conpty-subprocess--` を受け取ったプロセスは次の処理を行う。

1. Base64の引数からGitコマンドを復元する
2. ConPTY入出力用の匿名パイプを作る
3. `CreatePseudoConsole()` で疑似コンソールを作る
4. `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` を設定する
5. `CreateProcessW()` でGitをサスペンド状態で起動する
6. 出力読み取りスレッドを開始する
7. Gitを再開する
8. 入力をConPTYへ転送しながらGitの終了を待つ
9. ConPTYを閉じ、出力スレッドを `join()` する

## プロセスを分離した理由

Qt Creatorの「Run in Terminal」では、実行対象はQt Creatorが管理する端末環境内で起動される。そのプロセスがさらにConPTYを作ると、次の役割が1プロセスに集中する。

- Qt Creatorから監視される実行対象
- 内側のConPTYの所有者
- ConPTY出力の読み取り
- 外側の疑似端末への出力
- Git、ConPTY、読み取りスレッドの終了管理

この構成では、Gitの終了、ConPTYの最終画面生成、出力パイプのEOF、Qt Creatorによる実行終了検知が近いタイミングで発生する。実行タイミングによって `ReadFile()` の分割や最終出力の到着順が変わり、外側の端末が実行終了を処理する時点との競合が疑われた。

プロセス分離後は、監督プロセスが次の完了順序を保証する。

1. ConPTYワーカーがGitを終了させる
2. ワーカーがConPTYを閉じる
3. ワーカーが出力スレッドを `join()` する
4. ワーカーが終了する
5. 監督プロセスが通常パイプをEOFまで読み取る
6. 監督プロセスがQt Creatorへ結果を出力する
7. 最後に監督プロセスが終了する

監督プロセスは、内側のConPTY終了とQt Creator側の端末終了の間に「寿命のバリア」を作る。また、ConPTYから細かく到着する出力を通常パイプで再度まとめるため、Qt Creatorからは内側のConPTYを直接扱わずに済む。

デバッグ実行では、通常は監督プロセスが主なデバッグ対象となり、ConPTYワーカーがデバッガのスレッド停止や終了処理から間接的に離れる可能性もある。ただし、これはQt Creatorの子プロセス追跡設定に依存する。

この分離は完全な隔離ではない。ワーカーは通常の `CreateProcessW()` で起動されるため、環境変数やジョブオブジェクトなどは原則として親の影響を受ける。

参考資料:

- [Qt Creator: Open a terminal](https://doc.qt.io/qtcreator/creator-reference-terminal-view.html)
- [Microsoft: Pseudoconsoles](https://learn.microsoft.com/en-us/windows/console/pseudoconsoles)

## ConPTY出力を失わないための変更

### 入力パイプをGit終了まで保持する

初期実装では、ConPTY入力の書き込み端をGit実行前に閉じていた。ConPTY側では入力チャネル終了として扱われ、`git --version` が出力を書き込む前に終了する場合があった。

入力を行わないコマンドでも、ConPTY入力の書き込み端はGit終了まで保持する。

また、`CreatePseudoConsole()` に渡した `hPipeInRead` と `hPipeOutWrite` は、ConPTYへ接続するGitプロセスを `CreateProcessW()` で作成した後に閉じる。

### 出力を専用スレッドで読み取る

初期実装ではGit終了後に `ReadFile()` を始めていた。この順序には次の問題がある。

- 出力がパイプ容量を超えると相互待ちになる
- ConPTY終了直前の出力を失う可能性がある
- 出力を排出せずに `ClosePseudoConsole()` するとデッドロックする可能性がある

現在はGit再開前に出力スレッドを開始し、ConPTYの実行中から終了処理完了までパイプを排出する。

終了順序は次のとおり。

1. Gitの終了を確認する
2. ConPTY入力の書き込み端を閉じる
3. `ClosePseudoConsole()` を呼ぶ
4. 出力スレッドを `join()` する
5. 出力読み取り端を閉じる

- [Microsoft: Creating a Pseudoconsole Session](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)
- [Microsoft: ClosePseudoConsole](https://learn.microsoft.com/en-us/windows/console/closepseudoconsole)

### 親stdinのEOFでConPTYを早期終了しない

入力転送中に親stdinがEOFになったとき、ConPTY入力も閉じる実装を試した。しかし、入力を必要としない `git --version` まで途中終了した。

親側から入力できなくなった場合は入力転送だけを終了し、ConPTY入力パイプ自体はGit終了まで保持する。

## 実行結果の保持

`ExecResult` はConPTYワーカー内の実行結果を保持する。

| メンバー | 内容 |
|---|---|
| `started` | Gitプロセスを開始できたか |
| `exit_code` | Gitプロセスの終了コード |
| `error_code` | Windows API呼び出しのエラーコード |
| `raw_output` | VTシーケンスを含むConPTY生出力 |
| `text_output` | VTシーケンスを除去したテキスト |

ConPTYから読み取ったデータは次のように処理する。

- 生データを `raw_output` に保存する
- `VtStripper` で制御シーケンスを除去する
- 除去後のテキストをstdoutへ逐次出力する
- 同じテキストを `text_output` に保存する

ワーカーのstdoutは監督プロセスの通常パイプへ接続されているため、最終的には監督プロセスが全テキストを回収する。

## VTシーケンス除去

ConPTYからは、例えば次の制御シーケンスが返る。

```text
ESC [ ? 9001 h
ESC [ ? 1004 h
ESC ] 0 ; <title> BEL
```

`ReadFile()` のチャンク境界はVTシーケンスの境界と一致しない。ある読み取りが `ESC[?` で終わり、次の読み取りが `9001h` から始まることがある。各チャンクを独立して処理すると、後半の `9001h` が通常文字として残る。

状態を読み取り間で保持する `VtStripper` を実装し、次を処理する。

- CSI (`ESC [`)
- OSC (`ESC ]`、BELまたはSTで終了)
- DCS、SOS、PM、APCなどの文字列シーケンス
- 中間バイトを持つESCシーケンス

デバッグ時には、非表示文字を `\x1b` として表示した直後に同じ生データを `fwrite()` していたため、制御シーケンスが二重に見える問題もあった。現在は除去済みテキストだけを `writeOutput` へ渡す。

## `ReadFile()` の結果が毎回変わる理由

匿名パイプはバイトストリームであり、書き込み単位や行単位を保持しない。同じコマンドでも、読み取りは次のように分割され得る。

```text
1回目: "git ver"
2回目: "sion 2.55"
3回目: ".0.windows.2\r\n"
```

個々の `ReadFile()` の結果が異なること自体は正常である。完全な結果として扱えるのは、EOFまで連結した `raw_output` または `text_output` である。

- [Microsoft: Pipe Handle Inheritance](https://learn.microsoft.com/en-us/windows/win32/ipc/pipe-handle-inheritance)

`ReadFile()` が `FALSE` を返した場合は、正常な `ERROR_BROKEN_PIPE` とその他のI/Oエラーを区別すると、実際の欠落を診断しやすい。現在のループはエラー理由を保存していないため、今後の改善候補である。

## `git fetch` のSSHホスト鍵確認

`known_hosts` を削除した状態では、次のプロンプトを期待した。

```text
Are you sure you want to continue connecting (yes/no/[fingerprint])?
```

Git for Windows同梱SSHを使用した場合、この検証環境では次のエラーになった。

```text
Host key verification failed.
fatal: Could not read from remote repository.
```

切り分け結果は次のとおり。

- Windows OpenSSHをConPTYの直接の子として起動するとプロンプトが出る
- 通常の `git fetch` ではプロンプトが出ず、即座に失敗する
- Windows OpenSSHをGitへ明示すると、`git fetch` でもプロンプトが出る

GitがSSHの標準入出力をGitプロトコル用パイプへ接続した状態で、Git同梱のMSYS版SSHがConPTYを確認入力用TTYとして再取得できなかったと考えられる。対話実行ではWindows OpenSSHを明示する。

```text
git -c core.sshCommand="C:/Windows/System32/OpenSSH/ssh.exe" fetch
```

実際のパスは `GetSystemDirectoryW()` で取得し、`OpenSSH/ssh.exe` の存在を確認する。

Windows OpenSSHとGit同梱SSHでは、設定や `known_hosts` の参照先が異なる可能性がある。検証時はGitへ指定したSSHが参照するファイルを確認する。

- [GitHub's SSH key fingerprints](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints)

## 躓きポイント一覧

| 症状 | 原因 | 対応 |
|---|---|---|
| 最初のESCシーケンスしか取得できない | ConPTY入力をGit実行前に閉じていた | 入力書き込み端をGit終了まで保持 |
| Git出力が欠落する | Git終了後まで出力を読んでいなかった | 出力専用スレッドで実行中から読み取る |
| 大量出力時に停止する可能性がある | プロセス待機とパイプ満杯による相互待ち | 待機と出力読み取りを別スレッドに分離 |
| stdinがEOFだと非対話コマンドも終了する | EOFに合わせてConPTY入力を閉じていた | ConPTY入力をGit終了まで維持 |
| `9001h1004h` が残る | VTシーケンスが読み取りチャンクをまたいだ | 状態を保持するVTフィルターを使用 |
| ESCシーケンスが二重に見える | デバッグ表現と生データを両方出力していた | 除去後テキストだけを表示 |
| `git fetch` でホスト鍵確認が出ない | Git同梱MSYS版SSHが確認用TTYを取得できなかった | Windows OpenSSHを明示 |
| Qt Creator内で出力がタイミング依存になる | 外側と内側の疑似端末、実行対象の寿命が近接していた | 監督プロセスとConPTYワーカーを分離 |
| 個々の `ReadFile()` の内容が毎回異なる | 匿名パイプがバイトストリームである | EOFまで全チャンクを連結 |

## 現在の制約と注意点

- Windows 10 version 1809以降または対応するWindows Serverが必要
- Windows OpenSSH Clientが必要
- ConPTYではstdoutとstderrが同じ端末出力として統合される
- `VtStripper` は完全な端末エミュレーターではない
- `\r`、バックスペース、カーソル移動後の最終画面状態までは再構成しない
- コンソール入力転送は文字入力が中心で、矢印キーなどを完全なVT入力へ変換しない
- 監督プロセスの `exec_git_win()` はワーカーの `hStdInput` を設定していない
- そのため、現在の2プロセス構成ではQt Creator側からワーカーへのキーボード入力経路がない
- `git --version` には影響しないが、ホスト鍵確認へ回答するには親からワーカーへの入力パイプが必要
- ワーカーのGit終了コードは監督プロセスへ明示的に伝播していない
- 現在の実装はGitとWindows OpenSSHを前提にした実験コードである

## 最終的に採用した方針

- ConPTYの作成とGit実行は専用ワーカープロセスで行う
- Qt Creatorから直接起動されるプロセスは監督と出力回収に専念する
- 監督からワーカーへはBase64化したコマンドを渡す
- ワーカー出力は通常パイプでEOFまで回収する
- ConPTY内では出力専用スレッドを維持する
- VTシーケンスは状態を保持して除去する
- `git fetch` ではWindows OpenSSHを明示する

この構成により、外側の開発環境の疑似端末と、アプリケーションが所有するConPTYの寿命と入出力を分離している。
