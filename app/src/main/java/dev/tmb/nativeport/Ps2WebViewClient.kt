package dev.tmb.nativeport

import android.content.Context
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import java.io.IOException

class Ps2WebViewClient(private val context: Context) : WebViewClient() {
    override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
        val uri = request.url
        val path = uri.path ?: return super.shouldInterceptRequest(view, request)
        
        val assetPath = when {
            path.endsWith("/") || path.endsWith("/ps2web.html") -> "www/ps2web.html"
            path.contains("www/") -> "www" + path.substring(path.indexOf("www/") + 3)
            else -> "www" + path
        }

        try {
            val inputStream = context.assets.open(assetPath)
            val mimeType = when {
                assetPath.endsWith(".html") -> "text/html"
                assetPath.endsWith(".js") -> "application/javascript"
                assetPath.endsWith(".wasm") -> "application/wasm"
                assetPath.endsWith(".data") -> "application/octet-stream"
                assetPath.endsWith(".json") -> "application/json"
                else -> "application/octet-stream"
            }

            val headers = mapOf(
                "Cross-Origin-Opener-Policy" to "same-origin",
                "Cross-Origin-Embedder-Policy" to "require-corp",
                "Access-Control-Allow-Origin" to "*"
            )

            return WebResourceResponse(mimeType, "UTF-8", 200, "OK", headers, inputStream)
        } catch (e: IOException) {
            return super.shouldInterceptRequest(view, request)
        }
    }
}
