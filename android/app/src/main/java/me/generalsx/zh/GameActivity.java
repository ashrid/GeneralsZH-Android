// GeneralsX @feature android-port 06/07/2026
//
// GameActivity — the Android entry point. Mirrors what iOS gets for free from
// SDL's UIApplicationMain bootstrap: load the native library, hand control to
// SDL_main(). SDL3 ships SDLActivity.java (via the FetchContent'd SDL3 source
// under android-project/app/src/main/java/org/libsdl/app/); the packaging script
// copies it in. We subclass it only to pin the package name + set library hints
// before SDL's nativeInit runs.
//
// On launch: SDLActivity.onCreate -> System.loadLibrary("SDL3") ->
// nativeRunMain("main", "SDL_main", argv) -> dlopen("libmain.so") -> SDL_main().
//
// The native SDL_main lives in GeneralsMD/Code/Main/SDL3Main.cpp (renamed by
// SDL3/SDL_main.h on Android). Everything else — Vulkan surface creation, touch
// routing, lifecycle — is handled by the engine + SDL3, identical to iOS.

package me.generalsx.zh;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    private static final int SCRIPT_IMPORT_REQUEST_CODE = 1001;

    // GeneralsX @feature android-port 30/07/2026 Gate SDL startup until the
    // owner supplies the missing legal skirmish script through Android SAF.
    private final Object scriptImportLock = new Object();
    private boolean scriptImportComplete;
    private boolean scriptImportSucceeded;

    @Override
    protected String[] getLibraries() {
        // Order matters: the engine (libmain.so) dlopens libdxvk_d3d8.so, which
        // pulls libvulkan.so. SDL3 + SDL3_image are the windowing/asset layer.
        // Preloading them here makes them resolvable inside the app's linker
        // namespace before the engine's dlopen() runs (Android API 24+ namespace
        // isolation requires a lib to be registered via loadLibrary before a
        // bare-name dlopen can find it).
        return new String[]{
            "SDL3",
            "SDL3_image",
            "openal",
            "dxvk_d3d9",
            "dxvk_d3d8",
            "main"
        };
    }

    @Override
    protected String getMainFunction() {
        // The symbol nativeRunMain dlsyms inside libmain.so.
        return "SDL_main";
    }

    // GeneralsX @feature android-port 30/07/2026 Android 16 prevents ADB from
    // populating app-owned external storage, so request the script before SDL
    // starts the native engine on its dedicated main thread.
    @Override
    protected void main() {
        File skirmishScript = getSkirmishScript();
        if (skirmishScript == null) {
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    finishStorageUnavailable();
                }
            });
            return;
        }

        if (skirmishScript.isFile() && skirmishScript.length() > 0) {
            super.main();
            return;
        }

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                // GeneralsX @bugfix android-port 30/07/2026 If no activity can
                // resolve ACTION_OPEN_DOCUMENT (stripped ROM or emulator without
                // DocumentsUI), startActivityForResult throws the documented
                // ActivityNotFoundException on the UI thread and onActivityResult
                // is never invoked. The native SDL thread is already blocked on
                // scriptImportLock.wait(); without this guard it would wait
                // forever. Route the failure through finishImport(false) to
                // release the lock, surface the existing close dialog, and skip
                // super.main().
                try {
                    startActivityForResult(intent, SCRIPT_IMPORT_REQUEST_CODE);
                } catch (ActivityNotFoundException e) {
                    finishImport(false);
                }
            }
        });

        synchronized (scriptImportLock) {
            while (!scriptImportComplete) {
                try {
                    scriptImportLock.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            finishImport(false);
                        }
                    });
                    return;
                }
            }
        }

        if (scriptImportSucceeded) {
            super.main();
        }
    }

    // GeneralsX @feature android-port 30/07/2026 Preserve SDL's result
    // handling while importing the user-selected document away from the UI.
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode != SCRIPT_IMPORT_REQUEST_CODE) {
            return;
        }

        Uri source = data == null ? null : data.getData();
        if (resultCode != RESULT_OK || source == null) {
            finishImport(false);
            return;
        }

        importSkirmishScript(source);
    }

    private File getSkirmishScript() {
        // GeneralsX @feature android-port 30/07/2026 Never let File's null
        // parent overload turn an unavailable Android storage root into a
        // relative path that could reach native SDL from the wrong directory.
        File storageRoot = getExternalFilesDir(null);
        if (storageRoot == null) {
            return null;
        }
        return new File(storageRoot,
                "GameData/Data/Scripts/SkirmishScripts.scb");
    }

    private void importSkirmishScript(final Uri source) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                File skirmishScript = getSkirmishScript();
                if (skirmishScript == null) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            finishStorageUnavailable();
                        }
                    });
                    return;
                }

                try (InputStream in = getContentResolver().openInputStream(source)) {
                    if (in == null) {
                        throw new IOException("Unable to open selected script");
                    }
                    ScriptImporter.copyAndValidate(in, skirmishScript);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            finishImport(true);
                        }
                    });
                } catch (Exception e) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            finishImport(false);
                        }
                    });
                }
            }
        }, "SkirmishScriptImport").start();
    }

    // GeneralsX @feature android-port 30/07/2026 Keep native SDL stopped when
    // Android cannot provide this activity's app-owned external storage root.
    private void finishStorageUnavailable() {
        synchronized (scriptImportLock) {
            scriptImportSucceeded = false;
            scriptImportComplete = true;
            scriptImportLock.notifyAll();
        }

        new AlertDialog.Builder(this)
                .setMessage("Game storage is unavailable.")
                .setPositiveButton("Close", (dialog, which) -> finish())
                .setOnCancelListener(dialog -> finish())
                .show();
    }

    private void finishImport(boolean succeeded) {
        synchronized (scriptImportLock) {
            scriptImportSucceeded = succeeded;
            scriptImportComplete = true;
            scriptImportLock.notifyAll();
        }

        if (!succeeded) {
            new AlertDialog.Builder(this)
                    .setMessage("Select the legal SkirmishScripts.scb from your "
                            + "Zero Hour Data/Scripts folder.")
                    .setPositiveButton("Close", (dialog, which) -> finish())
                    .setOnCancelListener(dialog -> finish())
                    .show();
        }
    }
}
