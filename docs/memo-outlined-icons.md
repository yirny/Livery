# Memo: outlined icon fills

Tried 2026-11-10 and reverted the same day. Not current behaviour. The window icons are the
centreline strokes in `sarrai-common`'s `Theme.cpp`.

Seven icons (search, filter, pen, chest, star, dice, dressed form) were redrawn from fill-only
SVGs. Each SVG is the old stroke outlined into a filled ribbon; the star's face and the dice
pips were already fills and stayed fills. The bare dress form was not part of this and stayed
its stroked centreline (`DrawDressFormIcon`). The meshes were baked offline from the nonzero
fill and drawn with ImGui's antialiased fill. The files are outside the repo:
`C:\Users\yirny\Dropbox\dev\icons\livery-icon-{search,filter,pen,chest,star,dice,dressed-form}-o.svg`.

The outlined stroke is about 4% of the icon square, so at a heading icon (18px at 1080p) the
ribbon is about one pixel, and about two at a tab icon. The fill fringe insets each side by
half a pixel and drops the ribbon's triangles, and the edge that remains looks jagged. In game
that aliasing was too visible, so the meshes were removed and the centreline strokes restored.
`kDiceInset` is 0.14 again and the chest icon's vertical centre is 0.57 again.

A retry should draw strokes, as the bare form does, rather than a filled outline of those
strokes. A filled outline stays smooth only where the ribbon is several pixels wide.
