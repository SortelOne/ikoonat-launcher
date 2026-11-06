package io.github.sortelone.ikoonat

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.KeyEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import io.github.sortelone.ikoonat.ui.EpisodeActions
import io.github.sortelone.ikoonat.ui.EpisodePicture
import io.github.sortelone.ikoonat.ui.EpisodeScreen
import io.github.sortelone.ikoonat.ui.GridScreen
import io.github.sortelone.ikoonat.ui.IkoonatTheme
import io.github.sortelone.ikoonat.ui.ImportScreen
import io.github.sortelone.ikoonat.ui.LocalPalette
import io.github.sortelone.ikoonat.ui.PillButton
import io.github.sortelone.ikoonat.ui.SettingsActions
import io.github.sortelone.ikoonat.ui.SettingsScreen
import io.github.sortelone.ikoonat.ui.accentNamed
import io.github.sortelone.ikoonat.ui.paletteNamed

/**
 * Ikoonat Launcher's first screen: the episode library (design: smbx2compat/
 * ROADMAP.md, Phase 7, "Native Android library"). The game itself runs in
 * thextechActivity, in its own process, started with the episode's
 * arguments; this activity stays as it was meanwhile and refreshes when the
 * player comes back.
 */
class LibraryActivity : ComponentActivity()
{
    private val model: LibraryModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?)
    {
        super.onCreate(savedInstanceState)
        hideSystemBars()
        setContent { App() }
    }

    override fun onResume()
    {
        super.onResume()
        hideSystemBars()
        model.refresh()
    }

    private fun hideSystemBars()
    {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    /**
     * Controller buttons: A presses what has focus (as the d-pad's center
     * key, which Compose's clickable() takes), B goes back (swapped with
     * the Nintendo layout setting), L1/R1 switch the filter, Start opens
     * Settings. The d-pad and stick move focus by themselves.
     */
    override fun dispatchKeyEvent(event: KeyEvent): Boolean
    {
        val swap = model.settings.swapAB
        val confirm = if(swap) KeyEvent.KEYCODE_BUTTON_B else KeyEvent.KEYCODE_BUTTON_A
        val back = if(swap) KeyEvent.KEYCODE_BUTTON_A else KeyEvent.KEYCODE_BUTTON_B

        if(event.keyCode == back)
        {
            // straight to the back dispatcher: a made-up BACK key event can be taken
            // by Compose's focus system, or skipped by Android 16's predictive back
            if(event.action == KeyEvent.ACTION_UP && !event.isCanceled)
                onBackPressedDispatcher.onBackPressed()
            return true
        }

        if(event.keyCode == confirm)
        {
            val copy = KeyEvent(
                event.downTime, event.eventTime, event.action, KeyEvent.KEYCODE_DPAD_CENTER, event.repeatCount,
                event.metaState, event.deviceId, event.scanCode, event.flags, event.source,
            )
            return super.dispatchKeyEvent(copy)
        }

        val pressed = event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0
        when(event.keyCode)
        {
            KeyEvent.KEYCODE_BUTTON_L1 ->
            {
                if(pressed && model.screen == Screen.Grid)
                    model.nextFilter(-1)
                return true
            }
            KeyEvent.KEYCODE_BUTTON_R1 ->
            {
                if(pressed && model.screen == Screen.Grid)
                    model.nextFilter(1)
                return true
            }
            KeyEvent.KEYCODE_BUTTON_START ->
            {
                if(pressed)
                    model.screen = if(model.screen == Screen.Settings) Screen.Grid else Screen.Settings
                return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    private fun play(e: Episode, slot: Int)
    {
        if(!Paths.hasAssetPack())
        {
            model.showToast("Import TheXTech's SMBX 1.3 asset pack first")
            model.screen = Screen.Import
            return
        }
        if(GameLauncher.isGameRunning(this))
        {
            // the game keeps the episode it started with until the player leaves it
            model.showToast("The game is still running: leave its episode from the pause menu first")
            GameLauncher.resume(this)
            return
        }
        model.gameStartedAt = System.currentTimeMillis()
        GameLauncher.play(this, e, slot)
    }

    private fun requestStorageAccess()
    {
        if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
        {
            val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
            runCatching { startActivity(intent) }
                .recoverCatching { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) }
                .onFailure { model.showToast("Allow \"All files access\" for Ikoonat in Android's settings") }
        }
        else
        {
            requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE), 1)
        }
    }

    @Composable
    private fun App()
    {
        val palette = paletteNamed(model.settings.theme)
        val accent = accentNamed(model.settings.accent)

        // pickers (Android's own screens; they take the controller too)
        var pictureFor by remember { mutableStateOf<Episode?>(null) }
        var backupFor by remember { mutableStateOf<Episode?>(null) }
        var restoreFor by remember { mutableStateOf<Episode?>(null) }

        val zipPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if(uri != null) model.startImport(uri, false)
        }
        val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if(uri != null) model.startImport(uri, true)
        }
        val picturePicker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
            val e = pictureFor
            if(uri != null && e != null) model.setCover(e, uri)
        }
        val backupCreator = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
            val e = backupFor
            if(uri != null && e != null)
                model.backupSaves(e, uri)
        }
        val restorePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            val e = restoreFor
            if(uri != null && e != null)
                model.restoreSaves(e, uri)
        }

        LaunchedEffect(model.shareLogRequest) {
            val log = model.shareLogRequest
            if(log != null)
            {
                model.shareLogRequest = null
                if(!GameLauncher.shareLog(this@LibraryActivity, log))
                    model.showToast("Couldn't share the log")
            }
        }

        IkoonatTheme(palette, accent) {
            val pal = LocalPalette.current
            Box(Modifier.fillMaxSize().background(pal.bg)) {
                Background(model)

                if(model.loaded && !model.hasStorage)
                {
                    StorageScreen { requestStorageAccess() }
                }
                else
                {
                    BackHandler(enabled = model.screen != Screen.Grid) {
                        model.screen = Screen.Grid
                    }

                    AnimatedContent(
                        targetState = model.screen,
                        transitionSpec = { fadeIn() togetherWith fadeOut() },
                        label = "screen",
                    ) { screen ->
                        when(screen)
                        {
                            is Screen.Grid -> GridScreen(model) { e, slot -> play(e, slot) }
                            is Screen.EpisodePage ->
                            {
                                val e = model.episode(screen.key)
                                val pin: ((Episode) -> Unit)? =
                                    if(GameLauncher.canPinShortcuts(this@LibraryActivity))
                                        ({ ep: Episode ->
                                            if(!GameLauncher.pinShortcut(this@LibraryActivity, ep))
                                                model.showToast("Your home screen doesn't take shortcuts")
                                        })
                                    else
                                        null
                                if(e == null)
                                    GridScreen(model) { ep, slot -> play(ep, slot) }
                                else
                                    EpisodeScreen(
                                        model, e,
                                        EpisodeActions(
                                            play = { ep, slot -> play(ep, slot) },
                                            pickCover = {
                                                pictureFor = it
                                                picturePicker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly))
                                            },
                                            backup = {
                                                if(!SaveBackup.hasSaves(it))
                                                    model.showToast("No saves to back up yet")
                                                else
                                                {
                                                    backupFor = it
                                                    backupCreator.launch(SaveBackup.suggestedName(it))
                                                }
                                            },
                                            restore = {
                                                restoreFor = it
                                                restorePicker.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                                            },
                                            pinShortcut = pin,
                                        ),
                                    )
                            }
                            is Screen.Settings -> SettingsScreen(
                                model,
                                SettingsActions(
                                    engineOptions = {
                                        startActivity(Intent().setClassName(this@LibraryActivity, "ru.wohlsoft.thextech.GameSettings"))
                                    },
                                    classicMenu = {
                                        if(GameLauncher.isGameRunning(this@LibraryActivity))
                                        {
                                            model.showToast("The game is still running: leave its episode from the pause menu first")
                                            GameLauncher.resume(this@LibraryActivity)
                                        }
                                        else if(Paths.hasAssetPack())
                                        {
                                            model.gameStartedAt = System.currentTimeMillis()
                                            GameLauncher.start(this@LibraryActivity, GameLauncher.classicMenuArgs())
                                        }
                                        else
                                            model.showToast("Import TheXTech's SMBX 1.3 asset pack first")
                                    },
                                    shareLog = {
                                        val log = GameLauncher.latestLog()
                                        if(log == null)
                                            model.showToast("No logs yet: they appear after the game has run")
                                        else if(!GameLauncher.shareLog(this@LibraryActivity, log))
                                            model.showToast("Couldn't share the log")
                                    },
                                    version = GameLauncher.versionName(this@LibraryActivity),
                                ),
                            )
                            is Screen.Import -> ImportScreen(
                                model,
                                pickZip = { zipPicker.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream")) },
                                pickFolder = { folderPicker.launch(null) },
                            )
                        }
                    }
                }

                ToastBar(model.toast)
            }
        }
    }
}

