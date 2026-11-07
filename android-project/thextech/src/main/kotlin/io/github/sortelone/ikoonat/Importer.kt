package io.github.sortelone.ikoonat

import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.io.File
import java.io.FilterInputStream
import java.io.InputStream
import java.nio.charset.Charset
import java.util.zip.ZipException
import java.util.zip.ZipFile

/** What an import is doing, for the progress screen (updated from the import's thread) */
class ImportProgress
{
    @Volatile var stage: String = "Starting"
    @Volatile var done: Long = 0
    /** 0: unknown */
    @Volatile var total: Long = 0
}

data class ImportResult(
    val ok: Boolean,
    val message: String,
    val episodes: List<String> = emptyList(),
    val smbx2Data: Boolean = false,
    val assetPack: Boolean = false,
)

/**
 * Imports a zip or a folder into Ikoonat/ and sorts out what it holds, as the
 * Mac library's importer does (src/main/library_import.cpp):
 *  - SMBX2's data folder (has scripts/base/engine/main.lua) -> Ikoonat/smbx2
 *    (only scripts, graphics, sound, music, costumes, config and
 *    PGE/configs/SMBX2-Integration); episodes in its worlds/ are imported too
 *  - TheXTech's asset pack (gameinfo.ini and graphics/ui/) -> Ikoonat/assets
 *  - episodes (folders with a .wld/.wldx) -> Ikoonat/worlds/<name>; an
 *    existing episode is updated, its save files are never replaced
 * Windows programs (.exe, .dll, ...) are left out. Everything is unpacked
 * into Ikoonat/import-tmp first, then moved into place.
 */
object Importer
{
    private val skippedExtensions = setOf("exe", "dll", "pdb", "bat", "msi", "com", "scr")
    private val smbx2Parts = listOf("scripts", "graphics", "sound", "music", "costumes", "config", "PGE/configs/SMBX2-Integration")
    private val skipDirs = setOf("__MACOSX", "_templates")

    fun run(context: Context, uri: Uri, isFolder: Boolean, progress: ImportProgress): ImportResult
    {
        val tmp = Paths.importTmp
        try
        {
            tmp.deleteRecursively()
            tmp.mkdirs()

            val name = displayName(context, uri, isFolder)
            val top = File(tmp, safeName(name.removeSuffix(".zip").removeSuffix(".ZIP")).ifEmpty { "import" })
            top.mkdirs()

            if(isFolder)
            {
                progress.stage = "Copying $name"
                copyTree(context, uri, top, progress)
            }
            else
            {
                progress.stage = "Unpacking $name"
                progress.total = fileSize(context, uri)
                unzip(context, uri, top, progress)
            }

            progress.stage = "Sorting"
            progress.total = 0
            return sort(top)
        }
        catch(e: Exception)
        {
            return ImportResult(false, "Import failed: ${e.message ?: e.javaClass.simpleName}")
        }
        finally
        {
            tmp.deleteRecursively()
        }
    }

    // ---- reading the source ----

    private fun displayName(context: Context, uri: Uri, isFolder: Boolean): String
    {
        if(isFolder)
        {
            val doc = DocumentsContract.buildDocumentUriUsingTree(uri, DocumentsContract.getTreeDocumentId(uri))
            queryName(context, doc)?.let { return it }
            return DocumentsContract.getTreeDocumentId(uri).substringAfterLast(':').substringAfterLast('/')
        }
        return queryName(context, uri) ?: (uri.lastPathSegment ?: "import").substringAfterLast('/')
    }

