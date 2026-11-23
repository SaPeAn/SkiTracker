package com.skitracker.app

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Треки, сохранённые на телефоне. */
class Store(context: Context) {
    private val dir = File(context.filesDir, "tracks").apply { mkdirs() }

    fun file(name: String): File {
        require(TrackerClient.validName(name))
        return File(dir, name)
    }

    fun has(t: RemoteTrack) = file(t.name).let { it.exists() && it.length() == t.size }

    fun names(): List<String> = (dir.list() ?: emptyArray()).filter { TrackerClient.validName(it) }.sortedDescending()

    fun load(name: String): Track = file(name).bufferedReader().use { Track.parse(it) }
}

object Fmt {
    fun duration(s: Long) = String.format(Locale.US, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
    fun km(m: Double) = String.format(Locale.US, "%.2f", m / 1000)
    fun one(v: Double) = String.format(Locale.US, "%.1f", v)
    fun date(t: Long): String = SimpleDateFormat("d MMM yyyy, HH:mm", Locale("ru")).format(Date(t * 1000))
    fun size(b: Long) = if (b < 1024 * 1024) "${(b + 1023) / 1024} КБ" else one(b / 1048576.0) + " МБ"
}
