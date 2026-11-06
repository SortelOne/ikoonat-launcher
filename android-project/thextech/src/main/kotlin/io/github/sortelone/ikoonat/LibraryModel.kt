package io.github.sortelone.ikoonat

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class Filter(val label: String) { ALL("All"), SMBX2("SMBX2"), SMBX("SMBX"), RECENT("Recent") }

sealed class Screen
{
    object Grid : Screen()
    data class EpisodePage(val key: String) : Screen()
    object Settings : Screen()
    object Import : Screen()
}

/** Library-wide settings, in library.ini's [library] group (the Mac library reads theme and accent too) */
data class LibrarySettings(
    val theme: String = "Dark",
    val accent: String = "Leaf",
    /** "plain" or "picture" (the selected episode's picture, blurred) */
    val background: String = "plain",
    /** B confirms and A goes back (Nintendo's layout) */
    val swapAB: Boolean = false,
)

class LibraryModel(app: Application) : AndroidViewModel(app)
{
    var screen by mutableStateOf<Screen>(Screen.Grid)
    var episodes by mutableStateOf<List<Episode>>(emptyList())
    var loaded by mutableStateOf(false)
    var filter by mutableStateOf(Filter.ALL)
    var settings by mutableStateOf(LibrarySettings())
    var hasStorage by mutableStateOf(false)
    var hasAssetPack by mutableStateOf(false)
    var hasSmbx2 by mutableStateOf(false)

    /** The episode whose card has focus (the background picture follows it) */
    var focusedKey by mutableStateOf<String?>(null)

    /** A short message at the bottom of the screen */
    var toast by mutableStateOf<String?>(null)
    private var toastId = 0

    /** When the game was last started (to notice a crash when the library comes back) */
    var gameStartedAt = 0L
    var crashedLog by mutableStateOf<java.io.File?>(null)
    /** A log the activity should open the share sheet for */
    var shareLogRequest by mutableStateOf<java.io.File?>(null)

    // import
    var importing by mutableStateOf(false)
    var importStage by mutableStateOf("")
    var importDone by mutableStateOf(0L)
    var importTotal by mutableStateOf(0L)
    var importResult by mutableStateOf<ImportResult?>(null)

    /** Bumped when a picture changes, so cards load it again */
    var pictureVersion by mutableIntStateOf(0)

    fun showToast(text: String)
    {
        val id = ++toastId
        toast = text
        viewModelScope.launch {
            delay(3500)
            if(toastId == id)
                toast = null
        }
    }

    fun refresh()
    {
        val context = getApplication<Application>()
        hasStorage = Paths.hasStorageAccess(context)
        if(!hasStorage)
        {
            loaded = true
            return
        }

        viewModelScope.launch {
            val result = withContext(Dispatchers.IO) {
                Paths.ensureFolders()
                val ini = IniFile.load(Paths.libraryIni)
                val s = LibrarySettings(
                    theme = ini.get("library", "theme") ?: "Dark",
                    accent = ini.get("library", "accent") ?: "Leaf",
                    background = ini.get("library", "background") ?: "plain",
                    swapAB = ini.getInt("library", "swap-ab", 0) != 0,
                )
                Triple(Episodes.scan(), s, Pair(Paths.hasAssetPack(), Paths.hasSmbx2Data()))
            }
            episodes = result.first
            settings = result.second
            hasAssetPack = result.third.first
            hasSmbx2 = result.third.second
            loaded = true

            if(gameStartedAt > 0)
            {
                val since = gameStartedAt
                gameStartedAt = 0
                val crashed = withContext(Dispatchers.IO) { GameLauncher.latestLogCrashed(since) }
                if(crashed)
                    crashedLog = GameLauncher.latestLog()
            }
        }
    }

    fun episode(key: String): Episode? = episodes.firstOrNull { it.key == key }

    fun visibleEpisodes(): List<Episode> = when(filter)
    {
        Filter.ALL -> episodes
        Filter.SMBX2 -> episodes.filter { it.isSmbx2 }
        Filter.SMBX -> episodes.filter { !it.isSmbx2 }
        Filter.RECENT -> episodes.filter { it.lastPlayed > 0 }.sortedByDescending { it.lastPlayed }
    }

