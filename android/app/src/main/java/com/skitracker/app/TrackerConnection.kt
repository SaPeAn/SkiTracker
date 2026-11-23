package com.skitracker.app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import java.net.HttpURLConnection
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class NeedManualWifi : Exception()

/**
 * Подключение к точке доступа трекера.
 * Сначала пробуем уже подключённый Wi-Fi; на Android 10+ просим систему подключиться самой.
 * Запросы идут строго через сеть трекера, мобильный интернет при этом продолжает работать.
 */
class TrackerConnection(context: Context) {
    private val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var waiting: CountDownLatch? = null
    private var closed = false

    private fun client(n: Network) = TrackerClient { n.openConnection(it) as HttpURLConnection }

    /** Блокирующий вызов, не для главного потока. */
    fun connect(ssid: String, pass: String): TrackerClient {
        existing()?.let { return it }
        if (Build.VERSION.SDK_INT < 29) throw NeedManualWifi()
        return request(ssid, pass)
    }

    @Suppress("DEPRECATION")
    private fun existing(): TrackerClient? = cm.allNetworks
        .filter { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
        .map { client(it) }
        .firstOrNull { c -> runCatching { c.info(2500) }.isSuccess }

    @androidx.annotation.RequiresApi(29)
    private fun request(ssid: String, pass: String): TrackerClient {
        close()
        val spec = WifiNetworkSpecifier.Builder().setSsid(ssid).setWpa2Passphrase(pass).build()
        val req = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(spec)
            .build()
        val latch = CountDownLatch(1)
        var found: Network? = null
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { found = network; latch.countDown() }
            override fun onUnavailable() { latch.countDown() }
        }
        synchronized(this) {
            closed = false
            callback = cb
            waiting = latch
            cm.requestNetwork(req, cb, 60000)
        }
        latch.await(65, TimeUnit.SECONDS)
        val n = found
        if (n == null || synchronized(this) { closed }) {
            close()
            throw Exception("Сеть «$ssid» не найдена или подключение отменено")
        }
        val c = client(n)
        var last: Throwable? = null
        repeat(5) {   // адрес по DHCP появляется не сразу
            val r = runCatching { c.info() }
            if (r.isSuccess) return c
            last = r.exceptionOrNull()
            Thread.sleep(800)
        }
        close()
        throw Exception("Подключились к «$ssid», но трекер не отвечает: ${last?.message}")
    }

    @Synchronized
    fun close() {
        closed = true
        callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null
        waiting?.countDown()
        waiting = null
    }
}
