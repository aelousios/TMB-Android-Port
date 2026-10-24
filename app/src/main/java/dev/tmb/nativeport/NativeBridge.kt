package dev.tmb.nativeport

internal object NativeBridge {
    init {
        System.loadLibrary("tmb_native")
    }

    external fun surfaceCreated()
    external fun surfaceChanged(width: Int, height: Int)
    external fun renderFrame(deltaSeconds: Float)
    external fun setControl(control: Int, pressed: Boolean)
    external fun getHudText(): String
    external fun inspectIsoFd(fd: Int): String
}