/** The selected episode's picture behind everything, blurred and dimmed (Settings > Background) */
@Composable
private fun Background(model: LibraryModel)
{
    if(model.settings.background != "picture")
        return
    val pal = LocalPalette.current
    val key = when(val s = model.screen)
    {
        is Screen.EpisodePage -> s.key
        else -> model.focusedKey
    }
    val e = key?.let { model.episode(it) } ?: return
    val file = e.pictureFile ?: return

    Box(Modifier.fillMaxSize()) {
        // blur() works on Android 12 and later; before that the picture is only dimmed
        EpisodePicture(file, e.title, model.pictureVersion, Modifier.fillMaxSize().blur(28.dp), maxWidth = 640)
        Box(Modifier.fillMaxSize().background(pal.bg.copy(alpha = if(Build.VERSION.SDK_INT >= 31) 0.72f else 0.86f)))
    }
}

@Composable
private fun ToastBar(text: String?)
{
    if(text == null)
        return
    val pal = LocalPalette.current
    Box(Modifier.fillMaxSize().padding(bottom = 24.dp), contentAlignment = Alignment.BottomCenter) {
        Text(
            text,
            color = pal.text,
            fontSize = 14.sp,
            modifier = Modifier.clip(RoundedCornerShape(50)).background(pal.cardHi).padding(horizontal = 20.dp, vertical = 10.dp),
        )
    }
}

@Composable
private fun StorageScreen(onGrant: () -> Unit)
{
    val pal = LocalPalette.current
    val focus = remember { FocusRequester() }
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(Modifier.widthIn(max = 560.dp).padding(28.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("Welcome to Ikoonat", color = pal.text, fontSize = 26.sp, fontWeight = FontWeight.Bold)
            Text(
                "Ikoonat keeps your episodes, saves and logs in a folder named Ikoonat on this device's storage, " +
                    "so you can reach them from a file manager and they stay when the app is reinstalled. " +
                    "Android asks you to allow access to all files for that.",
                color = pal.sub, fontSize = 15.sp,
            )
            PillButton("Allow access", onGrant, primary = true, focusRequester = focus)
        }
    }
    LaunchedEffect(Unit) { runCatching { focus.requestFocus() } }
}
