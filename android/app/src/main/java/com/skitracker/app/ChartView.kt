package com.skitracker.app

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.log10
import kotlin.math.pow

/** Один показатель по времени. Касание ставит курсор, общий для всех графиков и карты. */
@SuppressLint("ViewConstructor")
class ChartView(
    context: Context, private val title: String, private val unit: String, color: Int, zeroBased: Boolean,
    private val points: List<TrackPoint>, private val onCursor: (Int) -> Unit, private val value: (TrackPoint) -> Double?,
) : View(context) {
    private val d = resources.displayMetrics.density
    private val sp = resources.displayMetrics.scaledDensity
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        this.color = color; style = Paint.Style.STROKE; strokeWidth = 2 * d
        strokeJoin = Paint.Join.ROUND; strokeCap = Paint.Cap.ROUND
    }
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = color }
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = context.getColor(R.color.card) }
    private val grid = Paint().apply { this.color = context.getColor(R.color.grid); strokeWidth = d }
    private val cross = Paint().apply { this.color = context.getColor(R.color.ink2); strokeWidth = d }
    private val ink = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = context.getColor(R.color.ink); textSize = 14 * sp }
    private val small = Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = context.getColor(R.color.ink2); textSize = 11 * sp }
    private var cursor = -1

    private val lo: Double
    private val hi: Double
    private val step: Double

    init {
        val v = points.mapNotNull(value)
        val mn = if (zeroBased) 0.0 else (v.minOrNull() ?: 0.0)
        val mx = v.maxOrNull() ?: 1.0
        step = niceStep((mx - mn).coerceAtLeast(1.0) / 3)
        lo = floor(mn / step) * step
        hi = (ceil(mx / step) * step).coerceAtLeast(lo + step)
        contentDescription = "График: $title, от ${Fmt.one(v.minOrNull() ?: 0.0)} до ${Fmt.one(mx)} $unit"
    }

    private fun niceStep(raw: Double): Double {
        val p = 10.0.pow(floor(log10(raw)))
        return p * when { raw / p <= 1 -> 1; raw / p <= 2 -> 2; raw / p <= 5 -> 5; else -> 10 }
    }

    fun setCursor(i: Int) { cursor = i; invalidate() }

    private val left get() = 36 * d
    private val top get() = 30 * d
    private val bottom get() = height - 20 * d
    private val right get() = width - 8 * d
    private val span get() = (points.last().time - points[0].time).coerceAtLeast(1)

    private fun x(p: TrackPoint) = left + (right - left) * (p.time - points[0].time) / span
    private fun y(v: Double) = (bottom - (bottom - top) * ((v - lo) / (hi - lo))).toFloat()

    override fun onDraw(c: Canvas) {
        if (points.size < 2) return
        c.drawText(title, 0f, 16 * sp, ink)
        var g = lo
        while (g <= hi + step / 1000) {
            c.drawLine(left, y(g), right, y(g), grid)
            val t = Fmt.one(g).removeSuffix(".0")
            c.drawText(t, left - 6 * d - small.measureText(t), y(g) + 4 * sp, small)
            g += step
        }
        c.drawText("0:00", left, height - 4 * d, small)
        val end = Fmt.duration(span)
        c.drawText(end, right - small.measureText(end), height - 4 * d, small)

        // в один столбец пикселей попадает много точек — усредняем
        val path = Path()
        var i = 0
        var pen = false
        var prevTime = 0L
        while (i < points.size) {
            val px = x(points[i]).toInt()
            var sum = 0.0; var n = 0
            val first = points[i].time
            var last = first
            while (i < points.size && x(points[i]).toInt() == px) {
                value(points[i])?.let { sum += it; n++ }
                last = points[i].time
                i++
            }
            if (n == 0) { pen = false; continue }
            val yy = y(sum / n)
            if (pen && first - prevTime <= Track.GAP_S) path.lineTo(px.toFloat(), yy) else path.moveTo(px.toFloat(), yy)
            pen = true
            prevTime = last
        }
        c.drawPath(path, line)

        if (cursor in points.indices) {
            val p = points[cursor]
            c.drawLine(x(p), top, x(p), bottom, cross)
            val v = value(p)
            val txt = (if (v == null) "—" else Fmt.one(v).removeSuffix(".0") + " " + unit) + "   " + Fmt.duration(p.time - points[0].time)
            c.drawText(txt, right - ink.measureText(txt), 16 * sp, ink)
            if (v != null) {
                c.drawCircle(x(p), y(v), 6 * d, ring)
                c.drawCircle(x(p), y(v), 4 * d, dot)
            }
        } else {
            c.drawText(unit, right - small.measureText(unit), 16 * sp, small)
        }
    }

    private var downX = 0f
    private var downY = 0f

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (points.size < 2) return false
        when (e.action) {
            MotionEvent.ACTION_DOWN -> { downX = e.x; downY = e.y }
            MotionEvent.ACTION_MOVE ->   // горизонтальное ведение — курсор, вертикальное — прокрутка
                if (abs(e.x - downX) > abs(e.y - downY)) parent.requestDisallowInterceptTouchEvent(true)
        }
        if (e.action != MotionEvent.ACTION_CANCEL) {
            val f = ((e.x - left) / (right - left)).coerceIn(0f, 1f)
            val t = points[0].time + (span * f).toLong()
            var i = points.binarySearch { it.time.compareTo(t) }
            if (i < 0) i = (-i - 1).coerceAtMost(points.lastIndex)
            onCursor(i)
        }
        return true
    }
}
