[English version is here](./README.md)

<img src="https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/images/teaser.png" title="teaser" />

# STAR BREAKER — MSX 用シンプルブロック崩し

**z88dk** (sccz80) でビルドした MSX-DOS `.COM` ゲームです。
try16 で検証した「ビルド → DSK → WebMSX」の流れで開発しました。

現行バージョン: **v4** — MSX2 (SCREEN 5) グラフィックス + PSG BGM/SE、
パフォーマンスチューニング済み（下記「パフォーマンス」参照）、
タイトルロゴは事前レンダリング画像化（下記「タイトル画像の焼き込み」参照）。
v3 の自動プレイベンチマーク（ゲームプレイ 1800 フレーム、WebMSX 上で
JIFFY により計測）:

| ビルド | JIFFY ティック | 実効 fps |
|---|---|---|
| v2 (チューニング前) | 8659 | 12.47 |
| v3 (チューニング後) | 2172 | 49.72 |

## 遊ぶ / ダウンロード

- [WebMSX で遊ぶ](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.DSK&FAST_BOOT)
- [STARBRK.DSK (ブート可能 720KB ディスクイメージ)](https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.DSK)
- [STARBRK.COM (MSX-DOS プログラム)](https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.COM)


## パフォーマンス (v3 チューニング)

v2 は動作が重かったため、コード監査で見つけた4つのホットスポットを
すべて `src/main.c` 内で修正しました（ライブラリは無変更）:

1. **ブロック衝突判定** は毎フレーム全65ブロックを走査し、ブロック毎に
   整数乗算を2回行っていました。現在はボールの位置から、事前計算した
   原点テーブル (`blk_x0[]` / `blk_y0[]`) で候補を最大2×2ブロックに
   絞ります。ブロック帯の外側では範囲チェック1回で済みます。
2. **入力ポーリング** はポーリング毎に最大11回の BIOS (CALSLT) 呼び出し
   を行い、フレーム待ちの間ずっと回っていました。現在はキーボード
   マトリクスを PPI (ポート 0xAA/0xA9、制御ポート上位ニブルを
   保存/復元) から直接読み取り、待ち時間中にラッチします。
   ジョイスティックは BIOS 経由でフレームに1回だけマージします。
3. **HUD 再描画** はブロックを壊すたびに約22回の VDP コマンドを
   発行していました。ラベルは一度だけ描画し、更新されるのは
   スコア/ライフの数字5文字だけです。
4. **PSG 書き込み** は BIOS WRTPSG 経由（1回ごとに CALSLT）でした。
   現在はレジスタへ直接書き込み（ポート 0xA0/0xA1）、シャドウ配列で
   変化のない値を抑制します。スプライト属性（3プレーン連続）は
   フレーム毎に `putspr` 3回ではなく `ldirvm` 1回で転送します。

ベンチマークハーネスは `-DBENCH` を付けたときだけコンパイルされます
（自動パドル、固定1800フレーム、DOS 画面に
`BENCH frames=1800 jiffy=NNNN fps=XX.XX` を表示）。

後段の改修で、ボールがブロックに当たった瞬間に止まって見える問題も
解消しました。以前はヒットしたフレームでブロック消去（背景復元）と
スコア再描画の両方を支払っていたためフレーム境界を越えていました。
両方とも翌フレームへ遅延させ、HUD の数字は変化した桁だけ再描画
します。ヒット直後のフレームのスリップ数（2ティック以上かかった
フレーム、30ヒット中）で計測すると、約30 → HUD 遅延で 8 →
消去も遅延させて **3** になり、同条件の実効 fps は 49.65 → 55.78 に
なりました。

## タイトル画像の焼き込み (v4)

タイトル文字はグリフ単位で描くのをやめ、事前レンダリングした
画像にしました。ロゴ（STAR=黄、BREAKER=緑、白グロス、黒縁）と
その下の小さい2行を、ゲーム自身のパレット/フォントデータから
`tools/gen_title_image.py` で PackBits RLE データ
（`src/title_img.h`、6,103 バイト）に変換し、小さな asm
ブリッタで VRAM へ直接展開します（ブリッタはまず VDP コマンド
エンジンのアイドルを待ちます。コマンド実行中の CPU の VRAM
書き込みはコマンド自身のアドレスポインタに紛れるため）。
タイトル描画全体で **約205 → 約6 JIFFY ティック**。
y=136 の点滅プロンプト「PRESS SPACE TO START」は、2つの
焼き込み帯の間で従来どおり動的に描画します。

