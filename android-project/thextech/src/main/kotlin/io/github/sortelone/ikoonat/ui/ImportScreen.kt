package io.github.sortelone.ikoonat.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.sortelone.ikoonat.LibraryModel
import io.github.sortelone.ikoonat.Screen

@Composable
fun ImportScreen(model: LibraryModel, pickZip: () -> Unit, pickFolder: () -> Unit)
{
    val pal = LocalPalette.current
    val accent = LocalAccent.current
    val first = remember { FocusRequester() }

    Row(Modifier.fillMaxSize(), horizontalArrangement = Arrangement.Center) {
        Column(
            Modifier.widthIn(max = 640.dp).fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            BackToLibrary({ model.screen = Screen.Grid })
            Text("Import", color = pal.text, fontSize = 26.sp, fontWeight = FontWeight.Bold)
            Text(
                "Pick a zip or a folder. Ikoonat sorts out what's inside: episodes (SMBX 1.3 or SMBX2), " +
                    "SMBX2's data folder, TheXTech's SMBX 1.3 asset pack, or a download with several of them. " +
                    "Windows programs in it are left out. Saves of episodes you already have are kept.",
                color = pal.sub, fontSize = 14.sp,
            )

            if(model.importing)
            {
                Spacer(Modifier.height(8.dp))
                Text(model.importStage, color = pal.text, fontSize = 15.sp)
                val total = model.importTotal
                if(total > 0)
                {
                    val p = (model.importDone.toFloat() / total).coerceIn(0f, 1f)
                    LinearProgressIndicator(progress = { p }, modifier = Modifier.fillMaxWidth(), color = accent.color, trackColor = pal.cardHi)
                    Text("${model.importDone / (1024 * 1024)} of ${total / (1024 * 1024)} MB", color = pal.sub, fontSize = 13.sp)
                }
                else
                {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth(), color = accent.color, trackColor = pal.cardHi)
                    if(model.importDone > 0)
                        Text("${model.importDone / (1024 * 1024)} MB", color = pal.sub, fontSize = 13.sp)
                }
                Text("You can leave this screen open; big downloads take a minute or two.", color = pal.sub, fontSize = 13.sp)
            }
            else
            {
                model.importResult?.let { r ->
                    Text(r.message, color = if(r.ok) accent.color else pal.text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
                }
                Spacer(Modifier.height(4.dp))
                PillButton("Import a zip", pickZip, modifier = Modifier.fillMaxWidth(), primary = true, focusRequester = first)
                PillButton("Import a folder", pickFolder, modifier = Modifier.fillMaxWidth())
                if(model.importResult?.ok == true)
                    PillButton("Back to the library", { model.screen = Screen.Grid }, modifier = Modifier.fillMaxWidth())
            }

            Spacer(Modifier.height(8.dp))
            Text(
                "Where to get them: TheXTech's asset pack from wohlsoft.ru (the .zip, or unpack the .7z to a folder first); " +
                    "SMBX2 and episodes from their own sites. Ikoonat doesn't download anything itself.",
                color = pal.sub, fontSize = 13.sp,
            )
        }
    }

    LaunchedEffect(model.importing) {
        if(!model.importing)
            runCatching { first.requestFocus() }
    }
}
