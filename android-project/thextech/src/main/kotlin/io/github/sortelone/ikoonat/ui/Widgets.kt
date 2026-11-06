package io.github.sortelone.ikoonat.ui

import android.graphics.BitmapFactory
import android.util.LruCache
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Anything you can select with the d-pad or touch: it grows a little and
 * gets an accent ring while it has focus. clickable() makes it focusable,
 * and Android's d-pad (and the stick, which Android turns into d-pad
 * presses) moves focus between them; A presses it (LibraryActivity maps A
 * to the d-pad's center key).
 */
@Composable
fun Pressable(
    onClick: () -> Unit,
    shape: Shape,
    modifier: Modifier = Modifier,
    focusRequester: FocusRequester? = null,
    onFocused: () -> Unit = {},
    elevation: Dp = 0.dp,
    background: Color = Color.Transparent,
    content: @Composable BoxScope.(focused: Boolean) -> Unit,
)
{
    val accent = LocalAccent.current
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val pressed by interaction.collectIsPressedAsState()
    val scale by animateFloatAsState(
        targetValue = when
        {
            pressed -> 0.97f
            focused -> 1.04f
            else -> 1f
        },
        label = "scale",
    )

    LaunchedEffect(focused) {
        if(focused)
            onFocused()
    }

    var m = modifier.graphicsLayer {
        scaleX = scale
        scaleY = scale
    }
    if(elevation > 0.dp)
        m = m.shadow(if(focused) elevation * 2 else elevation, shape, clip = false)
    if(focusRequester != null)
        m = m.focusRequester(focusRequester)
    m = m.clip(shape).background(background, shape)
    if(focused)
        m = m.border(3.dp, accent.color, shape)
    m = m.clickable(interactionSource = interaction, indication = null, onClick = onClick)

    Box(m) { content(focused) }
}

/** A rounded label: the mode badge (SMBX2/SMBX), small stats */
@Composable
fun Pill(text: String, bg: Color, fg: Color, modifier: Modifier = Modifier, size: TextUnit = 11.sp)
{
    Box(modifier.clip(RoundedCornerShape(50)).background(bg).padding(horizontal = 9.dp, vertical = 3.dp)) {
        Text(text, color = fg, fontSize = size, fontWeight = FontWeight.SemiBold, maxLines = 1)
    }
}

/** A pill-shaped button: accent-filled when primary, card-colored otherwise */
@Composable
fun PillButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    primary: Boolean = false,
    danger: Boolean = false,
    focusRequester: FocusRequester? = null,
    size: TextUnit = 15.sp,
)
{
    val pal = LocalPalette.current
    val accent = LocalAccent.current
    val bg = when
    {
        primary -> accent.color
        danger -> Color(200, 60, 60)
        else -> pal.cardHi
    }
    val fg = when
    {
        primary -> accent.on
        danger -> Color.White
        else -> pal.text
    }
    Pressable(onClick = onClick, shape = RoundedCornerShape(50), modifier = modifier, focusRequester = focusRequester, background = bg) {
        Text(
            text,
            color = fg,
            fontSize = size,
            fontWeight = FontWeight.SemiBold,
            maxLines = 1,
            modifier = Modifier.align(Alignment.Center).padding(horizontal = 18.dp, vertical = 10.dp),
        )
    }
}

// ---- pictures ----

private object PictureCache
{
    val cache = object : LruCache<String, ImageBitmap>(48 * 1024 * 1024)
    {
        override fun sizeOf(key: String, value: ImageBitmap) = value.width * value.height * 4
    }

    fun load(file: File, maxWidth: Int): ImageBitmap?
    {
        val key = "${file.absolutePath}:${file.lastModified()}:$maxWidth"
        cache.get(key)?.let { return it }

        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.absolutePath, bounds)
        if(bounds.outWidth <= 0)
            return null
        var sample = 1
        while(bounds.outWidth / (sample * 2) >= maxWidth)
            sample *= 2
        val bmp = BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = sample })
            ?: return null
        val img = bmp.asImageBitmap()
        cache.put(key, img)
        return img
    }
}

/**
 * An episode's picture, cropped to fill; until it has one, a gradient in a
 * color picked from the title, with its first letter.
 */
@Composable
fun EpisodePicture(file: File?, title: String, version: Int, modifier: Modifier = Modifier, maxWidth: Int = 720)
{
    val image by produceState<ImageBitmap?>(null, file?.absolutePath, file?.lastModified(), version) {
        value = if(file == null) null else withContext(Dispatchers.IO) { runCatching { PictureCache.load(file, maxWidth) }.getOrNull() }
    }

    val img = image
    if(img != null)
    {
        Image(img, contentDescription = null, contentScale = ContentScale.Crop, modifier = modifier)
    }
    else
    {
        val hue = ((title.hashCode() % 360) + 360) % 360
        val c1 = Color.hsl(hue.toFloat(), 0.45f, 0.42f)
        val c2 = Color.hsl(((hue + 40) % 360).toFloat(), 0.55f, 0.22f)
        Box(modifier.background(Brush.linearGradient(listOf(c1, c2)))) {
            Text(
                title.trim().take(1).uppercase(),
                color = Color.White.copy(alpha = 0.85f),
                fontSize = 54.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.align(Alignment.Center),
            )
        }
    }
}
