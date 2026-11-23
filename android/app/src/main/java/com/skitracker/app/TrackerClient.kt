package com.skitracker.app

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

data class RemoteTrack(val name: String, val size: Long)
data class TrackerInfo(val free: Long, val total: Long, val battery: Double)

/** Обмен с трекером. [open] открывает соединение через нужную сеть (Wi-Fi трекера). */
class TrackerClient(private val base: String = "http://192.168.4.1", private val open: (URL) -> HttpURLConnection) {

    fun info(timeoutMs: Int = 4000): TrackerInfo {
        val j = JSONObject(text("/api/info", timeoutMs))
        if (j.optString("device") != "SkiTracker") throw IOException("Это не SkiTracker")
        return TrackerInfo(j.optLong("free"), j.optLong("total"), j.optDouble("bat", 0.0))
    }

    fun tracks(): List<RemoteTrack> {
        val a = JSONArray(text("/api/tracks", 8000))
        return (0 until a.length()).map { a.getJSONObject(it) }
            .map { RemoteTrack(it.getString("name"), it.getLong("size")) }
            .filter { validName(it.name) }
            .sortedByDescending { it.name }
    }

    /** Качает во временный файл; на место кладёт только полностью полученный. */
    fun download(track: RemoteTrack, target: File, progress: (Long) -> Unit = {}) {
        require(validName(track.name))
        val tmp = File(target.parentFile, target.name + ".part")
        try {
            request("/csv?f=${track.name}", "GET", 15000) { c ->
                c.inputStream.use { input -> tmp.outputStream().use { out ->
                    val buf = ByteArray(8192)
                    var total = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        total += n
                        progress(total)
                    }
                } }
            }
            if (tmp.length() != track.size) throw IOException("Файл получен не полностью: ${tmp.length()} из ${track.size} байт")
            if (target.exists() && !target.delete()) throw IOException("Не удалось заменить файл")
            if (!tmp.renameTo(target)) throw IOException("Не удалось сохранить файл")
        } finally {
            tmp.delete()
        }
    }

    fun delete(track: RemoteTrack) {
        require(validName(track.name))
        request("/api/delete", "POST", 8000, "f=${track.name}") { it.inputStream.use { s -> s.readBytes() } }
    }

    private fun text(path: String, timeoutMs: Int): String {
        var r = ""
        request(path, "GET", timeoutMs) { r = it.inputStream.use { s -> s.readBytes().toString(Charsets.UTF_8) } }
        return r
    }

    private fun request(path: String, method: String, timeoutMs: Int, body: String? = null, use: (HttpURLConnection) -> Unit) {
        val c = open(URL(base + path))
        try {
            c.requestMethod = method
            c.connectTimeout = timeoutMs
            c.readTimeout = timeoutMs
            c.useCaches = false
            c.instanceFollowRedirects = false
            c.setRequestProperty("Connection", "close")
            if (body != null) {
                c.doOutput = true
                c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
                c.outputStream.use { it.write(body.toByteArray()) }
            }
            if (c.responseCode != 200) throw IOException("Трекер ответил ${c.responseCode}")
            use(c)
        } finally {
            c.disconnect()
        }
    }

    companion object {
        private val NAME = Regex("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,40}\\.csv")
        /** Имя приходит из сети — не даём ему выйти из каталога треков. */
        fun validName(n: String) = NAME.matches(n) && !n.contains("..")
    }
}
