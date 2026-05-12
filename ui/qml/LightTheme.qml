pragma Singleton
import QtQuick

/**
 * @brief LightTheme - Apple-style Light Mode color palette
 *
 * Complete semantic color definitions following Apple HIG.
 * Mirror of DarkTheme with light-appropriate values.
 */
QtObject {
    // ── Core backgrounds ─────────────────────────────────────────────────
    readonly property color backgroundPrimary:   "#F5F5F7"
    readonly property color backgroundSecondary: "#FFFFFF"
    readonly property color backgroundTertiary:  "#E5E5EA"
    readonly property color backgroundElevated:  "#FFFFFF"
    readonly property color backgroundGlass:     Qt.rgba(0.96, 0.96, 0.97, 0.75)

    // ── Text ─────────────────────────────────────────────────────────────
    readonly property color textPrimary:     "#1D1D1F"
    readonly property color textSecondary:   "#86868B"
    readonly property color textTertiary:    "#A1A1A6"
    readonly property color textPlaceholder: "#C7C7CC"

    // ── Apple accent colors (same in both modes) ─────────────────────────
    readonly property color accentBlue:   "#007AFF"
    readonly property color accentGreen:  "#34C759"
    readonly property color accentRed:    "#FF3B30"
    readonly property color accentOrange: "#FF9500"
    readonly property color accentYellow: "#FFCC00"
    readonly property color accentPurple: "#AF52DE"
    readonly property color accentTeal:   "#5AC8FA"
    readonly property color accentIndigo: "#5856D6"
    readonly property color accentPink:   "#FF2D55"

    // ── Semantic labels ──────────────────────────────────────────────────
    readonly property color label:            textPrimary
    readonly property color labelSecondary:   textSecondary
    readonly property color labelTertiary:    textTertiary

    // ── Separators ───────────────────────────────────────────────────────
    readonly property color separator:       "#E5E5EA"
    readonly property color separatorOpaque: "#C6C6C8"

    // ── Fills ────────────────────────────────────────────────────────────
    readonly property color fillPrimary:   Qt.rgba(0.47, 0.47, 0.50, 0.10)
    readonly property color fillSecondary: Qt.rgba(0.47, 0.47, 0.50, 0.08)
    readonly property color fillTertiary:  Qt.rgba(0.46, 0.46, 0.50, 0.06)

    // ── Buttons ──────────────────────────────────────────────────────────
    readonly property color buttonBg:        "#E5E5EA"
    readonly property color buttonHover:     "#D1D1D6"
    readonly property color buttonPressed:   "#C7C7CC"
    readonly property color buttonDisabled:  "#F2F2F7"
    readonly property color buttonText:      textPrimary
    readonly property color buttonBorder:    "#D1D1D6"
    readonly property color buttonPrimaryBg: accentBlue

    // ── Inputs ───────────────────────────────────────────────────────────
    readonly property color inputBg:        "#FFFFFF"
    readonly property color inputBorder:    "#D1D1D6"
    readonly property color inputFocused:   accentBlue
    readonly property color inputText:      textPrimary
    readonly property color inputPlaceholder: textPlaceholder

    // ── Tables ───────────────────────────────────────────────────────────
    readonly property color tableBg:        "#FFFFFF"
    readonly property color tableAltRow:    "#F5F5F7"
    readonly property color tableSelected:  Qt.rgba(0.0, 0.48, 1.0, 0.12)
    readonly property color tableHeader:    "#E5E5EA"
    readonly property color tableBorder:    separator
    readonly property color tableText:      textPrimary

    // ── Panels ───────────────────────────────────────────────────────────
    readonly property color panelBg:        backgroundSecondary
    readonly property color panelBorder:    separator
    readonly property color panelHeader:    "#E5E5EA"
    readonly property color panelText:      textPrimary

    // ── Toolbar ──────────────────────────────────────────────────────────
    readonly property color toolbarBg:      Qt.rgba(0.96, 0.96, 0.97, 0.94)
    readonly property color toolbarBorder:  separator
    readonly property color toolbarText:    textPrimary

    // ── Sidebar ──────────────────────────────────────────────────────────
    readonly property color sidebarBg:      Qt.rgba(0.96, 0.96, 0.97, 0.96)
    readonly property color sidebarBorder:  separator
    readonly property color sidebarText:    textPrimary
    readonly property color sidebarItemHover:  "#E5E5EA"
    readonly property color sidebarItemSelected: Qt.rgba(0.0, 0.48, 1.0, 0.10)

    // ── Glassmorphism ────────────────────────────────────────────────────
    readonly property color glassBg:        Qt.rgba(0.96, 0.96, 0.97, 0.72)
    readonly property color glassBorder:    Qt.rgba(0, 0, 0, 0.06)
    readonly property color glassHighlight: Qt.rgba(1.0, 1.0, 1.0, 0.50)

    // ── Tooltips ─────────────────────────────────────────────────────────
    readonly property color tooltipBg:      "#1D1D1F"
    readonly property color tooltipText:    "#FFFFFF"
    readonly property color tooltipBorder:  "#3A3A3C"

    // ── Overlays ─────────────────────────────────────────────────────────
    readonly property color overlay:        Qt.rgba(0, 0, 0, 0.30)
    readonly property color overlayLight:   Qt.rgba(0, 0, 0, 0.15)

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
    readonly property color scrollbarTrack: "#F5F5F7"
    readonly property color scrollbarThumb: "#C7C7CC"
    readonly property color scrollbarHover: "#A1A1A6"

    // ── Fonts (fallback names) ───────────────────────────────────────────
    readonly property string fontDisplay:   "SF Pro Display"
    readonly property string fontText:      "SF Pro Text"
    readonly property string fontMono:      "SF Mono"
}
