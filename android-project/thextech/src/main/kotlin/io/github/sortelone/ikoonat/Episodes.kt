package io.github.sortelone.ikoonat

import java.io.File

/** A save slot as the game last recorded it in library.ini */
data class SlotInfo(
    val progress: Int = -1,
    val stars: Int = 0,
    val maxStars: Int = 0,
    /** play time in game frames (65 a second) */
    val time: Long = 0,
)
{
    val isEmpty: Boolean get() = progress < 0
}

data class Episode(
    /** the episode folder */
    val dir: File,
    /** the world file the game starts */
    val worldFile: File,
    /** library.ini's group name (the folder relative to Ikoonat/, as the game makes it) */
    val key: String,
    val title: String,
    /** the player's choice: auto, smbx or smbx2 */
    val mode: String,
    /** the game's last auto-detect (smbx/smbx2), or this app's guess before the first play */
    val detected: String,
    val detectedWhy: String,
    /** seconds since 1970; 0: never played */
    val lastPlayed: Long,
    val lastSlot: Int,
    /** player 1's character, 1-5: Mario, Luigi, Peach, Toad, Link */
    val character: Int,
    val players: Int,
    val slots: List<SlotInfo>,
    /** one of the asset pack's own episodes (Ikoonat/assets/worlds) */
    val fromAssetPack: Boolean,
)
{
    val isSmbx2: Boolean
        get() = when(mode)
        {
            "smbx2" -> true
            "smbx" -> false
            else -> detected == "smbx2"
        }

    val modeLabel: String get() = if(isSmbx2) "SMBX2" else "SMBX"

    /** The slot Continue starts: the last one played, else the first with a save, else 1 */
    val continueSlot: Int
        get()
        {
            if(lastSlot in 1..slots.size)
                return lastSlot
            val i = slots.indexOfFirst { !it.isEmpty }
            return if(i >= 0) i + 1 else 1
        }

    val hasAnySave: Boolean get() = slots.any { !it.isEmpty }

    val totalTime: Long get() = slots.sumOf { it.time }

    /** The player's own picture for the card */
    val coverFile: File get() = File(Paths.pictures, Episodes.pictureName(key) + ".cover.jpg")

    /** The picture the game captured ~45 s into play */
    val autoPictureFile: File get() = File(Paths.pictures, Episodes.pictureName(key) + ".png")

    val pictureFile: File?
        get() = when
        {
            coverFile.isFile -> coverFile
            autoPictureFile.isFile -> autoPictureFile
            else -> null
        }
}

object Episodes
{
    const val SLOTS = 3

    private val skipDirs = setOf("__MACOSX", "_templates")

    /** Every episode in Ikoonat/worlds and the asset pack's worlds, most recently played first */
    fun scan(): List<Episode>
    {
        val ini = IniFile.load(Paths.libraryIni)
        val found = ArrayList<Episode>()

        fun walk(dir: File, depth: Int, fromPack: Boolean)
        {
            val children = dir.listFiles() ?: return
            val world = children.filter { it.isFile && isWorldFile(it.name) }.minByOrNull { it.name.lowercase() }
            if(world != null)
            {
                found.add(load(dir, world, ini, fromPack))
                return
            }
            if(depth <= 0)
                return
            for(c in children.sortedBy { it.name.lowercase() })
            {
                if(c.isDirectory && !c.name.startsWith(".") && c.name !in skipDirs)
                    walk(c, depth - 1, fromPack)
            }
        }

        walk(Paths.worlds, 3, false)
        walk(File(Paths.assets, "worlds"), 2, true)

        return found.sortedWith(
            compareByDescending<Episode> { it.lastPlayed }.thenBy { it.title.lowercase() }
        )
    }

    fun isWorldFile(name: String): Boolean
    {
        val n = name.lowercase()
        return n.endsWith(".wld") || n.endsWith(".wldx")
    }

