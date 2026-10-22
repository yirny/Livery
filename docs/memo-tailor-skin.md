# Memo: the Tailor skin

The Modern skin replaced it on 2026-10-23 (branch `feat/modern-skin`). Kept so it can come back.

- Last code with it: tag `tailor-skin-last` (main @ 18e2e14), `src/Theme.cpp` (`SetTailorStyle`,
  `TailorRow`, the Forge palette constants) and `src/Settings.cpp` (`Skin=Tailor`).
- It copied Tailor 3.0 (GPL-3.0; `src/ui/imgui/Theme.cpp`, `TailorScreen.cpp`): radii (window 0,
  child and popup 10, frame 8, grab 6, scroll bar 8), 40px controls, no window border; the
  spacing and resolution curve Modern still has.
- Fonts: Poppins-Regular 14px and Montserrat-Black 18px, both × 4/3 (Tailor's
  `FontMetricScale`), read from `Data/SKSE/Plugins/Tailor/fonts`; Livery shipped none, so
  without Tailor installed it drew with ImGui's default font.
- Forge palette: floor #1A130C, surface #1C1813, raised #2A2118, amber #F59E0B, amber light
  #FBBF24, copper #B87333, text #F4EBD8 (ghost at alpha 102, border 20, fills 8 / 15 / 26).
  A selected row: amber at alpha 31; a hovered one: raised. Labels in the text colour, links
  lit amber, drag grips in the text colour at half brightness.
- The nearest palette file today (`[Palette]` keys): Window #1A130CFF, Surface #1C1813FF,
  TitleBar #2A2118FF, Text #F4EBD8FF, Label #F4EBD866, Accent #F59E0BFF, AccentLight #FBBF24FF,
  Rule #F4EBD81A, Hover #2A2118FF, Selected #F59E0B1F. Modern's radii, 36px controls, grey labels
  and Jost still differ.
