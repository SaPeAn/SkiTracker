package com.skitracker.app

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import java.util.concurrent.Executors

/** Список сохранённых треков. */
class MainActivity : Activity() {
    private val exec = Executors.newSingleThreadExecutor()
    private lateinit var store: Store
    private lateinit var list: LinearLayout

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        store = Store(this)
        val root = column(12)
        root.addView(Button(this).apply {
            text = "Загрузить с трекера"
            onClick { startActivity(Intent(this@MainActivity, TrackerActivity::class.java)) }
        }, lp(MATCH, WRAP, bottom = dp(8)))
        list = column()
        root.addView(ScrollView(this).apply { addView(list) }, lp(MATCH, 0, 1f))
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        exec.execute {
            val items = store.names().map { it to runCatching { store.load(it) }.getOrNull() }
            ui { show(items) }
        }
    }

    private fun show(items: List<Pair<String, Track?>>) {
        list.removeAllViews()
        if (items.isEmpty()) {
            list.addView(label("Треков пока нет.\n\nНа трекере остановите запись (правая кнопка 1,5 с), " +
                "включите Wi-Fi (левая кнопка 1,5 с и отпустить) и нажмите «Загрузить с трекера».", 15f, R.color.ink2).apply {
                gravity = Gravity.CENTER
                setPadding(dp(16), dp(48), dp(16), 0)
            })
            return
        }
        for ((name, t) in items) {
            val c = card()
            val s = t?.stats
            if (s == null || t.points.isEmpty()) {
                c.addView(label(name, 16f, bold = true))
                c.addView(label("Нет координат", 14f, R.color.ink2))
            } else {
                c.addView(label(if (s.start > 0) Fmt.date(s.start) else name, 16f, bold = true))
                val hr = if (s.avgHr > 0) "   пульс ${s.avgHr}" else ""
                c.addView(label("${Fmt.km(s.distM)} км   ${Fmt.duration(s.elapsed)}   ${Fmt.one(s.avgSpeed)} км/ч$hr", 14f, R.color.ink2))
            }
            c.onClick { startActivity(Intent(this, TrackActivity::class.java).putExtra("name", name)) }
            list.addView(c, lp(MATCH, WRAP, bottom = dp(8)))
        }
    }

    override fun onDestroy() {
        exec.shutdownNow()
        super.onDestroy()
    }
}
