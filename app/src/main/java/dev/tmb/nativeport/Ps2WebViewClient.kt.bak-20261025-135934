package dev.tmb.nativeport

import android.content.Context
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import java.io.File
import java.io.FileInputStream
import java.io.IOException

class Ps2WebViewClient(private val context: Context) : WebViewClient() {
    override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
        val uri = request.url
        val path = uri.path ?: return super.shouldInterceptRequest(view, request)
        
        // Check if the runtime is requesting the disc image
        if (path.contains("disc.iso") || path.contains("selected-disc.iso") || path.endsWith(".iso")) {
            val isoFile = File(context.noBackupFilesDir, "tmb/selected-disc.iso")
            if (isoFile.exists() && isoFile.length() > 0) {
                try {
                    val fileInputStream = FileInputStream(isoFile)
                    val fileLength = isoFile.length()
                    val rangeHeader = request.requestHeaders["Range"]
                    
                    val headers = mapOf(
                        "Cross-Origin-Opener-Policy" to "same-origin",
                        "Cross-Origin-Embedder-Policy" to "require-corp",
                        "Access-Control-Allow-Origin" to "*",
                        "Accept-Ranges" to "bytes",
                        "Content-Length" to fileLength.toString()
                    )

                    if (rangeHeader != null) {
                        // Handle HTTP Range requests for efficient disc streaming
                        val rangeValue = rangeHeader.removePrefix("bytes=").trim()
                        val parts = rangeValue.split("-")
                        val start = parts[0].toLongOrNull() ?: 0L
                        val end = if (parts.size > 1 && parts[1].isNotBlank()) parts[1].toLongOrNull() ?: (fileLength - 1) else (fileLength - 1)
                        val contentLength = (end - start + 1).coerceAtMost(fileLength - start)

                        fileInputStream.skip(start)
                        val partialHeaders = headers.toMutableMap()
                        partialHeaders["Content-Range"] = "bytes $start-$end/$fileLength"
                        partialHeaders["Content-Length"] = contentLength.toString()

                        return WebResourceResponse("application/octet-stream", "UTF-8", 206, "Partial Content", partialHeaders, fileInputStream)
                    }

                    return WebResourceResponse("application/octet-stream", "UTF-8", 200, "OK", headers, fileInputStream)
                } catch (_: Exception) {
                    // Fallthrough to 404 / default handler if streaming fails
                }
            }
        }

        val assetPath = when {
            path.endsWith("/") || path.endsWith("/ps2web.html") -> "www/ps2web.html"
            path.contains("www/") -> "www" + path.substring(path.indexOf("www/") + 3)
            else -> "www" + path
        }

        try {
            val inputStream = context.assets.open(assetPath)
            
            if (assetPath.endsWith(".html")) {
                var html = inputStream.bufferedReader().use { it.readText() }
                val polyfill = "<script>if (!window.crossOriginIsolated) { Object.defineProperty(window, 'crossOriginIsolated', { get: () => true }); }</script>"
                html = html.replaceFirst("<head>", "<head>\n$polyfill")
                val bytes = html.toByteArray(Charsets.UTF_8)
                return WebResourceResponse("text/html", "UTF-8", 200, "OK", mapOf(
                    "Cross-Origin-Opener-Policy" to "same-origin",
                    "Cross-Origin-Embedder-Policy" to "require-corp",
                    "Access-Control-Allow-Origin" to "*"
                ), bytes.inputStream())
            }

            if (assetPath.endsWith(".js")) {
                var js = inputStream.bufferedReader().use { it.readText() }
                js = js.replace("!crossOriginIsolated", "false")
                val bytes = js.toByteArray(Charsets.UTF_8)
                return WebResourceResponse("application/javascript", "UTF-8", 200, "OK", mapOf(
                    "Cross-Origin-Opener-Policy" to "same-origin",
                    "Cross-Origin-Embedder-Policy" to "require-corp",
                    "Access-Control-Allow-Origin" to "*"
                ), bytes.inputStream())
            }

            val mimeType = when {
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
