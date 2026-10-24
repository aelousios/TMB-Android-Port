package org.libsdl.app;

/** SDL-managed Android Activity used only to verify native host packaging and lifecycle. */
public final class TMBRecompActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
