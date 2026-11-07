package io.github.sortelone.ikoonat

import android.content.Context
import android.os.Build
import android.os.Environment
import java.io.File

/**
 * The Ikoonat folder on shared storage (visible in file managers, kept when
 * the app is reinstalled). It is the game's user folder (`-u`), so the
 * engine and this app see the same files:
 *
 *     Ikoonat/
 *       assets/        TheXTech's SMBX 1.3 asset pack (imported once)
 *       smbx2/         SMBX2's data folder (scripts, graphics, sound, ...)
 *       smbx2core/     core mode's scripts, copied out of the APK
 *       worlds/        episodes
 *       settings/      the game's settings, library.ini, gamesaves/, library/ (pictures)
 *       logs/          the game's logs
 */
object Paths
{
    val root: File get() = File(Environment.getExternalStorageDirectory(), "Ikoonat")
    val assets: File get() = File(root, "assets")
    val smbx2: File get() = File(root, "smbx2")
    val core: File get() = File(root, "smbx2core")
    val worlds: File get() = File(root, "worlds")
    val settings: File get() = File(root, "settings")
    val libraryIni: File get() = File(settings, "library.ini")
    val pictures: File get() = File(settings, "library")
    val logs: File get() = File(root, "logs")
    val importTmp: File get() = File(root, "import-tmp")

    /** The game's user folder argument (with the slash the engine adds itself) */
    val userDirArg: String get() = root.absolutePath + "/"

    /** What the game needs to find the pack (its UI graphics), not just any graphics folder */
    fun hasAssetPack(): Boolean =
        File(assets, "gameinfo.ini").isFile && File(assets, "graphics/ui").isDirectory

    fun hasSmbx2Data(): Boolean =
        File(smbx2, "scripts/base/engine/main.lua").isFile

    /** The asset pack's id ([game] id in gameinfo.ini): the game keeps saves under gamesaves/<id>/ */
    fun assetPackId(): String
    {
        val ini = IniFile.load(File(assets, "gameinfo.ini"))
        return ini.get("game", "id")?.takeIf { it.isNotBlank() } ?: "smbx"
    }

    fun gamesaves(): File = File(settings, "gamesaves/" + assetPackId())

    /** All-files access: the game and the library read and write Ikoonat/ directly */
    fun hasStorageAccess(context: Context): Boolean
    {
        return if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            Environment.isExternalStorageManager()
        else
            context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
    }

    fun ensureFolders()
    {
        for(d in listOf(root, worlds, settings, pictures))
            d.mkdirs()
        // keeps episode pictures and game graphics out of the phone's gallery
        val noMedia = File(root, ".nomedia")
        if(!noMedia.exists())
            runCatching { noMedia.createNewFile() }
    }
}
