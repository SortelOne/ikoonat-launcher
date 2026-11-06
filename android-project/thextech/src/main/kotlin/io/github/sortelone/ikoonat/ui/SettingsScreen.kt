package io.github.sortelone.ikoonat.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.sortelone.ikoonat.LibraryModel
import io.github.sortelone.ikoonat.Paths

class SettingsActions(
    val engineOptions: () -> Unit,
    val classicMenu: () -> Unit,
    val shareLog: () -> Unit,
    val version: String,
)

@Composable
fun SettingsScreen(model: LibraryModel, actions: SettingsActions)
{
    val pal = LocalPalette.current
    val s = model.settings
    val first = remember { FocusRequester() }

    Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.Center) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text("Settings", color = pal.text, fontSize = 26.sp, fontWeight = FontWeight.Bold)

            Heading("Look")
            ChoiceRow("Theme", palettes.map { it.name }, s.theme, first) { model.updateSettings(s.copy(theme = it)) }
            ChoiceRow("Accent color", accents.map { it.name }, s.accent) { model.updateSettings(s.copy(accent = it)) }
            ChoiceRow("Background", listOf("Plain", "Picture"), if(s.background == "picture") "Picture" else "Plain") {
                model.updateSettings(s.copy(background = if(it == "Picture") "picture" else "plain"))
            }

            Heading("Controller")
            ChoiceRow("Confirm button", listOf("A", "B"), if(s.swapAB) "B" else "A") {
                model.updateSettings(s.copy(swapAB = it == "B"))
            }
            Text(
                "D-pad or stick moves, A opens, B goes back, L1/R1 switch the filter, Start opens Settings. " +
                    "In the game, TheXTech's own controls apply (set them in Game options).",
                color = pal.sub, fontSize = 13.sp,
            )

            Heading("Game")
            OptionRow("Game options", "renderer, frame skip, ...") { actions.engineOptions() }
            OptionRow("Classic menu", "battle mode, the editor") { actions.classicMenu() }

            Heading("Help")
            OptionRow("Share the latest log", "for bug reports") { actions.shareLog() }
            Text(
                "Your episodes, saves and logs are in ${Paths.root.absolutePath} " +
                    "(reachable from a file manager; kept if the app is uninstalled).",
                color = pal.sub, fontSize = 13.sp,
            )
            Text(
                "Ikoonat Launcher ${actions.version}. Runs on TheXTech (GPLv3).",
                color = pal.sub, fontSize = 13.sp,
            )
            Spacer(Modifier.height(16.dp))
        }
    }

    LaunchedEffect(Unit) {
        runCatching { first.requestFocus() }
    }
}

@Composable
private fun Heading(title: String)
{
    val pal = LocalPalette.current
    Text(title.uppercase(), color = pal.sub, fontSize = 12.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 10.dp, start = 4.dp))
}

/** A label and its choices as pills; the chosen one is accent-filled */
@Composable
private fun ChoiceRow(label: String, choices: List<String>, selected: String, focus: FocusRequester? = null, onPick: (String) -> Unit)
{
    val pal = LocalPalette.current
    Row(Modifier.padding(start = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = pal.text, fontSize = 15.sp, modifier = Modifier.weight(1f))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            for((i, c) in choices.withIndex())
            {
                PillButton(
                    c,
                    { onPick(c) },
                    primary = c.equals(selected, ignoreCase = true),
                    focusRequester = if(i == 0) focus else null,
                    size = 13.sp,
                )
            }
        }
    }
}
