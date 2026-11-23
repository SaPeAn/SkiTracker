package com.skitracker.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Gravity
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import androidx.core.content.FileProvider
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.CustomZoomButtonsController
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import java.io.File
import java.util.concurrent.Executors
import kotlin.math.ceil

/** Просмотр трека: карта, итоги, графики, километры. */
class TrackActivity : Activity() {
    private val exec = Executors.newSingleThreadExecutor()
    private lateinit var store: Store
    private lateinit var name: String
    private lateinit var map: MapView
    private lateinit var body: LinearLayout
    private var track: Track? = null
    private val charts = ArrayList<ChartView>()
    private var marker: Marker? = null

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        actionBar?.setDisplayHomeAsUpEnabled(true)
        store = Store(this)
        name = intent.getStringExtra("name") ?: ""
        if (!TrackerClient.validName(name)) { finish(); return }
        Configuration.getInstance().apply {
            userAgentValue = packageName
            osmdroidBasePath = File(cacheDir, "osmdroid")
            osmdroidTileCache = File(cacheDir, "osmdroid/tiles")
        }
        val land = resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE
        val root = LinearLayout(this).apply { orientation = if (land) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL }
        map = MapView(this).apply {
            setTileSource(TileSourceFactory.MAPNIK)
            setMultiTouchControls(true)
            zoomController.setVisibility(CustomZoomButtonsController.Visibility.SHOW_AND_FADEOUT)
        }
        val mapBox = FrameLayout(this).apply {
            addView(map)
            addView(label("© OpenStreetMap contributors", 10f).apply {
                setTextColor(Color.parseColor("#333333"))
                setBackgroundColor(Color.parseColor("#ccffffff"))
                setPadding(dp(4), dp(1), dp(4), dp(1))
            }, FrameLayout.LayoutParams(WRAP, WRAP, Gravity.BOTTOM or Gravity.END))
        }
        body = column(12)
        val scroll = ScrollView(this).apply { addView(body) }
        if (land) { root.addView(mapBox, lp(0, MATCH, 1f)); root.addView(scroll, lp(0, MATCH, 1f)) }
        else { root.addView(mapBox, lp(MATCH, 0, 4f)); root.addView(scroll, lp(MATCH, 0, 5f)) }
        setContentView(root)
        exec.execute {
            val t = runCatching { store.load(name) }.getOrNull()
            ui { if (t == null || t.points.isEmpty()) body.addView(label("В этом треке нет координат.", 15f, R.color.ink2)) else show(t) }
        }
    }

    private fun show(t: Track) {
        track = t
        title = if (t.stats.start > 0) Fmt.date(t.stats.start) else name
        drawMap(t)
        val s = t.stats
        tiles(listOf(
            "Дистанция, км" to Fmt.km(s.distM), "Время" to Fmt.duration(s.elapsed), "В движении" to Fmt.duration(s.moving),
            "Средняя, км/ч" to Fmt.one(s.avgSpeed), "Макс., км/ч" to Fmt.one(s.maxSpeed), "Набор / спуск, м" to "${s.ascent.toInt()} / ${s.descent.toInt()}",
            "Пульс средний" to (if (s.avgHr > 0) "${s.avgHr}" else "—"), "Пульс макс." to (if (s.maxHr > 0) "${s.maxHr}" else "—"), "Точек" to "${t.points.size}",
        ))
        chart("Скорость", "км/ч", R.color.speed, true) { it.speed }
        if (s.maxHr > 0) chart("Пульс", "уд/мин", R.color.hr, false) { if (it.hr > 0) it.hr.toDouble() else null }
        chart("Высота", "м", R.color.alt, false) { it.alt }
        splits(t.splits)
    }

    private fun tiles(items: List<Pair<String, String>>) {
        val c = card()
        for (r in items.chunked(3)) {
            val row = row()
            for ((k, v) in r) row.addView(column().apply {
                addView(label(v, 18f, bold = true))
                addView(label(k, 12f, R.color.ink2))
                setPadding(0, dp(4), 0, dp(4))
            }, lp(0, WRAP, 1f))
            c.addView(row, lp(MATCH, WRAP))
        }
        body.addView(c, lp(MATCH, WRAP, bottom = dp(8)))
    }

    private fun chart(title: String, unit: String, color: Int, zero: Boolean, value: (TrackPoint) -> Double?) {
        val v = ChartView(this, title, unit, getColor(color), zero, track!!.points, ::cursor, value)
        charts.add(v)
        body.addView(card().apply { addView(v, lp(MATCH, dp(150))) }, lp(MATCH, WRAP, bottom = dp(8)))
    }

    private fun splits(list: List<Split>) {
        if (list.isEmpty()) return
        val c = card()
        fun line(cells: List<String>, color: Int) {
            val r = row()
            for (x in cells) r.addView(label(x, 14f, color).apply { gravity = Gravity.END; setPadding(0, dp(4), 0, dp(4)) }, lp(0, WRAP, 1f))
            c.addView(r, lp(MATCH, WRAP))
        }
        c.addView(label("По километрам", 14f))
        line(listOf("км", "время", "км/ч", "пульс"), R.color.ink2)
        var total = 0.0
        for (s in list) {
            total += s.km
            line(listOf(Fmt.one(total), Fmt.duration(s.seconds), Fmt.one(s.speed), if (s.avgHr > 0) "${s.avgHr}" else "—"), R.color.ink)
        }
        body.addView(c, lp(MATCH, WRAP, bottom = dp(8)))
    }

    private fun cursor(i: Int) {
        charts.forEach { it.setCursor(i) }
        val p = track?.points?.getOrNull(i) ?: return
        val m = marker ?: Marker(map).also {
            it.setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
            it.setInfoWindow(null)
            it.icon = GradientDrawable().apply {
                shape = GradientDrawable.OVAL
                setColor(Color.parseColor("#eb6834")); setStroke(dp(2), Color.WHITE); setSize(dp(16), dp(16))
            }
            map.overlays.add(it)
            marker = it
        }
        m.position = GeoPoint(p.lat, p.lon)
        map.invalidate()
    }

    private fun drawMap(t: Track) {
        // одна гамма от светлого (медленно) к тёмному (быстро), под ней белая подложка
        val ramp = listOf("#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#0d366b").map { Color.parseColor(it) }
        val step = ceil(t.points.size / 4000.0).toInt().coerceAtLeast(1)
        val pts = t.points.filterIndexed { i, _ -> i % step == 0 || i == t.points.lastIndex }
        val top = pts.map { it.speed }.sorted().let { it[(it.size - 1) * 98 / 100] }.coerceAtLeast(5.0)
        fun bin(p: TrackPoint) = (p.speed / top * ramp.size).toInt().coerceIn(0, ramp.lastIndex)
        val gap = maxOf(Track.GAP_S, step * 3L)
        val casing = ArrayList<Polyline>()
        val colored = ArrayList<Polyline>()
        fun add(target: MutableList<Polyline>, p: List<GeoPoint>, color: Int, w: Float) {
            if (p.size < 2) return
            target.add(Polyline(map).apply {
                setPoints(p)
                outlinePaint.color = color
                outlinePaint.strokeWidth = w * resources.displayMetrics.density
                outlinePaint.strokeCap = android.graphics.Paint.Cap.ROUND
                outlinePaint.strokeJoin = android.graphics.Paint.Join.ROUND
                setInfoWindow(null)
            })
        }
        var whole = ArrayList<GeoPoint>()
        var seg = ArrayList<GeoPoint>()
        var cur = -1
        var prev: TrackPoint? = null
        for (p in pts) {
            val q = prev
            val g = GeoPoint(p.lat, p.lon)
            if (q != null && p.time - q.time > gap) {
                add(casing, whole, Color.WHITE, 7f); add(colored, seg, ramp[cur], 4f)
                whole = ArrayList(); seg = ArrayList(); cur = -1
            }
            val b = bin(p)
            whole.add(g)
            seg.add(g)
            if (cur != -1 && b != cur) { add(colored, seg, ramp[cur], 4f); seg = arrayListOf(g) }
            cur = b
            prev = p
        }
        add(casing, whole, Color.WHITE, 7f); add(colored, seg, ramp[cur], 4f)
        map.overlays.addAll(0, casing + colored)

        val legend = card().apply { setPadding(dp(14), dp(8), dp(14), dp(8)) }
        val r = row().apply { gravity = Gravity.CENTER_VERTICAL }
        r.addView(label("Скорость на карте   0 ", 12f, R.color.ink2))
        for (c in ramp) r.addView(View(this).apply { setBackgroundColor(c) }, lp(0, dp(8), 1f))
        r.addView(label(" ${top.toInt()}+ км/ч", 12f, R.color.ink2))
        legend.addView(r)
        body.addView(legend, lp(MATCH, WRAP, bottom = dp(8)))

        val box = BoundingBox.fromGeoPoints(pts.map { GeoPoint(it.lat, it.lon) })
        map.addOnFirstLayoutListener { _, _, _, _, _ -> fit(box) }
        if (map.isLayoutOccurred) fit(box)
    }

    private fun fit(box: BoundingBox) {
        if (box.latitudeSpan < 1e-4 && box.longitudeSpanWithDateLine < 1e-4) {
            map.controller.setZoom(17.0)
            map.controller.setCenter(box.centerWithDateLine)
        } else map.zoomToBoundingBox(box, false, dp(32))
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menu.add(0, 1, 0, "GPX").setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        menu.add(0, 2, 1, "Удалить с телефона").setShowAsAction(MenuItem.SHOW_AS_ACTION_NEVER)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            android.R.id.home -> finish()
            1 -> share()
            2 -> AlertDialog.Builder(this).setTitle("Удалить трек с телефона?").setMessage(name)
                .setNegativeButton("Отмена", null)
                .setPositiveButton("Удалить") { _, _ -> store.file(name).delete(); finish() }.show()
        }
        return true
    }

    private fun share() {
        val t = track ?: return
        exec.execute {
            val f = File(File(cacheDir, "share").apply { mkdirs() }, name.removeSuffix(".csv") + ".gpx")
            f.bufferedWriter().use { Gpx.write(name.removeSuffix(".csv"), t, it) }
            val uri = FileProvider.getUriForFile(this, "com.skitracker.app.files", f)
            ui {
                startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
                    type = "application/gpx+xml"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }, "Поделиться GPX"))
            }
        }
    }

    override fun onResume() { super.onResume(); if (::map.isInitialized) map.onResume() }
    override fun onPause() { if (::map.isInitialized) map.onPause(); super.onPause() }
    override fun onDestroy() {
        exec.shutdownNow()
        if (::map.isInitialized) map.onDetach()
        super.onDestroy()
    }
}
