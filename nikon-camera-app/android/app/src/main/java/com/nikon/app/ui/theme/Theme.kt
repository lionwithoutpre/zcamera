package com.nikon.app.ui.theme

import android.app.Activity
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val NikonColorScheme = darkColorScheme(
    primary = NikonYellow,
    onPrimary = NikonBlack,
    primaryContainer = NikonYellowDim,
    secondary = NikonBlue,
    tertiary = NikonOrange,
    background = NikonBlack,
    surface = NikonSurface,
    surfaceVariant = NikonSurface2,
    onBackground = NikonText,
    onSurface = NikonText,
    onSurfaceVariant = NikonText2,
    outline = NikonBorder,
    error = NikonRed,
)

/**
 * Nikon Connect 主题 — Material3 深色 + 尼康官方黑黄配色
 */
@Composable
fun NikonTheme(content: @Composable () -> Unit) {
    val colorScheme = NikonColorScheme
    val view = LocalView.current

    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = NikonBlack.toArgb()
            window.navigationBarColor = NikonBlack.toArgb()
            WindowCompat.getInsetsController(window, view)
                .isAppearanceLightStatusBars = false
        }
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = NikonTypography,
        content = content
    )
}
