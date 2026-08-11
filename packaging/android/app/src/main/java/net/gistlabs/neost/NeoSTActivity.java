package net.gistlabs.neost;

import org.libsdl.app.SDLActivity;

/**
 * Activité de NeoST : tout le travail est natif, cette classe ne fait que
 * déclarer l'ordre de chargement des bibliothèques. « SDL2 » d'abord (elle
 * porte le pont JNI), puis « neost » (notre frontend, cf. src/android/
 * main_android.cpp et la branche if(ANDROID) du CMakeLists racine).
 */
public class NeoSTActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "neost" };
    }
}
