package com.skitracker.app

import java.io.BufferedReader
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone
import kotlin.math.abs
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/** Одна строка CSV трекера. hr == 0 — пульса нет. */
data class TrackPoint(
    val time: Long, val lat: Double, val lon: Double, val alt: Double,
    val speed: Double, val hr: Int, val sats: Int, val hdop: Double,
    val dist: Double = 0.0,
)

data class Split(val km: Double, val seconds: Long, val avgHr: Int) {
    val speed get() = if (seconds > 0) km / (seconds / 3600.0) else 0.0
}

data class Stats(
    val start: Long, val elapsed: Long, val moving: Long, val distM: Double,
    val maxSpeed: Double, val ascent: Double, val descent: Double, val avgHr: Int, val maxHr: Int,
) {
    /** Средняя за всё время, км/ч. */
    val avgSpeed get() = if (elapsed > 0) distM / 1000.0 / (elapsed / 3600.0) else 0.0
}

class Track(val points: List<TrackPoint>, val stats: Stats, val splits: List<Split>) {
    companion object {
        /** Разрыв записи длиннее этого не соединяем линией. */
        const val GAP_S = 30L

        // те же фильтры, что в прошивке (include/config.h), чтобы цифры совпадали
        private const val STEP_M = 3.0
        private const val MIN_SPEED = 1.0
        private const val MAX_HDOP = 4.0
        private const val ASCENT_HYST = 5.0

        fun parse(reader: BufferedReader): Track {
            val raw = ArrayList<TrackPoint>()
            reader.forEachLine { line -> parseLine(line)?.let { p ->
                // время обязано идти вперёд: битые и повторные строки пропускаем
                if (raw.isEmpty() || p.time > raw.last().time) raw.add(p)
            } }
            return build(raw)
        }

        private fun parseLine(line: String): TrackPoint? {
            val f = line.trim().split(',')
            if (f.size != 8) return null
            val t = f[0].toLongOrNull() ?: return null
            val lat = f[1].toDoubleOrNull() ?: return null
            val lon = f[2].toDoubleOrNull() ?: return null
            val nums = (3..4).map { f[it].toDoubleOrNull() ?: return null }
            val hr = f[5].toIntOrNull() ?: return null
            val sats = f[6].toIntOrNull() ?: return null
            val hdop = f[7].toDoubleOrNull() ?: return null
            if (!lat.isFinite() || !lon.isFinite() || !nums.all { it.isFinite() }) return null
            if (abs(lat) > 90 || abs(lon) > 180 || (lat == 0.0 && lon == 0.0)) return null
            return TrackPoint(t, lat, lon, nums[0], nums[1], hr, sats, hdop)
        }

        private fun build(raw: List<TrackPoint>): Track {
            if (raw.isEmpty()) return Track(emptyList(), Stats(0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0), emptyList())
            val pts = ArrayList<TrackPoint>(raw.size)
            var dist = 0.0; var maxSpeed = 0.0; var up = 0.0; var down = 0.0; var moving = 0L
            var anchor: TrackPoint? = null
            var altRef: Double? = null
            var hrSum = 0L; var hrN = 0; var hrMax = 0
            var prev: TrackPoint? = null
            for (p in raw) {
                if (p.hdop <= MAX_HDOP) {
                    maxSpeed = maxOf(maxSpeed, p.speed)
                    val a = anchor
                    if (a == null) anchor = p
                    else if (p.speed >= MIN_SPEED) {
                        val d = distance(a, p)
                        if (d >= STEP_M) { dist += d; anchor = p }
                    }
                    val ref = altRef
                    if (ref == null) altRef = p.alt
                    else if (p.alt - ref >= ASCENT_HYST) { up += p.alt - ref; altRef = p.alt }
                    else if (ref - p.alt >= ASCENT_HYST) { down += ref - p.alt; altRef = p.alt }
                }
                if (p.hr > 0) { hrSum += p.hr; hrN++; hrMax = maxOf(hrMax, p.hr) }
                prev?.let { q -> if (p.time - q.time <= GAP_S && p.speed >= MIN_SPEED) moving += p.time - q.time }
                prev = p
                pts.add(p.copy(dist = dist))
            }
            val stats = Stats(pts[0].time, pts.last().time - pts[0].time, moving, dist, maxSpeed, up, down,
                if (hrN > 0) (hrSum / hrN).toInt() else 0, hrMax)
            return Track(pts, stats, splits(pts))
        }

        private fun splits(pts: List<TrackPoint>): List<Split> {
            val out = ArrayList<Split>()
            var from = 0
            for (i in pts.indices) {
                val km = (pts[i].dist - pts[from].dist) / 1000.0
                val last = i == pts.lastIndex
                if (km < 1.0 && !(last && km >= 0.05)) continue
                val hr = pts.subList(from, i + 1).filter { it.hr > 0 }
                out.add(Split(km, pts[i].time - pts[from].time,
                    if (hr.isEmpty()) 0 else hr.sumOf { it.hr } / hr.size))
                from = i
            }
            return out
        }

        fun distance(a: TrackPoint, b: TrackPoint): Double {
            val la = Math.toRadians(a.lat); val lb = Math.toRadians(b.lat)
            val s1 = sin((lb - la) / 2); val s2 = sin(Math.toRadians(b.lon - a.lon) / 2)
            return 2 * 6371000.0 * asin(min(1.0, sqrt(s1 * s1 + cos(la) * cos(lb) * s2 * s2)))
        }
    }
}

object Gpx {
    fun write(name: String, track: Track, out: Appendable) {
        val iso = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US).apply { timeZone = TimeZone.getTimeZone("UTC") }
        out.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"SkiTracker\" ")
        out.append("xmlns=\"http://www.topografix.com/GPX/1/1\" ")
        out.append("xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\">\n")
        out.append("<trk><name>").append(name.replace("&", "&amp;").replace("<", "&lt;"))
        out.append("</name><type>nordic_ski</type><trkseg>\n")
        var prev: TrackPoint? = null
        for (p in track.points) {
            if (prev != null && p.time - prev.time > Track.GAP_S) out.append("</trkseg><trkseg>\n")
            prev = p
            out.append(String.format(Locale.US, "<trkpt lat=\"%.6f\" lon=\"%.6f\"><ele>%.1f</ele><time>%s</time>",
                p.lat, p.lon, p.alt, iso.format(Date(p.time * 1000))))
            if (p.hr > 0) out.append("<extensions><gpxtpx:TrackPointExtension><gpxtpx:hr>${p.hr}" +
                "</gpxtpx:hr></gpxtpx:TrackPointExtension></extensions>")
            out.append("</trkpt>\n")
        }
        out.append("</trkseg></trk></gpx>\n")
    }
}
