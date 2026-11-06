package io.github.sortelone.ikoonat

import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.io.File

/**
 * Core mode's scripts (smbx2compat/core, packed into the APK's assets as
 * smbx2core/ by build.gradle) copied to Ikoonat/smbx2core, where the game
 * looks for them on Android. Copied again whenever the app is updated.
 */
object CoreScripts
{
    private const val ASSET_DIR = "smbx2core"

    private fun stamp(context: Context): String
    {
        val pm = context.packageManager
        val info = if(Build.VERSION.SDK_INT >= 33)
            pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
        else
            @Suppress("DEPRECATION") pm.getPackageInfo(context.packageName, 0)
        return "${info.versionName} ${info.lastUpdateTime}"
    }

    fun install(context: Context)
    {
        runCatching {
            val dest = Paths.core
            val stampFile = File(dest, ".installed")
            val want = stamp(context)
            if(stampFile.isFile && stampFile.readText() == want)
                return

            val staging = File(Paths.root, "smbx2core.installing")
            staging.deleteRecursively()
            copyAssets(context, ASSET_DIR, staging)
            File(staging, ".installed").writeText(want)

            dest.deleteRecursively()
            if(!staging.renameTo(dest))
            {
                staging.copyRecursively(dest, overwrite = true)
                staging.deleteRecursively()
            }
        }
    }

    private fun copyAssets(context: Context, path: String, dest: File)
    {
        val am = context.assets
        val children = am.list(path) ?: emptyArray()
        if(children.isEmpty())
        {
            // a file (asset folders always have entries)
            dest.parentFile?.mkdirs()
            am.open(path).use { input -> dest.outputStream().use { input.copyTo(it) } }
            return
        }
        dest.mkdirs()
        for(c in children)
            copyAssets(context, "$path/$c", File(dest, c))
    }
}
