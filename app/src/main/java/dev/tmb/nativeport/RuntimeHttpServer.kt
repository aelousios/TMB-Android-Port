package dev.tmb.nativeport

// TMB_LOOPBACK_RANGE_SERVER_V1
// A local HTTP endpoint is required because the emulation's Emscripten worker fetches
// ISO sectors asynchronously with HTTP Range requests. WebViewClient interception alone
// did not deliver those worker requests on the Pixel.

import android.content.Context
import android.util.Log
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.RandomAccessFile
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

internal class RuntimeHttpServer(private val context: Context) : Closeable {
    @Volatile private var listener: ServerSocket? = null
    @Volatile private var running = false
    private val clients = Executors.newCachedThreadPool { runnable ->
        Thread(runnable, "TMB-HTTP-client").apply { isDaemon = true }
    }
    private val assetCache = ConcurrentHashMap<String, ByteArray>()

    val origin: String
        get() = "http://127.0.0.1:${listener?.localPort ?: error("Runtime server is not started") }"

    fun start() {
        if (listener != null) return
        val socket = ServerSocket()
        socket.reuseAddress = true
        try {
            socket.bind(InetSocketAddress(InetAddress.getByName("127.0.0.1"), PREFERRED_PORT), 64)
        } catch (_: IOException) {
            // Keep the app usable if the preferred port is occupied; the active port is allowlisted dynamically.
            socket.bind(InetSocketAddress(InetAddress.getByName("127.0.0.1"), 0), 64)
        }
        listener = socket
        running = true
        Thread({
            while (running) {
                try {
                    val client = socket.accept()
                    clients.execute { handleClient(client) }
                } catch (_: SocketException) {
                    if (running) Log.e(TAG, "Loopback accept failed: socket exception")
                } catch (error: Exception) {
                    if (running) Log.e(TAG, "Loopback accept failed", error)
                }
            }
        }, "TMB-HTTP-acceptor").apply { isDaemon = true; start() }
        Log.i(TAG, "Serving runtime on $origin")
    }

    override fun close() {
        running = false
        try { listener?.close() } catch (_: Exception) { }
        listener = null
        clients.shutdownNow()
        Log.i(TAG, "Loopback runtime server stopped")
    }

    private fun handleClient(socket: Socket) {
        socket.use { client ->
            try {
                client.soTimeout = 20_000
                val input = BufferedInputStream(client.getInputStream())
                val output = BufferedOutputStream(client.getOutputStream())
                val requestLine = readLine(input, 8192) ?: return
                val pieces = requestLine.trim().split(Regex("\\s+"), limit = 3)
                if (pieces.size < 2) {
                    sendError(output, 400, "Bad Request", "Malformed request line")
                    return
                }
                val method = pieces[0].uppercase(Locale.ROOT)
                val target = pieces[1]
                val headers = HashMap<String, String>()
                var totalHeaderBytes = 0
                while (true) {
                    val line = readLine(input, 8192) ?: break
                    totalHeaderBytes += line.length
                    if (totalHeaderBytes > 32 * 1024) throw IOException("Request headers too large")
                    if (line.isEmpty()) break
                    val colon = line.indexOf(':')
                    if (colon > 0) headers[line.substring(0, colon).trim().lowercase(Locale.ROOT)] =
                        line.substring(colon + 1).trim()
                }
                val path = target.substringBefore('?').substringBefore('#').ifBlank { "/" }
                val range = headers["range"]
                if (method != "GET" && method != "HEAD") {
                    sendError(output, 405, "Method Not Allowed", "Only GET and HEAD are supported")
                    Log.i(TAG, "$method $path -> 405")
                    return
                }
                when {
                    path == "/disc.iso" || path.endsWith(".iso", ignoreCase = true) ||
                        path.contains("selected-disc.iso", ignoreCase = true) -> {
                        val file = File(context.noBackupFilesDir, "tmb/selected-disc.iso")
                        if (!file.isFile || file.length() <= 0L) {
                            sendError(output, 404, "Disc ISO Required", "Select a TMB ISO in the app")
                            Log.w(TAG, "$method $path -> 404 (no staged ISO)")
                        } else {
                            serveIso(output, method, path, range, file)
                        }
                    }
                    else -> {
                        val clean = when (path) { "/", "/index.html" -> "ps2web.html"; else -> path.removePrefix("/") }
                        if (clean !in ASSETS) {
                            sendError(output, 404, "Not Found", "Runtime asset not found")
                            if (path != "/favicon.ico") Log.w(TAG, "$method $path -> 404")
                        } else {
                            val bytes = assetCache.computeIfAbsent(clean) {
                                context.assets.open("www/$clean").use { stream -> stream.readBytes() }
                            }
                            serveBytes(output, method, path, clean, range, bytes)
                        }
                    }
                }
                output.flush()
            } catch (error: Exception) {
                if (running) Log.e(TAG, "HTTP client failed: ${error.message}")
            }
        }
    }

