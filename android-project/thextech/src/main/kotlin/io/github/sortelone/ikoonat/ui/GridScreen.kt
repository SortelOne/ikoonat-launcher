package io.github.sortelone.ikoonat.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.sortelone.ikoonat.Episode
import io.github.sortelone.ikoonat.Episodes
import io.github.sortelone.ikoonat.Filter
import io.github.sortelone.ikoonat.LibraryModel
import io.github.sortelone.ikoonat.Screen

private val cardShape = RoundedCornerShape(22.dp)

@Composable
fun GridScreen(model: LibraryModel, onPlay: (Episode, Int) -> Unit)
{
    val pal = LocalPalette.current
    val firstFocus = remember { FocusRequester() }
    val list = model.visibleEpisodes()

    Column(Modifier.fillMaxSize()) {
        TopBar(model)

        BoxWithConstraints(Modifier.fillMaxSize()) {
            val gap = 18.dp
            val side = 28.dp
            val cols = maxOf(2, ((maxWidth - side * 2 + gap) / (200.dp + gap)).toInt())
            val cardWidth = (maxWidth - side * 2 - gap * (cols - 1)) / cols

            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = side, vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(gap),
            ) {
                SetupNotes(model)

                // the episodes, then the "Add episode" card; focus starts on the
                // card last selected (coming back from its page), else the first
                val items: List<Episode?> = list + listOf(null)
                val focusKey = model.focusedKey?.takeIf { k -> list.any { it.key == k } }
                for(row in items.chunked(cols))
                {
                    Row(horizontalArrangement = Arrangement.spacedBy(gap)) {
                        for(e in row)
                        {
                            val takesFocus = if(focusKey != null) e?.key == focusKey else e === items.first()
                            val fr = if(takesFocus) firstFocus else null
                            if(e != null)
                                EpisodeCard(e, model, Modifier.width(cardWidth), fr, onPlay)
                            else
                                AddCard(Modifier.width(cardWidth), fr) { model.screen = Screen.Import }
                        }
                    }
                }

                if(list.isEmpty() && model.loaded)
                {
                    Text(
                        if(model.filter == Filter.ALL) "No episodes yet. Import a zip or folder with episodes in it."
                        else "No episodes here.",
                        color = pal.sub,
                        fontSize = 14.sp,
                    )
                }
                Spacer(Modifier.height(24.dp))
            }
        }
    }

    // focus the first card, so the d-pad works straight away
    LaunchedEffect(model.loaded, model.filter) {
        if(model.loaded)
            runCatching { firstFocus.requestFocus() }
    }
}

@Composable
private fun TopBar(model: LibraryModel)
{
    val pal = LocalPalette.current
    val accent = LocalAccent.current

    Row(
        Modifier.fillMaxWidth().padding(start = 28.dp, end = 28.dp, top = 18.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Image(
            ImageBitmap.imageResource(ru.wohlsoft.thextech.R.drawable.ikoonat_leaf),
            contentDescription = null,
            modifier = Modifier.size(30.dp),
            filterQuality = FilterQuality.None,
        )
        Spacer(Modifier.width(10.dp))
        Text("Ikoonat", color = pal.text, fontSize = 22.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.width(22.dp))

        for(f in Filter.values())
        {
            val selected = model.filter == f
            Pressable(
                onClick = { model.filter = f },
                shape = RoundedCornerShape(50),
                background = if(selected) accent.color else Color.Transparent,
                modifier = Modifier.padding(end = 6.dp),
            ) {
                Text(
                    f.label,
                    color = if(selected) accent.on else pal.sub,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 7.dp),
                )
            }
        }

        Spacer(Modifier.weight(1f))
        PillButton("Import", { model.screen = Screen.Import }, size = 14.sp)
        Spacer(Modifier.width(8.dp))
        PillButton("Settings", { model.screen = Screen.Settings }, size = 14.sp)
    }
}

/** What's missing before episodes can play */
@Composable
private fun SetupNotes(model: LibraryModel)
{
    if(!model.loaded)
        return

    if(!model.hasAssetPack)
    {
        Note(
            "Add TheXTech's SMBX 1.3 asset pack",
            "Every episode needs it. Import its zip (or the folder you unpacked it to) once.",
            "Import",
        ) { model.screen = Screen.Import }
    }
    else if(!model.hasSmbx2 && model.episodes.any { it.isSmbx2 })
    {
        Note(
            "SMBX2 episodes need SMBX2's data",
            "Import SMBX2's \"data\" folder or a download that has it; until then they play as SMBX 1.3 episodes.",
            "Import",
        ) { model.screen = Screen.Import }
    }

    val crashed = model.crashedLog
    if(crashed != null)
    {
        Note(
            "The game stopped with an error",
            "Its log says what happened. Share it to send it to your computer.",
            "Share log",
        ) {
            model.crashedLog = null
            model.shareLogRequest = crashed
        }
    }
}

