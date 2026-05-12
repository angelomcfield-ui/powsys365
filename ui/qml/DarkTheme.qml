pragma Singleton
import QtQuick

/**
 * @brief DarkTheme - Apple-style Dark Mode color palette
 *
 * Complete semantic color definitions following Apple HIG.
 * Used as fallback when ThemeManager singleton is not available.
 */
QtObject {
    // ── Core backgrounds ─────────────────────────────────────────────────
    readonly property color backgroundPrimary:   "#1C1C1E"
    readonly property color backgroundSecondary: "#2C2C2E"
    readonly property color backgroundTertiary:  "#3A3A3C"
    readonly property color backgroundElevated:  "#1C1C1E"
    readonly property color backgroundGlass:     Qt.rgba(0.16, 0.16, 0.17, 0.75)

    // ── Text ─────────────────────────────────────────────────────────────
    readonly property color textPrimary:     "#FFFFFF"
    readonly property color textSecondary:   "#98989D"
    readonly property color textTertiary:    "#636366"
    readonly property color textPlaceholder: "#636366"

    // ── Apple accent colors ──────────────────────────────────────────────
    readonly property color accentBlue:   "#0A84FF"
    readonly property color accentGreen:  "#32D74B"
    readonly property color accentRed:    "#FF453A"
    readonly property color accentOrange: "#FF9F0A"
    readonly property color accentYellow: "#FFD60A"
    readonly property color accentPurple: "#BF5AF2"
    readonly property color accentTeal:   "#64D2FF"
    readonly property color accentIndigo: "#5E5CE6"
    readonly property color accentPink:   "#FF375F"

    // ── Semantic labels ──────────────────────────────────────────────────
    readonly property color label:            textPrimary
    readonly property color labelSecondary:   textSecondary
    readonly property color labelTertiary:    textTertiary

    // ── Separators ───────────────────────────────────────────────────────
    readonly property color separator:       "#38383A"
    readonly property color separatorOpaque: "#38383A"

    // ── Fills ────────────────────────────────────────────────────────────
    readonly property color fillPrimary:   Qt.rgba(0.47, 0.47, 0.50, 0.14)
    readonly property color fillSecondary: Qt.rgba(0.47, 0.47, 0.50, 0.12)
    readonly property color fillTertiary:  Qt.rgba(0.46, 0.46, 0.50, 0.10)

    // ── Buttons ──────────────────────────────────────────────────────────
    readonly property color buttonBg:        "#3A3A3C"
    readonly property color buttonHover:     "#48484A"
    readonly property color buttonPressed:   "#525254"
    readonly property color buttonDisabled:  "#2C2C2E"
    readonly property color buttonText:      textPrimary
    readonly property color buttonBorder:    "#48484A"
    readonly property color buttonPrimaryBg: accentBlue

    // ── Inputs ───────────────────────────────────────────────────────────
    readonly property color inputBg:        "#1C1C1E"
    readonly property color inputBorder:    "#38383A"
    readonly property color inputFocused:   accentBlue
    readonly property color inputText:      textPrimary
    readonly property color inputPlaceholder: textPlaceholder

    // ── Tables ───────────────────────────────────────────────────────────
    readonly property color tableBg:        "#2C2C2E"
    readonly property color tableAltRow:    "#323234"
    readonly property color tableSelected:  Qt.rgba(0.04, 0.52, 1.0, 0.24)
    readonly property color tableHeader:    "#3A3A3C"
    readonly property color tableBorder:    separator
    readonly property color tableText:      textPrimary

    // ── Panels ───────────────────────────────────────────────────────────
    readonly property color panelBg:        backgroundSecondary
    readonly property color panelBorder:    separator
    readonly property color panelHeader:    "#3A3A3C"
    readonly property color panelText:      textPrimary

    // ── Toolbar ──────────────────────────────────────────────────────────
    readonly property color toolbarBg:      Qt.rgba(0.17, 0.17, 0.18, 0.94)
    readonly property color toolbarBorder:  separator
    readonly property color toolbarText:    textPrimary

    // ── Sidebar ──────────────────────────────────────────────────────────
    readonly property color sidebarBg:      Qt.rgba(0.12, 0.12, 0.13, 0.96)
    readonly property color sidebarBorder:  separator
    readonly property color sidebarText:    textPrimary
    readonly property color sidebarItemHover:  "#3A3A3C"
    readonly property color sidebarItemSelected: Qt.rgba(0.04, 0.52, 1.0, 0.20)

    // ── Glassmorphism ────────────────────────────────────────────────────
    readonly property color glassBg:        Qt.rgba(0.16, 0.16, 0.17, 0.72)
    readonly property color glassBorder:    Qt.rgba(1.0, 1.0, 1.0, 0.08)
    readonly property color glassHighlight: Qt.rgba(1.0, 1.0, 1.0, 0.05)

    // ── Tooltips ─────────────────────────────────────────────────────────
    readonly property color tooltipBg:      "#3A3A3C"
    readonly property color tooltipText:    textPrimary
    readonly property color tooltipBorder:  "#48484A"

    // ── Overlays ─────────────────────────────────────────────────────────
    readonly property color overlay:        Qt.rgba(0, 0, 0, 0.45)
    readonly property color overlayLight:   Qt.rgba(0, 0, 0, 0.25)

    // ── Power System colors ──────────────────────────────────────────────
    readonly property color busPQ:          accentBlue
    readonly property color busPV:          accentGreen
    readonly property color busSlack:       accentRed
    readonly property color busDead:        "#8E8E93"

    readonly property color lineNormal:     accentGreen
    readonly property color lineWarning:    accentYellow
    readonly property color lineOverload:   accentRed
    readonly property color lineOpen:       "#8E8E93"

    readonly property color genOnline:      accentGreen
    readonly property color genOffline:     "#8E8E93"
    readonly property color genLimited:     accentYellow

    readonly property color loadActive:     accentOrange
    readonly property color loadShed:       accentRed

    readonly property color txNormal:       accentTeal
    readonly property color txOverloaded:   accentRed

    readonly property color violation:      accentRed
    readonly property color warning:        accentYellow
    readonly property color info:           accentBlue
    readonly property color success:        accentGreen

    // ── Status indicators ────────────────────────────────────────────────
    readonly property color statusOnline:   accentGreen
    readonly property color statusBusy:     accentOrange
    readonly property color statusError:    accentRed
    readonly property color statusIdle:     "#8E8E93"

    // ── Scrollbar ────────────────────────────────────────────────────────
    readonly property color scrollbarTrack: "#1C1C1E"
    readonly property color scrollbarThumb: "#5A5A5C"
    readonly property color scrollbarHover: "#6A6A6C"

    // ── Fonts (fallback names) ───────────────────────────────────────────
    readonly property string fontDisplay:   "SF Pro Display"
    readonly property string fontText:      "SF Pro Text"
    readonly property string fontMono:      "SF Mono"
}
