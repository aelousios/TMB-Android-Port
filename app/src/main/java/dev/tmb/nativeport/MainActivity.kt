package dev.tmb.nativeport

import android.annotation.SuppressLint
import android.app.Activity
import android.os.Bundle
import android.view.WindowManager
import android.webkit.WebView
import android.webkit.WebViewClient

class MainActivity : Activity() {
    private lateinit var webView: WebView

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        webView = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = true
            settings.allowContentAccess = true
            settings.databaseEnabled = true
            settings.mediaPlaybackRequiresUserGesture = false
            setLayerType(WebView.LAYER_TYPE_HARDWARE, null)
            webViewClient = WebViewClient()
        }

        setContentView(webView)
        webView.loadUrl("file:///android_asset/www/ps2web.html")
    }

    override fun onResume() {
        super.onResume()
        if (::webView.isInitialized) webView.onResume()
    }

    override fun onPause() {
        if (::webView.isInitialized) webView.onPause()
        super.onPause()
    }
}
