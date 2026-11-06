package io.github.sortelone.ikoonat.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

/** The library's colors; the same themes and accents as the Mac library (screen_library.cpp) */
data class Palette(
    val name: String,
    val bg: Color,
    val card: Color,
    val cardHi: Color,
    val text: Color,
    val sub: Color,
    val line: Color,
    val dark: Boolean,
)

data class Accent(val name: String, val color: Color, val on: Color)

val palettes = listOf(
    Palette("Dark", Color(24, 25, 30), Color(40, 42, 50), Color(54, 57, 68), Color(240, 240, 245), Color(160, 164, 175), Color(70, 72, 82), true),
    Palette("Light", Color(236, 236, 240), Color(255, 255, 255), Color(246, 246, 250), Color(25, 25, 30), Color(100, 100, 112), Color(205, 205, 214), false),
    Palette("Midnight", Color(12, 16, 32), Color(24, 30, 54), Color(34, 42, 74), Color(235, 238, 255), Color(150, 160, 200), Color(50, 60, 96), true),
)

val accents = listOf(
    Accent("Leaf", Color(98, 190, 80), Color(16, 30, 12)),
    Accent("Sky", Color(70, 150, 235), Color.White),
    Accent("Berry", Color(220, 80, 140), Color.White),
    Accent("Sun", Color(240, 180, 50), Color(40, 28, 4)),
)

fun paletteNamed(name: String) = palettes.firstOrNull { it.name.equals(name, true) } ?: palettes[0]
fun accentNamed(name: String) = accents.firstOrNull { it.name.equals(name, true) } ?: accents[0]

val LocalPalette = staticCompositionLocalOf { palettes[0] }
val LocalAccent = staticCompositionLocalOf { accents[0] }

@Composable
fun IkoonatTheme(palette: Palette, accent: Accent, content: @Composable () -> Unit)
{
    val scheme = if(palette.dark)
        darkColorScheme(primary = accent.color, onPrimary = accent.on, background = palette.bg, surface = palette.card, onSurface = palette.text)
    else
        lightColorScheme(primary = accent.color, onPrimary = accent.on, background = palette.bg, surface = palette.card, onSurface = palette.text)

    CompositionLocalProvider(LocalPalette provides palette, LocalAccent provides accent) {
        MaterialTheme(colorScheme = scheme, content = content)
    }
}
