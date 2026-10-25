package dev.tmb.nativeport


import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.WindowManager
import android.util.Log
import android.webkit.ConsoleMessage
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.widget.Button
import android.widget.FrameLayout
import android.widget.Toast
import java.io.File
import java.io.FileOutputStream
import java.io.RandomAccessFile
import java.nio.file.Files
import java.nio.file.StandardCopyOption

class MainActivity : Activity() {
    private lateinit var runtimeServer: RuntimeHttpServer
    private lateinit var webView: WebView
    private lateinit var isoButton: Button
    private val stagedIso: File
        get() = File(File(noBackupFilesDir, "tmb"), "selected-disc.iso")

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // Serve worker-originated ISO range requests over loopback TCP.
        runtimeServer = RuntimeHttpServer(applicationContext)
        runtimeServer.start()

        val root = FrameLayout(this)
        webView = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = true
            settings.allowContentAccess = true
            settings.databaseEnabled = true
            settings.mediaPlaybackRequiresUserGesture = false
            setLayerType(WebView.LAYER_TYPE_HARDWARE, null)
            webViewClient = Ps2WebViewClient(context)
            webChromeClient = object : WebChromeClient() {
                override fun onConsoleMessage(message: ConsoleMessage): Boolean {
                    Log.e(
                        "TMB_WEB",
                        "JS ${message.messageLevel()}: ${message.message()} " +
                            "at ${message.sourceId()}:${message.lineNumber()}"
                    )
                    return true
                }
            }
        }
        root.addView(webView, FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT
        ))

        isoButton = Button(this).apply {
            isAllCaps = false
            text = if (stagedIso.isFile && stagedIso.length() > 0) "Change TMB ISO" else "Select TMB ISO"
            setOnClickListener { openIsoPicker() }
            elevation = 12f
        }
        val margin = (12 * resources.displayMetrics.density).toInt()
        val buttonLayout = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP or Gravity.START
        ).apply { setMargins(margin, margin, margin, 0) }
        root.addView(isoButton, buttonLayout)

        setContentView(root)
        // The bundled runtime requests this path. Ps2WebViewClient maps it to the staged private ISO.
        // Permit SharedArrayBuffer only for the app-owned runtime origin.
        val hasMultiProfile = WebViewFeature.isFeatureSupported(WebViewFeature.MULTI_PROFILE)
        val hasIsolationAllowlist = WebViewFeature.isFeatureSupported(
            WebViewFeature.CROSS_ORIGIN_ISOLATED_ALLOWLIST
        )
        if (hasMultiProfile && hasIsolationAllowlist) {
            try {
                WebViewCompat.getProfile(webView)
                    .setCrossOriginIsolatedAllowlist(setOf(runtimeServer.origin))
                Log.i("TMB_WEB", "Cross-origin isolated APIs allowlisted for ${runtimeServer.origin}")
            } catch (error: Exception) {
                Log.e("TMB_WEB", "Failed to configure SharedArrayBuffer permission", error)
            }
        } else {
            Log.e(
                "TMB_WEB",
                "WebView feature unavailable: MULTI_PROFILE=$hasMultiProfile, " +
                    "CROSS_ORIGIN_ISOLATED_ALLOWLIST=$hasIsolationAllowlist"
            )
        }
        webView.loadUrl("${runtimeServer.origin}/ps2web.html?disc=disc.iso")
    }

    private fun openIsoPicker() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf(
                "application/octet-stream",
                "application/x-iso9660-image",
                "application/x-cd-image"
            ))
        }
        try {
            startActivityForResult(intent, PICK_ISO_REQUEST)
        } catch (error: Exception) {
            Toast.makeText(this, "Cannot open the file picker: ${error.message}", Toast.LENGTH_LONG).show()
        }
    }

    @Deprecated("Uses the platform activity-result callback for compatibility with this Activity-based project")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_ISO_REQUEST || resultCode != RESULT_OK) return
        val uri: Uri = data?.data ?: return

        isoButton.isEnabled = false
        isoButton.text = "Copying ISO…"
        Thread {
            var bytesCopied = 0L
            var failure: String? = null
            try {
                bytesCopied = copyAndStageIso(uri)
            } catch (error: Exception) {
                failure = error.message ?: error.javaClass.simpleName
            }
            val resultBytes = bytesCopied
            val resultFailure = failure
            runOnUiThread {
                isoButton.isEnabled = true
                isoButton.text = if (stagedIso.isFile && stagedIso.length() > 0) "Change TMB ISO" else "Select TMB ISO"
                if (resultFailure == null) {
                    Toast.makeText(
                        this,
                        "ISO staged (${resultBytes / (1024 * 1024)} MiB). Reloading runtime. Bundled boot target: PAL SCES_503.60.",
                        Toast.LENGTH_LONG
                    ).show()
                    // The first page load may have failed before an ISO existed. Reload to restart its disc fetch.
                    webView.reload()
                } else {
                    Toast.makeText(this, "ISO not staged: $resultFailure", Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private fun copyAndStageIso(uri: Uri): Long {
        val parent = stagedIso.parentFile ?: error("Invalid private storage path")
        if (!parent.exists() && !parent.mkdirs()) error("Cannot create app-private ISO directory")
        val part = File(parent, "selected-disc.iso.part")
        if (part.exists() && !part.delete()) error("Cannot remove the previous temporary ISO copy")

        var copied = 0L
        val source = contentResolver.openInputStream(uri) ?: error("Android could not open the selected file")
        source.use { input ->
            FileOutputStream(part).use { output ->
                val buffer = ByteArray(1024 * 1024)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    if (count == 0) continue
                    output.write(buffer, 0, count)
                    copied += count
                }
                output.fd.sync()
            }
        }

        if (copied < 32774L) {
            part.delete()
            error("File is too small to be a PS2 ISO image")
        }
        if (!hasIso9660PrimaryVolumeDescriptor(part)) {
            part.delete()
            error("No ISO-9660 primary volume descriptor at sector 16; choose a plain .iso, not CHD/BIN/CUE")
        }

        // Same-directory replacement keeps the old staged image if copying or validation fails.
        Files.move(part.toPath(), stagedIso.toPath(), StandardCopyOption.REPLACE_EXISTING)
        return copied
    }

    private fun hasIso9660PrimaryVolumeDescriptor(file: File): Boolean =
        RandomAccessFile(file, "r").use { raf ->
            if (raf.length() < 32775L) return@use false
            raf.seek(32768L)
            val descriptor = ByteArray(7)
            raf.readFully(descriptor)
            descriptor[0].toInt() == 1 &&
                String(descriptor, 1, 5, Charsets.US_ASCII) == "CD001" &&
                descriptor[6].toInt() == 1
        }

    override fun onResume() {
        super.onResume()
        if (::webView.isInitialized) webView.onResume()
    }

    override fun onPause() {
        if (::webView.isInitialized) webView.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        if (::webView.isInitialized) webView.destroy()
        if (::runtimeServer.isInitialized) runtimeServer.close()

        super.onDestroy()
    }

    private companion object {
        const val PICK_ISO_REQUEST = 4172
    }
}
