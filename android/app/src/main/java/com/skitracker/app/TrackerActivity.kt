package com.skitracker.app

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.text.InputType
import android.view.Menu
import android.view.MenuItem
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.util.concurrent.Executors

/** Подключение к трекеру: список треков, скачивание, удаление. */
class TrackerActivity : Activity() {
    private val exec = Executors.newSingleThreadExecutor()
    private lateinit var store: Store
    private lateinit var conn: TrackerConnection
    private lateinit var status: TextView
    private lateinit var connectBtn: Button
    private lateinit var allBtn: Button
    private lateinit var list: LinearLayout
    private var client: TrackerClient? = null
    private var remote: List<RemoteTrack> = emptyList()
    private var busy = false
    private val prefs by lazy { getSharedPreferences("wifi", MODE_PRIVATE) }

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        actionBar?.setDisplayHomeAsUpEnabled(true)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        store = Store(this)
        conn = TrackerConnection(applicationContext)
        val root = column(12)
        status = label("Включите Wi-Fi на трекере: левая кнопка 1,5 с и отпустить. Запись должна быть остановлена.", 15f, R.color.ink2)
        root.addView(status, lp(MATCH, WRAP, bottom = dp(8)))
        connectBtn = Button(this).apply { text = "Подключиться"; onClick { connect() } }
        root.addView(connectBtn, lp(MATCH, WRAP))
        allBtn = Button(this).apply { text = "Скачать все новые"; visibility = android.view.View.GONE
            onClick { download(remote.filter { !store.has(it) }) } }
        root.addView(allBtn, lp(MATCH, WRAP, bottom = dp(8)))
        list = column()
        root.addView(ScrollView(this).apply { addView(list) }, lp(MATCH, 0, 1f))
        setContentView(root)
    }

    private fun run(message: String, job: () -> Unit) {
        if (busy) return
        setBusy(true)
        status.text = message
        exec.execute {
            val err = runCatching(job).exceptionOrNull()
            ui {
                setBusy(false)
                if (err is NeedManualWifi) manualWifi()
                else if (err != null) status.text = "Ошибка: ${err.message ?: err.javaClass.simpleName}"
            }
        }
    }

    private fun setBusy(v: Boolean) {
        busy = v
        connectBtn.isEnabled = !v
        allBtn.isEnabled = !v
    }

    private fun ssid() = prefs.getString("ssid", null) ?: "SkiTracker"
    private fun pass() = prefs.getString("pass", null) ?: "skitrack123"

    private fun connect() = run("Подключаюсь к «${ssid()}»… Подтвердите подключение в окне системы.") {
        val c = conn.connect(ssid(), pass())
        client = c
        reload(c)
    }

    private fun reload(c: TrackerClient, prefix: String = "") {
        val info = c.info()
        val tracks = c.tracks()
        ui {
            remote = tracks
            status.text = prefix + "Подключено. Свободно ${Fmt.size(info.free)} из ${Fmt.size(info.total)}, батарея ${Fmt.one(info.battery)} В."
            connectBtn.text = "Обновить"
            render()
        }
    }

    private fun render() {
        list.removeAllViews()
        val fresh = remote.count { !store.has(it) }
        allBtn.visibility = if (fresh > 0) android.view.View.VISIBLE else android.view.View.GONE
        allBtn.text = "Скачать все новые ($fresh)"
        if (remote.isEmpty()) list.addView(label("На трекере нет треков.", 15f, R.color.ink2))
        for (t in remote) {
            val c = card()
            c.addView(label(t.name.removeSuffix(".csv"), 16f, bold = true))
            c.addView(label(Fmt.size(t.size) + if (store.has(t)) "   ✓ на телефоне" else "", 14f, R.color.ink2))
            val buttons = row()
            if (store.has(t)) buttons.addView(Button(this).apply { text = "Открыть"; onClick {
                startActivity(Intent(this@TrackerActivity, TrackActivity::class.java).putExtra("name", t.name)) } })
            else buttons.addView(Button(this).apply { text = "Скачать"; onClick { download(listOf(t)) } })
            buttons.addView(Button(this).apply { text = "Удалить с трекера"; onClick { confirmDelete(t) } })
            c.addView(buttons)
            list.addView(c, lp(MATCH, WRAP, bottom = dp(8)))
        }
    }

    private fun download(items: List<RemoteTrack>) {
        val c = client ?: return
        run("Скачиваю…") {
            items.forEachIndexed { i, t ->
                var shown = 0L
                c.download(t, store.file(t.name)) { n ->
                    if (n - shown > 32768) {
                        shown = n
                        ui { status.text = "Файл ${i + 1} из ${items.size}: ${n * 100 / t.size.coerceAtLeast(1)}%" }
                    }
                }
                ui { render() }
            }
            reload(c, "Скачано: ${items.size}. ")
        }
    }

    private fun confirmDelete(t: RemoteTrack) {
        val warn = if (store.has(t)) "Копия на телефоне останется." else "Этот трек ещё НЕ скачан на телефон — он пропадёт навсегда."
        AlertDialog.Builder(this)
            .setTitle("Удалить с трекера?")
            .setMessage("${t.name}\n\n$warn")
            .setNegativeButton("Отмена", null)
            .setPositiveButton("Удалить") { _, _ ->
                val c = client ?: return@setPositiveButton
                run("Удаляю…") { c.delete(t); reload(c, "Удалено. ") }
            }.show()
    }

    private fun manualWifi() {
        status.text = "На этой версии Android подключитесь к сети «${ssid()}» вручную, вернитесь сюда и снова нажмите «Подключиться»."
        startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menu.add("Сеть трекера").setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) { finish(); return true }
        val box = column(20)
        val s = EditText(this).apply { setText(ssid()); hint = "Имя сети" }
        val p = EditText(this).apply { setText(pass()); hint = "Пароль (8–63 символа)"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD }
        box.addView(s); box.addView(p)
        AlertDialog.Builder(this).setTitle("Сеть трекера").setView(box)
            .setNegativeButton("Отмена", null)
            .setPositiveButton("Сохранить") { _, _ ->
                val ns = s.text.toString().trim(); val np = p.text.toString()
                if (ns.isEmpty() || np.length !in 8..63 || np.any { it.code !in 32..126 })
                    status.text = "Не сохранено: нужно имя сети и пароль 8–63 латинских символа."
                else prefs.edit().putString("ssid", ns).putString("pass", np).apply()
            }.show()
        return true
    }

    override fun onDestroy() {
        conn.close()
        exec.shutdownNow()
        super.onDestroy()
    }
}