    private fun load(dir: File, world: File, ini: IniFile, fromPack: Boolean): Episode
    {
        val key = key(dir)
        val has = ini.has(key)
        var detected = ini.get(key, "detected") ?: ""
        var why = ini.get(key, "detected-why") ?: ""
        if(detected.isEmpty())
        {
            // the game decides when the episode starts; until then, a quick guess for the badge
            val lua = findLua(dir, 2)
            detected = if(lua != null) "smbx2" else "smbx"
            why = if(lua != null) "Lua scripts (${lua.name})" else "nothing SMBX2-only"
        }

        val slots = (1..SLOTS).map { n ->
            if(!has)
                SlotInfo()
            else
                SlotInfo(
                    progress = ini.getInt(key, "slot$n-progress", -1),
                    stars = ini.getInt(key, "slot$n-stars", 0),
                    maxStars = ini.getInt(key, "slot$n-max-stars", 0),
                    time = ini.getLong(key, "slot$n-time", 0),
                )
        }

        return Episode(
            dir = dir,
            worldFile = world,
            key = key,
            title = readTitle(world) ?: dir.name,
            mode = ini.get(key, "mode") ?: "auto",
            detected = detected,
            detectedWhy = why,
            lastPlayed = ini.getLong(key, "last-played", 0),
            lastSlot = ini.getInt(key, "last-slot", 0),
            character = ini.getInt(key, "character", 1).coerceIn(1, 5),
            players = ini.getInt(key, "players", 1).coerceIn(1, 2),
            slots = slots,
            fromAssetPack = fromPack,
        )
    }

    /** The game's Library_key: the folder relative to the user folder, without a trailing slash */
    fun key(dir: File): String
    {
        val user = Paths.root.absolutePath.trimEnd('/') + "/"
        var d = normalize(dir.absolutePath)
        if(d.length > user.length && d.startsWith(user))
            d = d.substring(user.length)
        return d.trimEnd('/').replace('[', '_').replace(']', '_')
    }

    private fun normalize(path: String): String
    {
        val parts = ArrayList<String>()
        for(p in path.split('/'))
        {
            when(p)
            {
                "", "." -> {}
                ".." -> if(parts.isNotEmpty() && parts.last() != "..") parts.removeAt(parts.size - 1) else parts.add(p)
                else -> parts.add(p)
            }
        }
        return (if(path.startsWith("/")) "/" else "") + parts.joinToString("/")
    }

    /** The game's picture file name for a key: bytes other than [A-Za-z0-9_-] become '_' */
    fun pictureName(key: String): String
    {
        val sb = StringBuilder()
        for(b in key.toByteArray(Charsets.UTF_8))
        {
            val c = (b.toInt() and 0xFF).toChar()
            val ok = c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || c == '-' || c == '_'
            sb.append(if(ok) c else '_')
        }
        return sb.toString()
    }

    private fun findLua(dir: File, depth: Int): File?
    {
        val children = dir.listFiles() ?: return null
        children.firstOrNull { it.isFile && it.name.lowercase().endsWith(".lua") }?.let { return it }
        if(depth <= 0)
            return null
        for(c in children)
        {
            if(c.isDirectory && !c.name.startsWith("."))
                findLua(c, depth - 1)?.let { return it }
        }
        return null
    }

    /** The world's title: WLDX's HEAD "TL", or the second line of an SMBX 1.3 .wld */
    fun readTitle(world: File): String?
    {
        return try
        {
            if(world.name.lowercase().endsWith(".wldx"))
                readWldxTitle(world)
            else
                readWldTitle(world)
        }
        catch(e: Exception)
        {
            null
        }
    }

    private fun readWldxTitle(world: File): String?
    {
        world.bufferedReader().use { r ->
            var inHead = false
            var lines = 0
            while(lines++ < 64)
            {
                val line = r.readLine() ?: break
                val t = line.trim()
                if(t == "HEAD")
                {
                    inHead = true
                    continue
                }
                if(t == "HEAD_END")
                    break
                if(inHead)
                {
                    val v = pgexField(t, "TL")
                    if(!v.isNullOrBlank())
                        return v
                }
            }
        }
        return null
    }

