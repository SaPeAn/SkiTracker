package com.skitracker.app

import android.app.Activity
import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

fun Context.dp(v: Int) = (v * resources.displayMetrics.density + 0.5f).toInt()

fun Context.label(text: String, sp: Float = 14f, color: Int = R.color.ink, bold: Boolean = false) = TextView(this).apply {
    this.text = text
    textSize = sp
    setTextColor(getColor(color))
    if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
}

fun Context.column(pad: Int = 0) = LinearLayout(this).apply {
    orientation = LinearLayout.VERTICAL
    setPadding(dp(pad), dp(pad), dp(pad), dp(pad))
}

fun Context.row() = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }

fun Context.card() = column(14).apply {
    background = GradientDrawable().apply {
        setColor(getColor(R.color.card))
        cornerRadius = dp(10).toFloat()
    }
}

fun lp(w: Int, h: Int, weight: Float = 0f, bottom: Int = 0) =
    LinearLayout.LayoutParams(w, h, weight).apply { bottomMargin = bottom }

const val MATCH = LinearLayout.LayoutParams.MATCH_PARENT
const val WRAP = LinearLayout.LayoutParams.WRAP_CONTENT

/** Результат фоновой работы на экран, если он ещё жив. */
fun Activity.ui(block: () -> Unit) = runOnUiThread { if (!isDestroyed && !isFinishing) block() }

fun View.onClick(f: () -> Unit) = setOnClickListener { f() }
