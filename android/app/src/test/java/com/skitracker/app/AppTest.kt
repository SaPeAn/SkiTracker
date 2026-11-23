package com.skitracker.app

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.net.HttpURLConnection
import java.net.ServerSocket
import java.net.URL
import java.util.Locale
import kotlin.concurrent.thread
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

const val HEADER = "time_utc,lat,lon,alt_m,speed_kmh,hr_bpm,sats,hdop\n"

/** Синтетический круг ~5 км за ~20 минут, с холмами, пульсом и разрывом записи. */
fun demoCsv(): String = buildString {
    append(HEADER)
    var a = 0.0
    for (i in 0 until 1200) {
        val v = 15 + 6 * sin(i / 60.0)
        a += v / 3.6 / 800.0
        if (i in 500..560) continue
        val hr = if (i in 200..230) 0 else (140 + 20 * sin(i / 90.0)).toInt()
        append(String.format(Locale.US, "%d,%.6f,%.6f,%.1f,%.2f,%d,9,1.1\n", 1763800000L + i,
            60.0 + 800 * sin(a) / 111195.0, 30.0 + 800 * cos(a) / (111195.0 * cos(60 * PI / 180)),
            150 + 25 * sin(i / 100.0), v, hr))
    }
}

/** Имитация трекера: те же пути, что в src/web.cpp. */
class FakeTracker(val files: MutableMap<String, String>, val truncate: Boolean = false) {
    private val server = ServerSocket(0)
    val base = "http://127.0.0.1:${server.localPort}"
    init {
        thread(isDaemon = true) {
            while (!server.isClosed) runCatching {
                server.accept().use { s ->
                    val r = s.getInputStream().bufferedReader()
                    val (m, path) = r.readLine().split(' ')
                    var len = 0
                    while (true) {
                        val h = r.readLine()
                        if (h.isEmpty()) break
                        if (h.startsWith("Content-Length:", true)) len = h.substringAfter(':').trim().toInt()
                    }
                    val body = CharArray(len).also { if (len > 0) r.read(it) }.concatToString()
                    val name = (path.substringAfter("f=", "") + body.substringAfter("f=", ""))
                    val (code, out) = when {
                        path == "/api/info" -> 200 to """{"device":"SkiTracker","api":1,"total":1966080,"free":1900000,"bat":3.91}"""
                        path == "/api/tracks" -> 200 to files.entries.joinToString(",", "[", "]") { """{"name":"${it.key}","size":${it.value.length}}""" }
                        path.startsWith("/csv") && name in files -> 200 to files.getValue(name).let { if (truncate) it.dropLast(50) else it }
                        m == "POST" && path == "/api/delete" && files.remove(name) != null -> 200 to """{"ok":true}"""
                        else -> 404 to "not found"
                    }
                    s.getOutputStream().write("HTTP/1.1 $code X\r\nContent-Length: ${out.length}\r\nConnection: close\r\n\r\n$out".toByteArray())
                }
            }
        }
    }
    val client get() = TrackerClient(base) { (it as URL).openConnection() as HttpURLConnection }
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class AppTest {
    private val ctx get() = org.robolectric.RuntimeEnvironment.getApplication()
    private fun parse(s: String) = Track.parse(s.reader().buffered())

    @Test fun parsesAndSkipsBrokenLines() {
        val t = parse(HEADER + "100,60.0,30.0,150.0,10.0,120,9,1.0\nмусор\n101,60.0\n" +
            "102,0.000000,0.000000,0,0,0,0,9\n103,95.0,30,1,1,1,1,1\n99,60.0,30.0,150,10,120,9,1.0\n104,60.000100,30.0,150.0,10.0,0,9,1.0\n105,60.0,")
        assertEquals(listOf(100L, 104L), t.points.map { it.time })
        assertEquals(11.1, t.stats.distM, 0.1)
        assertEquals(120, t.stats.avgHr)
    }

    @Test fun statsMatchKnownTrack() {
        // строго на север 10 м/с = 36 км/ч, 200 с -> ~1990 м; подъём 100 м
        val t = parse(HEADER + (0 until 200).joinToString("\n") {
            String.format(Locale.US, "%d,%.6f,30.0,%.1f,36.0,150,9,1.0", 1000 + it, 60 + it * 10 / 111194.93, 100 + it * 0.5) })
        assertEquals(1990.0, t.stats.distM, 4.0)
        assertEquals(36.0, t.stats.avgSpeed, 0.2)
        assertEquals(99.5, t.stats.ascent, 5.0)
        assertEquals(199, t.stats.moving)
        assertEquals(2, t.splits.size)
        assertEquals(100.0, t.splits[0].seconds.toDouble(), 1.0)
    }

    @Test fun emptyAndSinglePoint() {
        assertTrue(parse(HEADER).points.isEmpty())
        assertEquals(1, parse(HEADER + "1,60,30,1,1,1,1,1").points.size)
    }

    @Test fun gpxIsValidXml() {
        val out = StringBuilder()
        Gpx.write("a&b<c", parse(demoCsv()), out)
        val doc = javax.xml.parsers.DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(out.toString().byteInputStream())
        assertEquals(1139, doc.getElementsByTagName("trkpt").length)
        assertEquals(2, doc.getElementsByTagName("trkseg").length)
        assertEquals(1108, doc.getElementsByTagName("gpxtpx:hr").length)
        assertTrue(out.contains("<time>2025-11-22T08:26:40Z</time>"))
    }

    @Test fun clientRoundTrip() {
        val f = FakeTracker(mutableMapOf("20261122_101500.csv" to demoCsv(), "../../evil.csv" to "x"))
        val c = f.client
        assertEquals(1900000, c.info().free)
        val list = c.tracks()
        assertEquals(listOf("20261122_101500.csv"), list.map { it.name })
        val target = File(ctx.cacheDir, "t.csv")
        c.download(list[0], target)
        assertEquals(demoCsv(), target.readText())
        c.delete(list[0])
        assertTrue(c.tracks().isEmpty())
        assertTrue(runCatching { c.delete(list[0]) }.isFailure)
    }

    @Test fun truncatedDownloadIsRejected() {
        val c = FakeTracker(mutableMapOf("a.csv" to demoCsv()), truncate = true).client
        val target = File(ctx.cacheDir, "a.csv").apply { writeText("старая копия") }
        assertTrue(runCatching { c.download(c.tracks()[0], target) }.isFailure)
        assertEquals("старая копия", target.readText())
        assertFalse(File(ctx.cacheDir, "a.csv.part").exists())
    }

    // ---------- экраны ----------
    private fun settle() = repeat(20) { Thread.sleep(50); shadowOf(Looper.getMainLooper()).idle() }

    private fun shot(a: Activity, name: String) {
        settle()
        val v = a.window.decorView
        val b = Bitmap.createBitmap(v.width, v.height, Bitmap.Config.ARGB_8888)
        v.draw(Canvas(b))
        File("build/shots").mkdirs()
        File("build/shots/$name.png").outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }

    private fun charts(v: View): List<ChartView> =
        if (v is ChartView) listOf(v) else if (v is ViewGroup) (0 until v.childCount).flatMap { charts(v.getChildAt(it)) } else emptyList()

    private fun trackScreen(name: String) {
        Store(ctx).file("20261122_101500.csv").writeText(demoCsv())
        val a = Robolectric.buildActivity(TrackActivity::class.java,
            Intent().putExtra("name", "20261122_101500.csv")).setup().visible().get()
        settle()
        val cs = charts(a.window.decorView)
        assertEquals(3, cs.size)
        val t = android.os.SystemClock.uptimeMillis()
        val e = android.view.MotionEvent.obtain(t, t, android.view.MotionEvent.ACTION_DOWN, cs[0].width * 0.6f, 60f, 0)
        assertTrue(cs[0].dispatchTouchEvent(e))
        shot(a, name)
        val scroll = cs[0].parent.parent.parent as android.widget.ScrollView
        scroll.scrollTo(0, 100000)
        shot(a, "$name-bottom")
    }

    @Test @Config(qualifiers = "ru-w411dp-h891dp-xhdpi") fun track() = trackScreen("track")
    @Test @Config(qualifiers = "ru-w411dp-h891dp-night-xhdpi") fun trackNight() = trackScreen("track-night")
    @Test @Config(qualifiers = "ru-w891dp-h411dp-land-xhdpi") fun trackLand() = trackScreen("track-land")
    @Test @Config(sdk = [24], qualifiers = "ru-w360dp-h640dp-xhdpi", ) @GraphicsMode(GraphicsMode.Mode.LEGACY)
    fun opensOnAndroid7() {
        Store(ctx).file("a.csv").writeText(demoCsv())
        Robolectric.buildActivity(MainActivity::class.java).setup()
        Robolectric.buildActivity(TrackActivity::class.java, Intent().putExtra("name", "a.csv")).setup()
        Robolectric.buildActivity(TrackerActivity::class.java).setup()
        settle()
    }

    @Test @Config(qualifiers = "ru-w411dp-h891dp-xhdpi") fun lists() {
        shot(Robolectric.buildActivity(MainActivity::class.java).setup().visible().get(), "main-empty")
        Store(ctx).file("20261122_101500.csv").writeText(demoCsv())
        Store(ctx).file("trk_000.csv").writeText(HEADER)
        shot(Robolectric.buildActivity(MainActivity::class.java).setup().visible().get(), "main")
        shot(Robolectric.buildActivity(TrackerActivity::class.java).setup().visible().get(), "tracker")
    }

    @Test fun badNameDoesNotCrash() {
        val a = Robolectric.buildActivity(TrackActivity::class.java, Intent().putExtra("name", "../../x")).setup().get()
        assertTrue(a.isFinishing)
    }

    @Test fun onePointTrackDoesNotCrash() {
        Store(ctx).file("one.csv").writeText(HEADER + "1763800000,60,30,1,1,0,9,1")
        Robolectric.buildActivity(TrackActivity::class.java, Intent().putExtra("name", "one.csv")).setup().visible()
        settle()
    }
}
