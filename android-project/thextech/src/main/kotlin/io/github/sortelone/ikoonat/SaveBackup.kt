package io.github.sortelone.ikoonat

import android.content.Context
import android.net.Uri
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * An episode's saves as one zip: the game's files in settings/gamesaves
 * (save, timers, deaths, ...) under "gamesaves/", and the files SMBX2 and
 * SMBX 1.3 keep in the episode folder (save1-ext.dat, save1.sav) under
 * "episode/", plus a marker file naming the episode.
 */
object SaveBackup
{
    const val MARKER = "ikoonat-backup.ini"

    private fun episodeSaveFiles(e: Episode): List<File> =
        e.dir.listFiles()?.filter {
            val n = it.name.lowercase()
            it.isFile && n.startsWith("save") && (n.endsWith(".sav") || n.endsWith("-ext.dat"))
        } ?: emptyList()

    private fun gameSaveFiles(e: Episode): List<File>
    {
        val prefix = e.worldFile.name + "-"
        return File(Paths.gamesaves(), e.dir.name).listFiles()?.filter { it.isFile && it.name.startsWith(prefix) } ?: emptyList()
    }

    fun hasSaves(e: Episode): Boolean = gameSaveFiles(e).isNotEmpty() || episodeSaveFiles(e).isNotEmpty()

    fun suggestedName(e: Episode): String =
        e.title.replace(Regex("[\\\\/:*?\"<>|]"), "_") + " saves.zip"

    fun export(context: Context, e: Episode, to: Uri)
    {
        val out = context.contentResolver.openOutputStream(to) ?: throw IllegalStateException("can't write the file")
        ZipOutputStream(out.buffered()).use { zip ->
            zip.putNextEntry(ZipEntry(MARKER))
            val marker = "[backup]\nepisode = \"${e.dir.name}\"\nworld = \"${e.worldFile.name}\"\ntitle = \"${e.title.replace("\"", "'")}\"\n"
            zip.write(marker.toByteArray())
            zip.closeEntry()

            for(f in gameSaveFiles(e))
            {
                zip.putNextEntry(ZipEntry("gamesaves/" + f.name))
                f.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
            for(f in episodeSaveFiles(e))
            {
                zip.putNextEntry(ZipEntry("episode/" + f.name))
                f.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
        }
    }

    /**
     * Replaces the episode's saves with a backup's. The backup's world file
     * has to match (its saves are named after it).
     */
    fun restore(context: Context, e: Episode, from: Uri): String
    {
        val tmp = File(Paths.root, "restore-tmp")
        tmp.deleteRecursively()
        tmp.mkdirs()
        try
        {
            val input = context.contentResolver.openInputStream(from) ?: return "Can't open the file."
            ZipInputStream(input.buffered()).use { zip ->
                while(true)
                {
                    val entry = zip.nextEntry ?: break
                    val name = entry.name
                    if(entry.isDirectory || name.contains("..") || name.startsWith("/"))
                        continue
                    val target = File(tmp, name)
                    target.parentFile?.mkdirs()
                    target.outputStream().use { zip.copyTo(it) }
                }
            }

            val marker = File(tmp, MARKER)
            if(!marker.isFile)
                return "That isn't a save backup from Ikoonat."
            val ini = IniFile.load(marker)
            val world = ini.get("backup", "world") ?: ""
            if(!world.equals(e.worldFile.name, ignoreCase = true))
                return "That backup is for another episode (${ini.get("backup", "title") ?: world})."

            // out with the current saves, in with the backup's
            for(f in gameSaveFiles(e))
                f.delete()
            for(f in episodeSaveFiles(e))
                f.delete()

            val gs = File(Paths.gamesaves(), e.dir.name)
            gs.mkdirs()
            File(tmp, "gamesaves").listFiles()?.forEach { f ->
                // saves are named "<world file>-save1.savx"; the backup's world name may differ in case
                val rest = f.name.substring(minOf(world.length, f.name.length))
                f.copyTo(File(gs, e.worldFile.name + rest), overwrite = true)
            }
            File(tmp, "episode").listFiles()?.forEach { f ->
                f.copyTo(File(e.dir, f.name), overwrite = true)
            }
            return "Saves restored. They show here after the next play."
        }
        catch(ex: Exception)
        {
            return "Restore failed: ${ex.message ?: ex.javaClass.simpleName}"
        }
        finally
        {
            tmp.deleteRecursively()
        }
    }
}