@Composable
private fun Note(title: String, text: String, button: String, onClick: () -> Unit)
{
    val pal = LocalPalette.current
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(18.dp)).background(pal.card).padding(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, color = pal.text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
            Text(text, color = pal.sub, fontSize = 13.sp)
        }
        Spacer(Modifier.width(12.dp))
        PillButton(button, onClick, primary = true, size = 14.sp)
    }
}

/**
 * The card Jake picked (2026-11-06): the picture fills the card and fades
 * into the text below it; name and mode pill, a stats line, a Play pill.
 */
@Composable
private fun EpisodeCard(e: Episode, model: LibraryModel, modifier: Modifier, focusRequester: FocusRequester?, onPlay: (Episode, Int) -> Unit)
{
    val pal = LocalPalette.current
    val accent = LocalAccent.current

    Pressable(
        onClick = { model.screen = Screen.EpisodePage(e.key) },
        shape = cardShape,
        modifier = modifier.aspectRatio(0.8f),
        focusRequester = focusRequester,
        onFocused = { model.focusedKey = e.key },
        elevation = 8.dp,
        background = pal.card,
    ) { focused ->
        EpisodePicture(e.pictureFile, e.title, model.pictureVersion, Modifier.fillMaxWidth().fillMaxHeight(0.68f))
        Box(
            Modifier.fillMaxWidth().fillMaxHeight(0.68f).background(
                Brush.verticalGradient(0.35f to Color.Transparent, 1f to pal.card)
            )
        )

        Column(Modifier.align(Alignment.BottomStart).fillMaxWidth().padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    e.title,
                    color = pal.text,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
                Spacer(Modifier.width(6.dp))
                ModePill(e)
            }
            Text(statsLine(e), color = pal.sub, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(lastPlayedText(e), color = pal.sub, fontSize = 11.sp, maxLines = 1, modifier = Modifier.weight(1f))
                Pill(
                    if(e.hasAnySave) "Continue" else "Play",
                    if(focused) accent.color else pal.cardHi,
                    if(focused) accent.on else pal.text,
                    size = 12.sp,
                )
            }
        }
    }
}

@Composable
fun ModePill(e: Episode)
{
    val pal = LocalPalette.current
    val accent = LocalAccent.current
    if(e.isSmbx2)
        Pill("SMBX2", accent.color.copy(alpha = 0.22f), if(pal.dark) accent.color else pal.text)
    else
        Pill("SMBX", pal.cardHi, pal.sub)
}

fun statsLine(e: Episode): String
{
    val slot = e.slots.getOrNull(e.continueSlot - 1)
    if(slot == null || slot.isEmpty)
        return "New"
    val parts = ArrayList<String>()
    if(slot.maxStars > 0)
        parts.add("★ ${slot.stars}/${slot.maxStars}")
    else if(slot.stars > 0)
        parts.add("★ ${slot.stars}")
    parts.add("${slot.progress}%")
    if(slot.time > 0)
        parts.add(Episodes.formatTime(slot.time))
    return parts.joinToString("  ·  ")
}

fun lastPlayedText(e: Episode): String
{
    if(e.lastPlayed <= 0)
        return ""
    val days = (System.currentTimeMillis() / 1000 - e.lastPlayed) / 86400
    return when
    {
        days < 1 -> "Played today"
        days < 2 -> "Played yesterday"
        days < 30 -> "Played $days days ago"
        else -> "Played a while ago"
    }
}

@Composable
private fun AddCard(modifier: Modifier, focusRequester: FocusRequester?, onClick: () -> Unit)
{
    val pal = LocalPalette.current
    Pressable(
        onClick = onClick,
        shape = cardShape,
        modifier = modifier.aspectRatio(0.8f),
        focusRequester = focusRequester,
        background = pal.card.copy(alpha = 0.55f),
    ) {
        Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
            Text("+", color = pal.sub, fontSize = 44.sp)
            Text("Add episode", color = pal.sub, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
        }
    }
}
