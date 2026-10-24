# WhateverXe

[English](README.md)

WhateverXeは、一部のIntel Xe-LP内蔵GPU向けにAppleのグラフィックスドライバを適応させるLiluプラグインです。WhateverGreenを基に開発しています。

本プロジェクトは実験段階であり、[OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher)が提供するXe root patchを必要とします。

## 対応範囲

現在の対象は **macOS Big Sur build 20G1443** です。

| GPUファミリー | バックエンド実装があるdevice ID |
| --- | --- |
| Tiger Lake | `9A40`、`9A49`、`9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |
| Alder Lake-P GT2（実験的） | `46A6`、PCI revision `0C`のみ |

対応状況は機種やコネクタ構成によって異なります。この表はバックエンドの実装範囲を示すもので、すべての機種での動作を保証するものではありません。詳細な選択条件と上書き設定は[デバイス選択](docs/device-selection.md)を参照してください。

Tiger LakeとAlder Lake-Nで、外部2画面の1920×1080・60Hz出力、Metal/OpenGL、ハードウェアHEVC、画面の消灯／復帰を確認しています。Tiger LakeではハードウェアH.264のエンコード／デコードも確認しています。Alder Lake-Pの項目は、厳密に`46A6` D0／Display-13に限定したbring-up profileであり、実機での追加検証が必要です。他のAlder Lake-Pシステムへの互換性を示すものではありません。厳密に一致するfirmware panel route（DDI A、pipe A、AUX `0x40`、native DP mode）に限り、保護された内蔵eDP lifecycle candidateがあります。これはopt-inの実験パスであり、他の内蔵画面、スリープ／復帰、USB-C、その他のコネクタ構成への対応を示すものではありません。3画面同時出力とより高い解像度には追加検証が必要です。

[Releases](https://github.com/b00t0x/WhateverXe/releases)から **RELEASE** ZIPをダウンロードしてください。DEBUG ZIPは問題調査用です。

## 必須構成

| コンポーネント | 役割 |
| --- | --- |
| Lilu | WhateverXeの動作に必要なランタイム |
| WhateverXe.kext | OpenCoreでLiluより後に読み込む |
| [OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher) | Xe root patchを適用してグラフィックス構成を導入する |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/OpenCore-Legacy-Patcher/blob/mod/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-Mod-RELEASE.zip) | 改変されたroot patchのグラフィックス構成に必要。OpenCoreで導入する |

OCLP forkは[Releasesページ](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases)からダウンロードしてください。rootの構成要素の導入とkernel collectionの更新はOCLPが行います。

リンク先の **Mod版** アーカイブから`AMFIPass.kext`を展開し、`EFI/OC/Kexts`へ配置して、`Kernel > Add`で有効にしてください。既存のAMFIPassがある場合はこのMod版へ置き換え、両方を同時に読み込まないでください。

複数GPUの構成を含め、WhateverGreenとWhateverXeは同時に有効にしないでください。

## 設定

GPUはPCI device IDから自動選択します。対応範囲内ではファームウェアのVBTから配線を読み取るため、固定HDMI／固定DisplayPort構成は通常、配線propertyの手動指定が不要です。高度な設定としての`device-id`上書きは[デバイス選択](docs/device-selection.md)で説明しています。

### DVMT

BIOS/UEFIで変更できる場合は、**DVMT Pre-Allocatedを96MBに設定することを推奨します**。

**60MBから変更できない場合**は、`boot-args`に`-igfxdvmt`を追加し、OpenCoreの`DeviceProperties > Add`にあるiGPUの項目（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）へ以下の2つを設定してください。

| Property | 型 | 値（16進数） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

Data値はリトルエンディアンです。`-igfxdvmt`は容量計算を補正し、DevicePropertiesは表示用メモリ領域を60MB内に収めます。ファームウェアが確保したメモリを増やす設定ではありません。96MBの場合、これらのDVMT用設定は不要です。

### 必須ブート引数

OpenCoreの`boot-args`に以下を追加してください。

```text
-allow3d -disablegfxfirmware
```

| 引数 | 用途 |
| --- | --- |
| `-allow3d` | Appleドライバの3D処理を有効にする。 |
| `-disablegfxfirmware` | 現在のバックエンドで、Appleドライバのグラフィックスファームウェア読み込みを無効にする。 |
| `-wex-nopanel` | 内蔵画面のないTiger Lake機で追加する。現在のAlder Lake-N / Twin Lakeは外部画面用の構成を自動生成するため、この引数を使用しない。 |

Alder Lake-N / Twin Lakeでは、世代選択や個別機能の有効化引数を追加する必要はありません。厳密なAlder Lake-P `46A6` D0 eDP candidateでは、下記の明示的な任意引数も必要です。

### 任意のブート引数

| 引数 | 用途 |
| --- | --- |
| `gfxrst=` | 起動時framebufferの引継ぎ方式。0：自動（既定）、1：reset、2：copy、3：clear、4：無効。 |
| `-wexdbg` | debug buildでログを有効にする。 |
| `-wexoff` | WhateverXeを無効にする。 |
| `-wexbeta` | プラグインのOSバージョンチェックを回避する。kernel／driverの一致条件は維持する。 |
| `-wex-tgl-off` | Tiger Lakeの処理を無効にする。 |
| `-wex-adlp-any-pipe` | 実験用の`46A6` D0限定admission。検証済みの単一firmware scanoutがDisplay-13のpipe BまたはCの場合も許可し、その実測pipeをboot handoff、power、IRQ検証まで維持する。`-allow3d`と`-disablegfxfirmware`を維持し、外部1画面の実験とpreflight結果の取得にのみ使用した後は削除する。 |
| `-wex-adlp-edp` | 実験用の厳密な`46A6` D0内蔵panel lifecycle candidate。firmware VBTのDDI A / pipe A / AUX `0x40` / native DP modeを必要とし、上記必須引数も必要。HDMI、Type-C、alternate pipeは有効にせず、削除するとframebuffer-only fallbackへ戻る。 |

## ロードマップ

### Planned

- OCLPの機能を利用したmacOS Monterey〜Tahoe対応
- Alder Lake／Raptor LakeのS/H/P/U対応
- USB-C画面出力対応
- 一般的な内蔵ディスプレイ対応

### Not Planned

- Xe-LPG対応
- Xe2以降のアーキテクチャへの対応

## ビルドとテスト

macOS、Xcode command-line tools、Python 3.9以降、MacKernelSDK、Liluの開発用ヘッダ／ライブラリを使用します。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に配置するか、`--sdk`と`--lilu`でパスを指定してください。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

kextとソースのmanifestは`build/`に出力されます。任意のdonor再生試験には`--donor /path/to/AppleIntelTGLGraphicsFramebuffer`と`--accelerator /path/to/AppleIntelTGLGraphics`を指定します。各試験が受け付ける正確なバイナリが必要です。CPUテストだけで実GPUの互換性が確認できるわけではありません。ビルドにWhateverGreenのソースは不要です。

## ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。LiluとMacKernelSDKは別の依存コンポーネントです。
