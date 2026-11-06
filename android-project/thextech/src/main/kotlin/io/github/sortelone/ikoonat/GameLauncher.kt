package io.github.sortelone.ikoonat

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import androidx.core.content.FileProvider
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import java.io.File

/**
 * Starting the game: the engine runs in its own process (thextechActivity,
 * android:process=":game") with command-line arguments, as run-level.sh
 * starts it on the Mac. A world started that way quits when the player
 * leaves the episode, and Android brings back whatever started it.
 */
object GameLauncher
{
    private const val GAME_ACTIVITY = "ru.wohlsoft.thextech.thextechActivity"
    /** thextechActivity appends these to its own arguments */
    const val EXTRA_ARGS = "ikoonat-args"

    /** For shortcuts and frontends (ES-DE, Daijisho): a world file or episode folder path */
    const val EXTRA_EPISODE = "episode"
    /** 1-3; without it the episode's Continue slot */
    const val EXTRA_SLOT = "slot"

    private fun baseArgs(): List<String> =
        listOf("-u", Paths.userDirArg, "-c", Paths.assets.absolutePath + "/")

    fun episodeArgs(e: Episode, slot: Int): Array<String>
    {
        val args = ArrayList(baseArgs())
        args.add(e.worldFile.absolutePath)
        args.addAll(listOf("--save-slot", slot.toString()))
        args.addAll(listOf("-n", e.players.toString()))
        args.addAll(listOf("-1", "c${e.character}"))
        if(e.players >= 2)
            args.addAll(listOf("-2", "c${if(e.character == 2) 1 else 2}"))
        return args.toTypedArray()
    }

    fun classicMenuArgs(): Array<String> = (baseArgs() + "--classic-menu").toTypedArray()

    /** False when the game can't start yet (no storage access or asset pack) */
    fun canStart(context: Context): Boolean = Paths.hasStorageAccess(context) && Paths.hasAssetPack()

    fun start(activity: Activity, args: Array<String>)
    {
        CoreScripts.install(activity)
        val intent = Intent().setClassName(activity, GAME_ACTIVITY).putExtra(EXTRA_ARGS, args)
        activity.startActivity(intent)
    }

    fun play(activity: Activity, e: Episode, slot: Int)
    {
        start(activity, episodeArgs(e, slot))
    }

    /** The game's process (":game") is alive: the player left it with Home, not through its menu */
    fun isGameRunning(context: Context): Boolean
    {
        val am = context.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
        val name = context.packageName + ":game"
        return am.runningAppProcesses?.any { it.processName == name } == true
    }

    /** Brings the running game back to the front */
    fun resume(activity: Activity)
    {
        activity.startActivity(Intent().setClassName(activity, GAME_ACTIVITY))
    }

    /** The episode a shortcut or frontend names: a world file, or a folder with one */
    fun findEpisode(path: String): Episode?
    {
        val f = File(path)
        val dir = (if(f.isFile) f.parentFile else f) ?: return null
        // /sdcard/... and /storage/emulated/0/... are the same folder
        val want = runCatching { dir.canonicalPath }.getOrDefault(dir.absolutePath)
        return Episodes.scan().firstOrNull { runCatching { it.dir.canonicalPath }.getOrDefault(it.dir.absolutePath) == want }
    }

    // ---- home-screen shortcuts ----

    fun canPinShortcuts(context: Context): Boolean =
        ShortcutManagerCompat.isRequestPinShortcutSupported(context)

    fun pinShortcut(context: Context, e: Episode): Boolean
    {
        val intent = Intent(Intent.ACTION_VIEW).setClassName(context, PlayActivity::class.java.name)
            .putExtra(EXTRA_EPISODE, e.worldFile.absolutePath)

        val icon = shortcutIcon(e)?.let { IconCompat.createWithAdaptiveBitmap(it) }
            ?: IconCompat.createWithResource(context, ru.wohlsoft.thextech.R.mipmap.ic_launcher)

        val info = ShortcutInfoCompat.Builder(context, "episode:" + e.key)
            .setShortLabel(e.title.take(24))
            .setLongLabel(e.title)
            .setIcon(icon)
            .setIntent(intent)
            .build()
        return ShortcutManagerCompat.requestPinShortcut(context, info, null)
    }

    /** The episode's picture cropped to a square, sized for an adaptive icon */
    private fun shortcutIcon(e: Episode): Bitmap?
    {
        val file = e.pictureFile ?: return null
        val src = BitmapFactory.decodeFile(file.absolutePath) ?: return null
        val side = minOf(src.width, src.height)
        val square = Bitmap.createBitmap(src, (src.width - side) / 2, (src.height - side) / 2, side, side)
        return Bitmap.createScaledBitmap(square, 432, 432, true)
    }

    // ---- logs ----

    fun latestLog(): File? =
        Paths.logs.listFiles()?.filter { it.isFile && it.name.endsWith(".txt") }?.maxByOrNull { it.lastModified() }

    /** True when the newest log ends in a crash the engine's crash handler caught */
    fun latestLogCrashed(since: Long): Boolean
    {
        val log = latestLog() ?: return false
        if(log.lastModified() < since)
            return false
        return runCatching { log.readText().let { it.contains("[Fatal]") || it.contains("<Unhandled exception") } }
            .getOrDefault(false)
    }

    /** False when it couldn't (the share sheet missing, a path outside Ikoonat/) */
    fun shareLog(activity: Activity, log: File): Boolean =
        runCatching {
            val uri = FileProvider.getUriForFile(activity, activity.packageName + ".files", log)
            val send = Intent(Intent.ACTION_SEND)
                .setType("text/plain")
                .putExtra(Intent.EXTRA_STREAM, uri)
                .putExtra(Intent.EXTRA_SUBJECT, "Ikoonat log " + log.name)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            activity.startActivity(Intent.createChooser(send, "Share log"))
        }.isSuccess

    fun versionName(context: Context): String =
        runCatching {
            val pm = context.packageManager
            val info = if(Build.VERSION.SDK_INT >= 33)
                pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
            else
                @Suppress("DEPRECATION") pm.getPackageInfo(context.packageName, 0)
            info.versionName ?: ""
        }.getOrDefault("")
}
