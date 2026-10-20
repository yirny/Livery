Outfit Management UI実装

# Vertical Tab UI

初期画面の右側にアイコンのみで構成されるtab uiを実装する。

添付のTailorのUIを参考にする。

現在あるメイン画面の左側に罫線を引いて分割する。
Tailor ThemeはTailor UIを参考。
Nordic Themeは添付画像(select an attribute...)の左右にある罫線画像を入手できるか試す。
Vanilla Themeは2pxのグレーの直線。

Nordic Themeの四隅の装飾はvertical tabも含めて1つのウィンドウという扱いで表示する。

アイコンはまず「Inventory」と「Manage Outfits」の2つ。この後「Manage Situations」も追加予定。

アイコンは以下の図形をあなたに描いて欲しい:

- Inventory: バックパックのアイコン
- Manage Outfits: 添付画像のような衣装台のアイコン
- Manage Situations: シンプルな雪の結晶のアイコン

表示はアイコンのみで、アイコンにマウスオーバーで上記のメニュー名がツールチップとして表示される。

初期画面（今後Inventory画面と呼ぶ）とManage Outfits画面でのみ表示され、Load Outfit...やSaveの詳細設定画面などでは表示されない。

タブ変更しても編集内容は破棄されない（元のタブに戻ると編集された状態が復元される。）

# Manage Outfits

Vertical TabのManage Outfitsボタンから遷移。

タイトルは Manage Outfits。

左ペインはLoad Outfit, Update Existingと同じもの。

右ペインはそれぞれの項目を編集可能にする（Create New/Update Existing時と同じ内容+装備品リスト）。

装備品リストには [+] Edit ボタンを表示。

Edit押下で、そのOutfitを加えた状態でequip piece選択画面に遷移する（Inventory画面で該当OutfitをLoad Outfitしたのとほぼ同じ状態。）
違いとして、Vertical Tab UIがなく、ボタンが Update pieces of #{Outfit Name} と Back の2つ。
Update押下で、そのpiecesに更新された状態でManage Outfit画面に戻る。

Manage Outfits画面のボタンはUpdate, Remove, Cancel。

Updateは何かしらの編集を加えるまではグレーアウト。

Update押下でその装備データを更新。

Update押下せず別画面に遷移しても編集状態は維持される。

Updateしていない編集が残った状態でManage Outfitsを抜けようとすると警告。You have unsaved changes for: ... みたいな。

Inventory画面に移った場合、その時点で上記の警告が出るが、updateせずに遷移しても編集内容をその場で破棄はしない。ただApplyなどでLiveryを閉じる（=編集が破棄される）操作をしても警告は出さない。

Removeでそのoutfitを削除。削除前に警告。

CancelでLivery終了。

# Outfit List UI （および一部共通パーツ） 改修

Load Outfit, Update Existing, および今回のManage Outfit画面で使うoutfit選択UIの改修。

上記すべて共通の装備選択ウィジェットを使うものとする。

Armor ClassをClassと省略するのはpieceリストのヘッダのみで、他の箇所（カテゴリ選択の小見出しなど）ではArmor Classと表記する。

カテゴリからJewelryを削除（装備一覧のtypeからは残す）。

Outfitリストの各項目を複数行対応にし、次の各情報を2行目以降にグレーで小さく表示する。Tailor UIを参考に。

- Female, Male (unisexは表示しない)
- Armor class
- Custom category
- Situation

上記の各追加情報は、リスト下にShow Categoriesチェックボックス（デフォルトON）を追加し、そこで表示・非表示切り替えられるようにする。

Filterウィジェットを一旦非表示に（再度表示する可能性大なのですぐ戻せるように）

Sort by...を拡張、以下の選択肢にする。

- Name
- Gender
- Armor Class
- Custom Category（なければ非表示）
- Situation

Name以外でソートした際、リストに小見出しをつける。
Genderの小見出しはFemale, Male, Unisex。
他はカテゴリ名をそのまま小見出しに。
複数ある場合は Town, Swimming などとカンマで区切る。

# その他

Inventory画面の [+] Load Outift... ボタンを削除、ボタンリストに同機能の Load ボタンを一番左に表示

各ボタンに次のtooltipを表示（英文は自然になるようにそちらで編集）
Load: Load eqipments from existing Outfit to the list.
Save: Create new Outfit or update existing Outfit from #{Character Name}'s current equipments.
Apply: Apply current equipments to #{Character Name} without saving.
Cancel: なし