    private fun serveIso(output: BufferedOutputStream, method: String, path: String,
                         range: String?, file: File) {
        val total = file.length()
        val slice = parseRange(range, total)
        if (slice == null) {
            if (range != null) {
                writeHeaders(output, 416, "Range Not Satisfiable", "text/plain; charset=utf-8", 0L,
                    mapOf("Content-Range" to "bytes */$total", "Accept-Ranges" to "bytes"))
                Log.w(TAG, "$method $path range=$range -> 416")
                return
            }
        }
        val start = slice?.first ?: 0L
        val end = slice?.second ?: (total - 1L)
        val partial = slice != null
        val length = end - start + 1L
        val extra = mutableMapOf("Accept-Ranges" to "bytes")
        if (partial) extra["Content-Range"] = "bytes $start-$end/$total"
        writeHeaders(output, if (partial) 206 else 200, if (partial) "Partial Content" else "OK",
            "application/octet-stream", length, extra)
        if (method == "GET") {
            RandomAccessFile(file, "r").use { raf ->
                raf.seek(start)
                val buffer = ByteArray(64 * 1024)
                var remaining = length
                while (remaining > 0L) {
                    val count = raf.read(buffer, 0, minOf(buffer.size.toLong(), remaining).toInt())
                    if (count < 0) throw IOException("Unexpected EOF reading staged ISO")
                    output.write(buffer, 0, count)
                    remaining -= count.toLong()
                }
            }
        }
        Log.i(TAG, "$method $path range=${range ?: "-"} -> ${if (partial) 206 else 200} bytes=$length")
    }

    private fun serveBytes(output: BufferedOutputStream, method: String, path: String,
                           asset: String, range: String?, bytes: ByteArray) {
        val slice = parseRange(range, bytes.size.toLong())
        if (slice == null && range != null) {
            writeHeaders(output, 416, "Range Not Satisfiable", mime(asset), 0L,
                mapOf("Content-Range" to "bytes */${bytes.size}", "Accept-Ranges" to "bytes"))
            Log.w(TAG, "$method $path range=$range -> 416")
            return
        }
        val start = slice?.first?.toInt() ?: 0
        val end = slice?.second?.toInt() ?: (bytes.size - 1)
        val partial = slice != null
        val length = if (bytes.isEmpty()) 0 else end - start + 1
        val extra = mutableMapOf("Accept-Ranges" to "bytes")
        if (partial) extra["Content-Range"] = "bytes $start-$end/${bytes.size}"
        writeHeaders(output, if (partial) 206 else 200, if (partial) "Partial Content" else "OK",
            mime(asset), length.toLong(), extra)
        if (method == "GET" && length > 0) output.write(bytes, start, length)
        Log.i(TAG, "$method /$asset range=${range ?: "-"} -> ${if (partial) 206 else 200} bytes=$length")
    }

    private fun parseRange(header: String?, total: Long): Pair<Long, Long>? {
        if (header == null) return null
        if (!header.startsWith("bytes=") || header.contains(',') || total <= 0L) return null
        val spec = header.removePrefix("bytes=").trim()
        val dash = spec.indexOf('-')
        if (dash < 0) return null
        val left = spec.substring(0, dash).trim()
        val right = spec.substring(dash + 1).trim()
        if (left.isEmpty()) {
            val suffix = right.toLongOrNull() ?: return null
            if (suffix <= 0L) return null
            return (total - suffix).coerceAtLeast(0L) to (total - 1L)
        }
        val start = left.toLongOrNull() ?: return null
        if (start < 0L || start >= total) return null
        val requestedEnd = if (right.isEmpty()) total - 1L else right.toLongOrNull() ?: return null
        if (requestedEnd < start) return null
        return start to requestedEnd.coerceAtMost(total - 1L)
    }

    private fun sendError(output: BufferedOutputStream, status: Int, reason: String, message: String) {
        val body = message.toByteArray(StandardCharsets.UTF_8)
        writeHeaders(output, status, reason, "text/plain; charset=utf-8", body.size.toLong(), emptyMap())
        output.write(body)
        output.flush()
    }

    private fun writeHeaders(output: BufferedOutputStream, status: Int, reason: String,
                             contentType: String, length: Long, extra: Map<String, String>) {
        val headers = linkedMapOf(
            "Content-Type" to contentType,
            "Content-Length" to length.toString(),
            "Cache-Control" to "no-store",
            "Connection" to "close",
            "Cross-Origin-Opener-Policy" to "same-origin",
            "Cross-Origin-Embedder-Policy" to "require-corp",
            "Cross-Origin-Resource-Policy" to "same-origin",
            "Document-Isolation-Policy" to "isolate-and-credentialless"
        )
        headers.putAll(extra)
        output.write("HTTP/1.1 $status $reason\r\n".toByteArray(StandardCharsets.US_ASCII))
        for ((name, value) in headers) {
            output.write("$name: $value\r\n".toByteArray(StandardCharsets.US_ASCII))
        }
        output.write("\r\n".toByteArray(StandardCharsets.US_ASCII))
    }

    private fun readLine(input: InputStream, maxLength: Int): String? {
        val buffer = ByteArrayOutputStream()
        while (buffer.size() <= maxLength) {
            val value = input.read()
            if (value < 0) return if (buffer.size() == 0) null else buffer.toString("US-ASCII")
            if (value == 10) return buffer.toString("US-ASCII")
            if (value != 13) buffer.write(value)
        }
        throw IOException("HTTP request line/header too long")
    }

    private fun mime(path: String): String = when {
        path.endsWith(".html", true) -> "text/html; charset=utf-8"
        path.endsWith(".js", true) -> "application/javascript; charset=utf-8"
        path.endsWith(".wasm", true) -> "application/wasm"
        path.endsWith(".data", true) -> "application/octet-stream"
        path.endsWith(".json", true) -> "application/json; charset=utf-8"
        else -> "application/octet-stream"
    }

    private companion object {
        const val TAG = "TMB_HTTP"
        const val PREFERRED_PORT = 18764
        val ASSETS = setOf("ps2web.html", "ps2web.js", "ps2web.wasm", "ps2web.data")
    }
}