    private fun readWldTitle(world: File): String?
    {
        world.bufferedReader(Charsets.ISO_8859_1).use { r ->
            r.readLine() // file format version
            val name = r.readLine()?.trim()?.removeSurrounding("\"")
            return if(name.isNullOrBlank()) null else name
        }
    }

    /** A PGE-X field value: TL:"text"; with \" \\ \n escapes */
    private fun pgexField(line: String, name: String): String?
    {
        var i = 0
        while(i < line.length)
        {
            val colon = line.indexOf(':', i)
            if(colon < 0)
                return null
            val field = line.substring(i, colon)
            var j = colon + 1
            val sb = StringBuilder()
            if(j < line.length && line[j] == '"')
            {
                j++
                while(j < line.length && line[j] != '"')
                {
                    if(line[j] == '\\' && j + 1 < line.length)
                    {
                        val n = line[j + 1]
                        sb.append(if(n == 'n') '\n' else n)
                        j += 2
                    }
                    else
                        sb.append(line[j++])
                }
                j++ // closing quote
            }
            else
            {
                while(j < line.length && line[j] != ';')
                    sb.append(line[j++])
            }
            if(field == name)
                return sb.toString()
            // next field after ';'
            while(j < line.length && line[j] != ';')
                j++
            i = j + 1
        }
        return null
    }

    // ---- changes ----

    fun setOptions(e: Episode, mode: String = e.mode, character: Int = e.character, players: Int = e.players)
    {
        val ini = IniFile.load(Paths.libraryIni)
        ini.setString(e.key, "mode", mode)
        ini.setNumber(e.key, "character", character.toLong())
        ini.setNumber(e.key, "players", players.toLong())
        ini.save()
    }

    /** Erases a save slot as the game's DeleteSave does (and SMBX2's own save data) */
    fun eraseSlot(e: Episode, slot: Int)
    {
        val gs = File(Paths.gamesaves(), e.dir.name)
        val prefix = e.worldFile.name + "-"
        for(f in listOf("save%d.savx", "save%d.luadata", "timers%d.ini", "deaths-%d.rip", "fails-%d.rip", "demos-%d.dmo"))
            File(gs, prefix + f.format(slot)).delete()

        File(e.dir, "save$slot-ext.dat").delete()

        // an SMBX 1.3 save inside the episode folder: the game ignores it while this file exists
        if(File(e.dir, "save$slot.sav").isFile)
        {
            gs.mkdirs()
            File(gs, prefix + "save$slot.nosave").writeText(
                "If this file exists, the Vanilla \"save*.sav\" inside the episode folder is ignored."
            )
        }

        val ini = IniFile.load(Paths.libraryIni)
        ini.setNumber(e.key, "slot$slot-progress", -1)
        ini.setNumber(e.key, "slot$slot-stars", 0)
        ini.setNumber(e.key, "slot$slot-max-stars", 0)
        ini.setNumber(e.key, "slot$slot-time", 0)
        ini.save()
    }

    /** Removes an episode's folder, its saves, pictures and library entry */
    fun remove(e: Episode)
    {
        if(e.fromAssetPack)
            return
        File(Paths.gamesaves(), e.dir.name).deleteRecursively()
        e.dir.deleteRecursively()
        e.coverFile.delete()
        e.autoPictureFile.delete()
        val ini = IniFile.load(Paths.libraryIni)
        ini.removeGroup(e.key)
        ini.save()
    }

    fun formatTime(frames: Long): String
    {
        val secs = frames / 65
        val h = secs / 3600
        val m = (secs / 60) % 60
        return if(h > 0) "$h h $m min" else "$m min"
    }

    val characterNames = listOf("Mario", "Luigi", "Peach", "Toad", "Link")
}