    private fun queryName(context: Context, uri: Uri): String? =
        runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if(c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull()

    private fun fileSize(context: Context, uri: Uri): Long =
        runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
                if(c.moveToFirst() && !c.isNull(0)) c.getLong(0) else 0L
            } ?: 0L
        }.getOrDefault(0L)

    private fun skipped(name: String): Boolean =
        name.substringAfterLast('.', "").lowercase() in skippedExtensions

    /** A file or folder name without path separators or other surprises */
    private fun safeName(name: String): String =
        name.replace('/', '_').replace('\\', '_').replace(':', '_').trim().trimStart('.')

    private class CountingStream(input: InputStream, private val progress: ImportProgress) : FilterInputStream(input)
    {
        override fun read(): Int
        {
            val b = super.read()
            if(b >= 0)
                progress.done++
            return b
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int
        {
            val n = super.read(b, off, len)
            if(n > 0)
                progress.done += n
            return n
        }
    }

    private fun unzip(context: Context, uri: Uri, dest: File, progress: ImportProgress)
    {
        // Java's streaming reader (ZipInputStream) stops without an error at a stored
        // entry whose sizes come after its data, as Windows' "Compress to ZIP" writes
        // (an empty .nomedia ended an asset pack after its first 19 entries), so the
        // zip is copied next to the import and read through its central directory
        val zipFile = File(dest.parentFile, dest.name + ".zip")
        val input = context.contentResolver.openInputStream(uri) ?: throw IllegalStateException("can't open the file")
        CountingStream(input.buffered(1 shl 16), progress).use { src ->
            zipFile.outputStream().use { out -> src.copyTo(out, 1 shl 16) }
        }

        // names in old zips aren't UTF-8; Java's zip reader throws on those, so retry as DOS names
        try
        {
            unzipWith(zipFile, dest, progress, Charsets.UTF_8)
        }
        catch(e: IllegalArgumentException)
        {
            retryAsDos(zipFile, dest, progress)
        }
        catch(e: ZipException)
        {
            retryAsDos(zipFile, dest, progress)
        }
        zipFile.delete()
    }

    private fun retryAsDos(zipFile: File, dest: File, progress: ImportProgress)
    {
        dest.deleteRecursively()
        dest.mkdirs()
        unzipWith(zipFile, dest, progress, Charset.forName("IBM437"))
    }

    private fun unzipWith(zipFile: File, dest: File, progress: ImportProgress, charset: Charset)
    {
        val destPath = dest.canonicalPath + File.separator
        val zip = if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) ZipFile(zipFile, ZipFile.OPEN_READ, charset) else ZipFile(zipFile)
        zip.use { z ->
            val entries = z.entries().toList()
            progress.done = 0
            progress.total = entries.sumOf { maxOf(it.size, 0L) }
            val buffer = ByteArray(1 shl 16)
            for(entry in entries)
            {
                val name = entry.name.replace('\\', '/')
                val target = File(dest, name)
                // never write outside the import folder ("zip slip")
                if(!target.canonicalPath.startsWith(destPath))
                    continue
                if(entry.isDirectory)
                {
                    target.mkdirs()
                    continue
                }
                if(skipped(target.name))
                    continue
                target.parentFile?.mkdirs()
                z.getInputStream(entry).use { input ->
                    target.outputStream().use { out ->
                        while(true)
                        {
                            val n = input.read(buffer)
                            if(n < 0)
                                break
                            out.write(buffer, 0, n)
                            progress.done += n
                        }
                    }
                }
            }
        }
    }

    private fun copyTree(context: Context, treeUri: Uri, dest: File, progress: ImportProgress)
    {
        val resolver = context.contentResolver
        val buffer = ByteArray(1 shl 16)

        fun walk(docId: String, into: File)
        {
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId)
            val cols = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
            )
            val entries = ArrayList<Triple<String, String, String>>()
            resolver.query(children, cols, null, null, null)?.use { c ->
                while(c.moveToNext())
                    entries.add(Triple(c.getString(0), c.getString(1) ?: "", c.getString(2) ?: ""))
            }
            into.mkdirs()
            for((id, name, mime) in entries)
            {
                val safe = safeName(name)
                if(safe.isEmpty())
                    continue
                if(mime == DocumentsContract.Document.MIME_TYPE_DIR)
                {
                    walk(id, File(into, safe))
                }
                else if(!skipped(safe))
                {
                    val doc = DocumentsContract.buildDocumentUriUsingTree(treeUri, id)
                    resolver.openInputStream(doc)?.use { input ->
                        File(into, safe).outputStream().use { out ->
                            while(true)
                            {
                                val n = input.read(buffer)
                                if(n < 0)
                                    break
                                out.write(buffer, 0, n)
                                progress.done += n
                            }
                        }
                    }
                }
            }
        }

        walk(DocumentsContract.getTreeDocumentId(treeUri), dest)
    }

    // ---- sorting into place ----

    private fun isSmbx2Data(dir: File) = File(dir, "scripts/base/engine/main.lua").isFile

    private fun isAssetPack(dir: File) = File(dir, "gameinfo.ini").isFile && File(dir, "graphics/ui").isDirectory

    private fun isEpisode(dir: File) = dir.listFiles()?.any { it.isFile && Episodes.isWorldFile(it.name) } == true

    private fun sort(top: File): ImportResult
    {
        val episodes = ArrayList<String>()
        var smbx2 = false
        var pack = false
        var backup = false

        fun walk(dir: File, depth: Int)
        {
            when
            {
                File(dir, SaveBackup.MARKER).isFile -> backup = true
                isSmbx2Data(dir) ->
                {
                    installSmbx2(dir)
                    smbx2 = true
                    val worlds = File(dir, "worlds")
                    worlds.listFiles()?.filter { it.isDirectory }?.forEach { walkEpisodes(it, 2, episodes) }
                }
                isAssetPack(dir) ->
                {
                    installReplacing(dir, Paths.assets)
                    pack = true
                }
                isEpisode(dir) -> episodes.add(installEpisode(dir))
                depth > 0 ->
                {
                    dir.listFiles()
                        ?.filter { it.isDirectory && !it.name.startsWith(".") && it.name !in skipDirs }
                        ?.sortedBy { it.name.lowercase() }
                        ?.forEach { walk(it, depth - 1) }
                }
            }
        }

        walk(top, 6)

        val parts = ArrayList<String>()
        if(pack)
            parts.add("the SMBX 1.3 asset pack")
        if(smbx2)
            parts.add("SMBX2's data")
        if(episodes.isNotEmpty())
            parts.add(if(episodes.size == 1) "\"${episodes[0]}\"" else "${episodes.size} episodes")

        return when
        {
            parts.isNotEmpty() -> ImportResult(true, "Imported " + parts.joinToString(" and "), episodes, smbx2, pack)
            backup -> ImportResult(false, "That's a save backup: open its episode and choose Restore saves.")
            else -> ImportResult(false, "Found no episodes, SMBX2 data or asset pack in it.")
        }
    }

    private fun walkEpisodes(dir: File, depth: Int, out: MutableList<String>)
    {
        if(dir.name.startsWith(".") || dir.name in skipDirs)
            return
        if(isEpisode(dir))
        {
            out.add(installEpisode(dir))
            return
        }
        if(depth > 0)
            dir.listFiles()?.filter { it.isDirectory }?.forEach { walkEpisodes(it, depth - 1, out) }
    }

    private fun installSmbx2(src: File)
    {
        val staging = File(Paths.root, "smbx2.importing")
        staging.deleteRecursively()
        for(part in smbx2Parts)
        {
            val from = File(src, part)
            if(from.isDirectory)
                move(from, File(staging, part))
        }
        swapIn(staging, Paths.smbx2)
    }

    private fun installReplacing(src: File, dest: File)
    {
        val staging = File(dest.parentFile, dest.name + ".importing")
        staging.deleteRecursively()
        move(src, staging)
        swapIn(staging, dest)
    }

    /** staging becomes dest; the old dest is deleted afterwards */
    private fun swapIn(staging: File, dest: File)
    {
        val old = File(dest.parentFile, dest.name + ".old")
        old.deleteRecursively()
        if(dest.exists() && !dest.renameTo(old))
            throw IllegalStateException("can't replace ${dest.name}")
        if(!staging.renameTo(dest))
        {
            old.renameTo(dest)
            throw IllegalStateException("can't move ${dest.name} into place")
        }
        old.deleteRecursively()
    }

    /** Returns the episode's folder name in Ikoonat/worlds */
    private fun installEpisode(src: File): String
    {
        val name = safeName(src.name).ifEmpty { "Episode" }
        val dest = File(Paths.worlds, name)
        Paths.worlds.mkdirs()
        if(!dest.exists())
        {
            move(src, dest)
            return name
        }

        // an update of an episode already here: new files win, saves stay
        src.walkTopDown().filter { it.isFile }.forEach { f ->
            val rel = f.relativeTo(src).path
            val target = File(dest, rel)
            if(target.exists() && f.name.lowercase().startsWith("save"))
                return@forEach
            target.parentFile?.mkdirs()
            if(target.exists())
                target.delete()
            if(!f.renameTo(target))
                f.copyTo(target, overwrite = true)
        }
        return name
    }

    /** A rename on the same storage; a copy when that fails */
    private fun move(src: File, dest: File)
    {
        dest.parentFile?.mkdirs()
        if(src.renameTo(dest))
            return
        src.copyRecursively(dest, overwrite = true)
        src.deleteRecursively()
    }
}
