package io.github.sortelone.ikoonat.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.sortelone.ikoonat.Episode
import io.github.sortelone.ikoonat.Episodes
import io.github.sortelone.ikoonat.LibraryModel
import io.github.sortelone.ikoonat.Screen
import io.github.sortelone.ikoonat.SlotInfo

/** What the episode page asks the activity to do (pickers, the share sheet, the game) */
class EpisodeActions(
    val play: (Episode, Int) -> Unit,
    val pickCover: (Episode) -> Unit,
    val backup: (Episode) -> Unit,
    val restore: (Episode) -> Unit,
    val pinShortcut: ((Episode) -> Unit)?,
)

@Composable
fun EpisodeScreen(model: LibraryModel, e: Episode, actions: EpisodeActions)
{
    val pal = LocalPalette.current
    val continueFocus = remember { FocusRequester() }

    // an action that needs a second press (erase, remove) waits here
    var armed by remember(e.key) { mutableStateOf<String?>(null) }

    Row(Modifier.fillMaxSize().padding(28.dp), horizontalArrangement = Arrangement.spacedBy(28.dp)) {
        // left: the picture, fading into the name
        Column(Modifier.weight(0.42f).fillMaxHeight()) {
            Box(
                Modifier.fillMaxWidth().aspectRatio(0.95f, matchHeightConstraintsFirst = true)
                    .shadow(10.dp, RoundedCornerShape(26.dp)).clip(RoundedCornerShape(26.dp)).background(pal.card)
            ) {
                EpisodePicture(e.pictureFile, e.title, model.pictureVersion, Modifier.fillMaxSize(), maxWidth = 1280)
                Box(Modifier.fillMaxSize().background(Brush.verticalGradient(0.45f to Color.Transparent, 1f to pal.card)))
                Column(Modifier.align(Alignment.BottomStart).padding(20.dp)) {
                    ModePill(e)
                    Spacer(Modifier.height(6.dp))
                    Text(e.title, color = pal.text, fontSize = 24.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    Text(
                        if(e.fromAssetPack) "From the asset pack" else e.dir.name,
                        color = pal.sub, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    )
                    if(e.totalTime > 0)
                        Text("Played ${Episodes.formatTime(e.totalTime)} in all", color = pal.sub, fontSize = 13.sp)
                }
            }
        }

        // right: Continue, the slots, the episode's settings
        Column(
            Modifier.weight(0.58f).fillMaxHeight().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            BackToLibrary({ model.screen = Screen.Grid })

            val cont = e.continueSlot
            PillButton(
                if(e.hasAnySave) "Continue  ·  Slot $cont" else "Play  ·  Slot $cont",
                { actions.play(e, cont) },
                modifier = Modifier.fillMaxWidth(),
                primary = true,
                focusRequester = continueFocus,
                size = 17.sp,
            )

            Section("Save slots")
            for(n in 1..Episodes.SLOTS)
            {
                SlotRow(n, e.slots[n - 1], armed == "erase$n",
                    onPlay = { actions.play(e, n) },
                    onErase = {
                        if(armed == "erase$n")
                        {
                            armed = null
                            model.eraseSlot(e, n)
                        }
                        else
                            armed = "erase$n"
                    })
            }

            Section("Settings")
            OptionRow("Mode", modeText(e)) {
                val next = when(e.mode) { "auto" -> "smbx2"; "smbx2" -> "smbx"; else -> "auto" }
                model.setEpisodeOptions(e, mode = next)
            }
            OptionRow("Character", Episodes.characterNames[e.character - 1]) {
                model.setEpisodeOptions(e, character = e.character % 5 + 1)
            }
            OptionRow("Players", e.players.toString()) {
                model.setEpisodeOptions(e, players = if(e.players == 1) 2 else 1)
            }

            Section("Picture")
            OptionRow("Card picture", if(e.coverFile.isFile) "Your own" else if(e.autoPictureFile.isFile) "From play" else "None yet") {
                actions.pickCover(e)
            }
            if(e.coverFile.isFile)
                OptionRow("Use the picture from play", "") { model.resetCover(e) }

            Section("More")
            actions.pinShortcut?.let { pin -> OptionRow("Add to home screen", "") { pin(e) } }
            OptionRow("Back up saves", "zip") { actions.backup(e) }
            OptionRow("Restore saves", "from a backup") { actions.restore(e) }
            if(!e.fromAssetPack)
            {
                OptionRow(
                    if(armed == "remove") "Press again to remove it and its saves" else "Remove episode",
                    "",
                    danger = armed == "remove",
                ) {
                    if(armed == "remove")
                        model.removeEpisode(e)
                    else
                        armed = "remove"
                }
            }
            Text(e.detectedWhy.let { if(it.isEmpty()) "" else "Auto: $it" }, color = pal.sub, fontSize = 12.sp)
            Spacer(Modifier.height(12.dp))
        }
    }

    LaunchedEffect(e.key) {
        runCatching { continueFocus.requestFocus() }
    }
}

private fun modeText(e: Episode): String = when(e.mode)
{
    "smbx2" -> "SMBX2"
    "smbx" -> "SMBX 1.3"
    else -> "Auto (" + (if(e.detected == "smbx2") "SMBX2" else "SMBX 1.3") + ")"
}

@Composable
private fun Section(title: String)
{
    val pal = LocalPalette.current
    Text(title.uppercase(), color = pal.sub, fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 8.dp, start = 4.dp))
}

@Composable
private fun SlotRow(n: Int, s: SlotInfo, eraseArmed: Boolean, onPlay: () -> Unit, onErase: () -> Unit)
{
    val pal = LocalPalette.current
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Pressable(onClick = onPlay, shape = RoundedCornerShape(16.dp), modifier = Modifier.weight(1f), background = pal.card) {
            Row(Modifier.padding(horizontal = 16.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Slot $n", color = pal.text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.width(64.dp))
                Text(
                    if(s.isEmpty) "Empty: start a new game" else slotText(s),
                    color = pal.sub, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if(!s.isEmpty)
            PillButton(if(eraseArmed) "Sure?" else "Erase", onErase, danger = eraseArmed, size = 13.sp)
    }
}

private fun slotText(s: SlotInfo): String
{
    val parts = ArrayList<String>()
    parts.add("${s.progress}%")
    if(s.maxStars > 0)
        parts.add("★ ${s.stars}/${s.maxStars}")
    else if(s.stars > 0)
        parts.add("★ ${s.stars}")
    if(s.time > 0)
        parts.add(Episodes.formatTime(s.time))
    return parts.joinToString("  ·  ")
}

@Composable
fun OptionRow(label: String, value: String, danger: Boolean = false, onClick: () -> Unit)
{
    val pal = LocalPalette.current
    Pressable(
        onClick = onClick,
        shape = RoundedCornerShape(16.dp),
        modifier = Modifier.fillMaxWidth(),
        background = if(danger) Color(200, 60, 60) else pal.card,
    ) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 13.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, color = if(danger) Color.White else pal.text, fontSize = 15.sp, modifier = Modifier.weight(1f))
            if(value.isNotEmpty())
                Text(value, color = if(danger) Color.White else pal.sub, fontSize = 14.sp)
        }
    }
}