## アトラクトデモ

タイトル画面で約5秒間何もしないとデモプレイが始まります
（AI パドル、HUD に "DEMO" 表示）。キーを押すとタイトルへ戻り、
ゲームオーバー / クリアでもタイトルへ戻るので、放置している間
デモがループします。

## ライブラリ

グラフィックス / スプライト / 入力は yamayamaru 氏の **msxalib** と
**msxclib** をベースにし、`src/` へ無変更でコピーしています:

- 出典: https://github.com/yamayamaru/yamayamaru-storage-place/tree/main/source-code_2023-09-30
- ファイル: `msxalib.h` (MD5 `8e8f731a3b9fc1e63f8d3aa18a2b6c6c`)、
  `msxalib.c` (MD5 `7e5733cbb87c85c8923a7879160e5fc0`)、
  `msxclib.h` (MD5 `b27d4e915dc55ab3a4cf8e831f4d3b39`)、
  `msxclib.c` (MD5 `3809e1e7a3b11d8b744e0f97d83c1ed7`)

ライブラリから使用しているもの: `ginit`, `vpoke`, `filvrm`, `ldirvm`,
`ldirmv`, `boxfil`, `line_m`, `cpym2v`, `palett`, `rnd`/`srnd`,
`screen`, `color`, `inispr`, `sprite`, `putspr`, `colspr`,
`gtstck`, `gttrig`, `kilbuf`, `gicini`、およびその下で使われる
`calbio`。（キーボード入力と PSG 書き込みは v3 で高速化のため
BIOS をバイパスしています —「パフォーマンス」参照。）

## ゲーム仕様

- MSX2 専用: SCREEN 5 (256×212 ビットマップ, V9938)、カスタム
  パレット、星空/ストライプ背景、メタリックな壁、光沢ブロック
  （VDP コマンド `boxfil` / `line_m`）。テキストは埋め込み 8x8
  フォントを `cpym2v` で描画: MSXPen の共有コード「8x8 ORIGINAL
  FONT 1 fix」
  (https://msxpen.com/codes/-Nq8q6wabU6mJ4onDmYE) の
  グリフ 0x20..0x7F を `src/main.c` に収録
- ブロック: 13列 × 5行 = 65個、各10点
- パドル: スプライト2個（幅32px）、ボール: スプライト1個、
  16×16 スプライトモード2、行ごとに色指定 (`colspr`)
- PSG の BGM + SE: 近未来シンセウェーブ風ループ —
  ch A の高速アルペジオ（Am–F–C–G、3フレーム/ステップ、プラッキー
  な減衰）に、ch B のオクターブを跳ねるパルスベース。
  効果音 (ch C) はパドル/壁/ブロック/発射/ミス/クリア/オーバー
- 操作: カーソルキー **または** ジョイスティック ポート1、
  SPACE / トリガA でボール発射、ESC で MSX-DOS へ終了
- ライフ3、ボールを落とすとライフ減、全ブロック破壊で GAME CLEAR
- フレーム同期は JIFFY (0xFC9E)、目標 60 fps (NTSC)

## ビルド (Linux)

```sh
make -f Makefile.linux        # -> build-linux/STARBRK.COM
make -f Makefile.linux dsk    # -> build-linux/STARBRK.DSK
```

DSK は try16 付属の `TRY16.DSK` を雛形にして作成します
（MSX-DOS を保持し、`TRY16.COM` を `STARBRK.COM` に差し替え、
`AUTOEXEC.BAT` を `starbrk` 実行に書き換え。`tools/make_dsk.py`）。

## 構成

```
starbreaker/
├─ Makefile.linux
├─ README.md / README.ja.md
├─ images/teaser.png
├─ bin/                 # STARBRK.COM / STARBRK.DSK (リリースバイナリ)
├─ src/
│  ├─ main.c            # ゲーム本体
│  ├─ title_img.h       # 焼き込みタイトル画像 (生成物)
│  ├─ msxalib.{h,c}     # yamayamaru ライブラリ (無変更)
│  └─ msxclib.{h,c}     # yamayamaru ライブラリ (無変更)
├─ tools/make_dsk.py, gen_title_image.py
└─ build-linux/         # STARBRK.COM / STARBRK.DSK (生成物)
```
