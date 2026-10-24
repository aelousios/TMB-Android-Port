package dev.tmb.nativeport

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class MainActivity : Activity() {
    private lateinit var gameView: NativeGameView
    private lateinit var hud: TextView
    private lateinit var isoStatus: TextView
    private val handler = Handler(Looper.getMainLooper())

    private val hudUpdater = object : Runnable {
        override fun run() {
            if (::hud.isInitialized) hud.text = NativeBridge.getHudText()
            handler.postDelayed(this, 250L)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            )

        val root = FrameLayout(this).apply {
            setBackgroundColor(Color.rgb(11, 15, 20))
        }
        gameView = NativeGameView(this)
        root.addView(gameView, FrameLayout.LayoutParams(-1, -1))

        hud = TextView(this).apply {
            text = "TWISTED METAL: BLACK  •  NATIVE PORT LAB"
            maxWidth = dp(365)
            maxLines = 4
            setTextColor(Color.rgb(230, 237, 243))
            textSize = 11f
            typeface = Typeface.MONOSPACE
            setPadding(dp(12), dp(9), dp(12), dp(9))
            background = panelBackground()
        }
        root.addView(hud, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.START).apply {
            setMargins(dp(12), dp(12), dp(12), dp(12))
        })

        val isoButton = makeButton("SELECT US PS2 ISO", 0)
        isoButton.setOnClickListener { openIsoPicker() }
        root.addView(isoButton, FrameLayout.LayoutParams(-2, dp(44), Gravity.TOP or Gravity.END).apply {
            setMargins(dp(12), dp(12), dp(12), dp(12))
        })

        isoStatus = TextView(this).apply {
            text = "Select US NTSC-U ISO to inspect • no game boot yet"
            maxWidth = dp(380)
            maxLines = 4
            setTextColor(Color.rgb(210, 218, 224))
            textSize = 11f
            typeface = Typeface.MONOSPACE
            setPadding(dp(10), dp(7), dp(10), dp(7))
            background = panelBackground()
        }
        root.addView(isoStatus, FrameLayout.LayoutParams(-2, -2, Gravity.TOP or Gravity.END).apply {
            setMargins(dp(12), dp(62), dp(12), dp(12))
        })

        val leftControls = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
        }
        val steeringRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        steeringRow.addView(controlButton("◀ STEER", 0), controlParams())
        steeringRow.addView(controlButton("STEER ▶", 1), controlParams())
        leftControls.addView(steeringRow)
        val drivingRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        drivingRow.addView(controlButton("□  BRAKE", 3), controlParams())
        drivingRow.addView(controlButton("✕  ACCEL", 2), controlParams())
        leftControls.addView(drivingRow)
        root.addView(leftControls, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.START).apply {
            setMargins(dp(10), dp(10), dp(10), dp(18))
        })

        val weapons = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
        }
        weapons.addView(controlButton("○  FIRE", 4, action = true), controlParams())
        weapons.addView(controlButton("△  SPECIAL", 5, action = true), controlParams())
        root.addView(weapons, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.END).apply {
            setMargins(dp(10), dp(10), dp(12), dp(18))
        })

        val notice = TextView(this).apply {
            text = "TECH DEMO ONLY  •  ORIGINAL GAME ENGINE NOT YET INTEGRATED"
            textSize = 10f
            typeface = Typeface.MONOSPACE
            setTextColor(Color.rgb(245, 190, 104))
            setPadding(dp(8), dp(5), dp(8), dp(5))
            background = panelBackground()
        }
        root.addView(notice, FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL).apply {
            bottomMargin = dp(14)
        })

        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        if (::gameView.isInitialized) gameView.onResume()
        handler.removeCallbacks(hudUpdater)
        handler.post(hudUpdater)
    }

    override fun onPause() {
        handler.removeCallbacks(hudUpdater)
        if (::gameView.isInitialized) gameView.onPause()
        super.onPause()
    }

    private fun openIsoPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/x-iso9660-image", "application/x-cd-image"))
        }
        try {
            startActivityForResult(intent, REQUEST_ISO)
        } catch (_: Exception) {
            Toast.makeText(this, "No file picker is available on this device.", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Uses the platform document picker without adding an AndroidX dependency")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQUEST_ISO || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return
        isoStatus.text = "Checking SCUS_971.01…"
        Thread {
            var result: String
            try {
                val pfd = contentResolver.openFileDescriptor(uri, "r")
                if (pfd == null) {
                    result = "Could not open selected file"
                } else {
                    pfd.use { descriptor -> result = NativeBridge.inspectIsoFd(descriptor.fd) }
                }
            } catch (e: Exception) {
                result = "ISO inspection failed: ${e.message ?: e.javaClass.simpleName}"
            }
            runOnUiThread {
                val displayName = uri.lastPathSegment?.substringAfterLast('/') ?: "Selected file"
                isoStatus.text = "$displayName\n$result"
            }
        }.start()
    }

    private fun controlButton(label: String, control: Int, action: Boolean = false): Button {
        val button = makeButton(label, control).apply {
            minWidth = dp(if (label.length <= 2) 54 else if (label.length < 9) 78 else 94)
            minHeight = dp(54)
            textSize = if (label.length <= 2) 23f else 10f
            alpha = 0.88f
            if (action) setTextColor(Color.rgb(255, 222, 185))
            setOnTouchListener { view, event ->
                when (event.actionMasked) {
                    MotionEvent.ACTION_DOWN -> {
                        NativeBridge.setControl(control, true)
                        view.isPressed = true
                        true
                    }
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                        NativeBridge.setControl(control, false)
                        view.isPressed = false
                        true
                    }
                    else -> true
                }
            }
        }
        return button
    }

    private fun makeButton(label: String, ignoredControl: Int): Button {
        @Suppress("UNUSED_VARIABLE") val intentionallyUnused = ignoredControl
        return Button(this).apply {
            text = label
            isAllCaps = false
            setTextColor(Color.WHITE)
            textSize = 11f
            typeface = Typeface.DEFAULT_BOLD
            background = panelBackground(0xCC202832.toInt())
            stateListAnimator = null
            setPadding(dp(9), dp(4), dp(9), dp(4))
        }
    }

    private fun controlParams() = LinearLayout.LayoutParams(-2, dp(54)).apply {
        setMargins(dp(4), dp(3), dp(4), dp(3))
    }

    private fun panelBackground(color: Int = 0xCC121820.toInt()) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(10).toFloat()
        setStroke(dp(1), 0x886A7886.toInt())
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private class NativeGameView(activity: Activity) : GLSurfaceView(activity) {
        init {
            setEGLContextClientVersion(2)
            preserveEGLContextOnPause = true
            setRenderer(NativeRenderer())
            renderMode = RENDERMODE_CONTINUOUSLY
        }

        private class NativeRenderer : Renderer {
            private var lastFrameNanos = 0L

            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                NativeBridge.surfaceCreated()
                lastFrameNanos = System.nanoTime()
            }

            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                NativeBridge.surfaceChanged(width, height)
            }

            override fun onDrawFrame(gl: GL10?) {
                val now = System.nanoTime()
                val delta = if (lastFrameNanos == 0L) 1f / 60f else
                    ((now - lastFrameNanos).toDouble() / 1_000_000_000.0).toFloat().coerceIn(0f, 0.05f)
                lastFrameNanos = now
                NativeBridge.renderFrame(delta)
            }
        }
    }

    companion object {
        private const val REQUEST_ISO = 4917
    }
}