    fun nextFilter(step: Int)
    {
        val all = Filter.values()
        filter = all[(filter.ordinal + step + all.size) % all.size]
    }

    fun updateSettings(s: LibrarySettings)
    {
        settings = s
        viewModelScope.launch(Dispatchers.IO) {
            val ini = IniFile.load(Paths.libraryIni)
            ini.setString("library", "theme", s.theme)
            ini.setString("library", "accent", s.accent)
            ini.setString("library", "background", s.background)
            ini.setNumber("library", "swap-ab", if(s.swapAB) 1 else 0)
            ini.save()
        }
    }

    fun setEpisodeOptions(e: Episode, mode: String = e.mode, character: Int = e.character, players: Int = e.players)
    {
        // shown at once; written in the background
        episodes = episodes.map { if(it.key == e.key) it.copy(mode = mode, character = character, players = players) else it }
        viewModelScope.launch(Dispatchers.IO) { Episodes.setOptions(e, mode, character, players) }
    }

    fun eraseSlot(e: Episode, slot: Int)
    {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { Episodes.eraseSlot(e, slot) }
            showToast("Slot $slot erased")
            refresh()
        }
    }

    fun removeEpisode(e: Episode)
    {
        viewModelScope.launch {
            withContext(Dispatchers.IO) { Episodes.remove(e) }
            screen = Screen.Grid
            showToast("Removed \"${e.title}\"")
            refresh()
        }
    }

    fun startImport(uri: Uri, isFolder: Boolean)
    {
        if(importing)
            return
        importing = true
        importResult = null
        importStage = "Starting"
        importDone = 0
        importTotal = 0
        val progress = ImportProgress()

        viewModelScope.launch {
            val poll = launch {
                while(isActive)
                {
                    importStage = progress.stage
                    importDone = progress.done
                    importTotal = progress.total
                    delay(150)
                }
            }
            val r = withContext(Dispatchers.IO) { Importer.run(getApplication(), uri, isFolder, progress) }
            poll.cancel()
            importing = false
            importResult = r
            refresh()
        }
    }

    /** The player's own picture for an episode's card: scaled to 1280 px at most, kept as JPEG */
    fun setCover(e: Episode, uri: Uri)
    {
        viewModelScope.launch {
            val ok = withContext(Dispatchers.IO) {
                runCatching {
                    val resolver = getApplication<Application>().contentResolver
                    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                    resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
                    var sample = 1
                    while(bounds.outWidth / (sample * 2) >= 1280)
                        sample *= 2
                    val opts = BitmapFactory.Options().apply { inSampleSize = sample }
                    val bmp = resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
                        ?: return@runCatching false
                    val scaled = if(bmp.width > 1280)
                        Bitmap.createScaledBitmap(bmp, 1280, bmp.height * 1280 / bmp.width, true)
                    else
                        bmp
                    Paths.pictures.mkdirs()
                    e.coverFile.outputStream().use { scaled.compress(Bitmap.CompressFormat.JPEG, 90, it) }
                    true
                }.getOrDefault(false)
            }
            pictureVersion++
            showToast(if(ok) "Picture set" else "Couldn't read that picture")
        }
    }

    fun backupSaves(e: Episode, to: Uri)
    {
        viewModelScope.launch {
            val error = withContext(Dispatchers.IO) {
                runCatching { SaveBackup.export(getApplication(), e, to) }.exceptionOrNull()
            }
            showToast(if(error == null) "Saves backed up" else "Backup failed: ${error.message}")
        }
    }

    fun restoreSaves(e: Episode, from: Uri)
    {
        viewModelScope.launch {
            val message = withContext(Dispatchers.IO) { SaveBackup.restore(getApplication(), e, from) }
            showToast(message)
            refresh()
        }
    }

    fun resetCover(e: Episode)
    {
        e.coverFile.delete()
        pictureVersion++
        showToast("Back to the episode's own picture")
    }
}
