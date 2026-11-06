package io.github.sortelone.ikoonat

import java.io.File

/**
 * A small INI reader/writer for the files the game shares with the library
 * (library.ini, gameinfo.ini, timers). It keeps groups and keys in their
 * order and writes values as the game's IniProcessing does: strings in
 * double quotes, numbers bare.
 */
class IniFile private constructor(private val file: File?)
{
    private val groups = LinkedHashMap<String, LinkedHashMap<String, String>>()

    /** Group names in file order */
    fun groupNames(): List<String> = groups.keys.toList()

    fun has(group: String): Boolean = groups.containsKey(group)

    fun get(group: String, key: String): String? = groups[group]?.get(key)?.let { unquote(it) }

    fun getInt(group: String, key: String, def: Int): Int =
        get(group, key)?.trim()?.toIntOrNull() ?: def

    fun getLong(group: String, key: String, def: Long): Long =
        get(group, key)?.trim()?.toLongOrNull() ?: def

    fun setString(group: String, key: String, value: String)
    {
        groups.getOrPut(group) { LinkedHashMap() }[key] = quote(value)
    }

    fun setNumber(group: String, key: String, value: Long)
    {
        groups.getOrPut(group) { LinkedHashMap() }[key] = value.toString()
    }

    fun removeGroup(group: String)
    {
        groups.remove(group)
    }

    /** Writes through a temporary file, so a crash never leaves half a file */
    fun save(to: File? = file)
    {
        val target = to ?: return
        target.parentFile?.mkdirs()
        val sb = StringBuilder()
        for((name, keys) in groups)
        {
            if(sb.isNotEmpty())
                sb.append('\n')
            sb.append('[').append(name).append("]\n")
            for((k, v) in keys)
                sb.append(k).append(" = ").append(v).append('\n')
        }
        val tmp = File(target.parentFile, target.name + ".tmp")
        tmp.writeText(sb.toString())
        if(!tmp.renameTo(target))
        {
            target.writeText(sb.toString())
            tmp.delete()
        }
    }

    companion object
    {
        fun load(file: File): IniFile
        {
            val ini = IniFile(file)
            if(!file.isFile)
                return ini

            var current: LinkedHashMap<String, String>? = null
            for(raw in file.readLines(Charsets.UTF_8))
            {
                val line = raw.trim().removePrefix("﻿")
                if(line.isEmpty() || line.startsWith(";") || line.startsWith("#"))
                    continue

                if(line.startsWith("[") && line.endsWith("]"))
                {
                    current = ini.groups.getOrPut(line.substring(1, line.length - 1)) { LinkedHashMap() }
                    continue
                }

                val eq = line.indexOf('=')
                if(eq <= 0)
                    continue

                var g = current
                if(g == null)
                {
                    g = ini.groups.getOrPut("") { LinkedHashMap() }
                    current = g
                }
                g[line.substring(0, eq).trim()] = line.substring(eq + 1).trim()
            }
            return ini
        }

        private fun unquote(v: String): String
        {
            if(v.length < 2 || !v.startsWith("\"") || !v.endsWith("\""))
                return v
            val sb = StringBuilder()
            var i = 1
            while(i < v.length - 1)
            {
                val c = v[i]
                if(c == '\\' && i + 1 < v.length - 1)
                {
                    when(val n = v[i + 1])
                    {
                        'n' -> sb.append('\n')
                        't' -> sb.append('\t')
                        else -> sb.append(n)
                    }
                    i += 2
                }
                else
                {
                    sb.append(c)
                    i++
                }
            }
            return sb.toString()
        }

        private fun quote(v: String): String
        {
            val sb = StringBuilder("\"")
            for(c in v)
            {
                when(c)
                {
                    '"' -> sb.append("\\\"")
                    '\\' -> sb.append("\\\\")
                    '\n' -> sb.append("\\n")
                    else -> sb.append(c)
                }
            }
            return sb.append('"').toString()
        }
    }
}
