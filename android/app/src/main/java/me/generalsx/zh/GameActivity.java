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
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.atomic.AtomicBoolean;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    private static final int SCRIPT_IMPORT_REQUEST_CODE = 1001;
    private static final int MOD_IMPORT_REQUEST_CODE = 1002;

    // GeneralsX @feature android-port 30/07/2026 Gate SDL startup until the
    // owner supplies the missing legal skirmish script through Android SAF.
    private final Object scriptImportLock = new Object();
    private boolean scriptImportComplete;
    private boolean scriptImportSucceeded;

    // GeneralsX @feature android-port 30/07/2026 One-shot completion flag polled
    // by the native Mods picker (via JNI) after it triggers
    // requestModFolderImport(). Set true only once a full tree copy succeeds and
    // consumed/cleared atomically by consumeModImported(). Deliberately decoupled
    // from the startup script gate: the game is already running when a mod is
    // imported, so this flag never participates in the SDL startup lock.
    private final AtomicBoolean modImported = new AtomicBoolean(false);

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

        if (requestCode == SCRIPT_IMPORT_REQUEST_CODE) {
            Uri source = data == null ? null : data.getData();
            if (resultCode != RESULT_OK || source == null) {
                finishImport(false);
                return;
            }
            importSkirmishScript(source);
            return;
        }

        if (requestCode == MOD_IMPORT_REQUEST_CODE) {
            // GeneralsX @tweak android-port 30/07/2026 TEMPORARY [MOD-IMPORT]
            // logcat probes isolating why SAF mod-folder import creates no
            // destination directory. Removed after the next device capture run.
            Uri tree = data == null ? null : data.getData();
            Log.i("GeneralsX", "[MOD-IMPORT] onActivityResult tree=" + tree
                    + " resultCode=" + resultCode);
            // GeneralsX @feature android-port 30/07/2026 A cancelled mod picker
            // or a null tree URI must leave the already-running game untouched:
            // no dialog, no success flag, and no SDL/script-lock interaction.
            if (resultCode != RESULT_OK || tree == null) {
                Log.i("GeneralsX", "[MOD-IMPORT] onActivityResult early-return"
                        + " (cancelled or null tree)");
                return;
            }
            importModFolder(tree);
        }
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

    // GeneralsX @feature android-port 30/07/2026 Native Mods picker entry point:
    // hop to the UI thread (SAF intents must be started from an Activity on the
    // main thread) and ask the user to pick a mod folder. The native side polls
    // consumeModImported() to learn when the copy finishes. Runs while the engine
    // is live, so it never touches the startup scriptImportLock and never
    // starts/stops SDL; a missing picker surfaces an error instead of hanging.
    public void requestModFolderImport() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                try {
                    startActivityForResult(intent, MOD_IMPORT_REQUEST_CODE);
                } catch (ActivityNotFoundException e) {
                    showModImportError(
                            "No file picker is available to choose a mod folder.");
                }
            }
        });
    }

    // GeneralsX @feature android-port 30/07/2026 Polled by the native Mods
    // picker after requestModFolderImport(): returns whether a mod tree finished
    // importing and clears the flag atomically so each success is consumed once.
    public boolean consumeModImported() {
        return modImported.getAndSet(false);
    }

    // GeneralsX @feature android-port 30/07/2026 Walk the selected SAF tree on a
    // dedicated background thread (copies block, so never the UI thread). On full
    // success the completion flag is set; any failure surfaces an existing-style
    // dialog on the UI thread. The game keeps running either way.
    private void importModFolder(final Uri treeUri) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    copyModTree(treeUri);
                    modImported.set(true);
                    Log.i("GeneralsX", "[MOD-IMPORT] success flag set"
                            + " (modImported=true)");
                } catch (Exception e) {
                    Log.e("GeneralsX", "[MOD-IMPORT] caught exception class="
                            + e.getClass().getName()
                            + " message=" + e.getMessage());
                    final String message = e.getMessage() == null
                            ? "Mod folder import failed." : e.getMessage();
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            showModImportError(message);
                        }
                    });
                }
            }
        }, "ModFolderImport").start();
    }

    private void copyModTree(Uri treeUri) throws IOException {
        File storageRoot = getExternalFilesDir(null);
        if (storageRoot == null) {
            Log.e("GeneralsX", "[MOD-IMPORT] storageRoot null"
                    + " (getExternalFilesDir unavailable)");
            throw new IOException("App storage is unavailable.");
        }
        File modsRoot = new File(storageRoot, "GameData/Mods");
        Log.i("GeneralsX", "[MOD-IMPORT] copyModTree modsRoot=" + modsRoot
                + " modsRootExists=" + modsRoot.exists());

        ContentResolver resolver = getContentResolver();
        String displayName = readRootDisplayName(treeUri, resolver);
        String modName = ModImporter.sanitizeModName(displayName);
        File destination = new File(modsRoot, modName);
        Log.i("GeneralsX", "[MOD-IMPORT] sanitized modName=\"" + modName
                + "\" destination=" + destination);

        // GeneralsX @feature android-port 30/07/2026 Re-import replaces only
        // this single destination tree, and only after the canonical containment
        // check confirms the destination is strictly inside the app-owned Mods
        // root — so a symlinked segment in the sanitized name cannot push the
        // recursive delete outside it.
        replaceModTree(modsRoot, destination);
        if (!destination.exists() && !destination.mkdirs()) {
            Log.e("GeneralsX", "[MOD-IMPORT] mkdirs FAILED destination="
                    + destination + " modsRootExists=" + modsRoot.exists());
            throw new IOException("Unable to create mod folder: " + destination);
        }
        Log.i("GeneralsX", "[MOD-IMPORT] destination ready exists="
                + destination.exists() + " destination=" + destination);

        walkAndCopy(treeUri, DocumentsContract.getTreeDocumentId(treeUri),
                "", destination, resolver);
    }

    private String readRootDisplayName(Uri treeUri, ContentResolver resolver)
            throws IOException {
        Uri rootDocUri = DocumentsContract.buildDocumentUriUsingTree(
                treeUri, DocumentsContract.getTreeDocumentId(treeUri));
        Cursor cursor = resolver.query(rootDocUri,
                new String[]{Document.COLUMN_DISPLAY_NAME},
                null, null, null);
        try {
            if (cursor == null || !cursor.moveToFirst()) {
                Log.e("GeneralsX", "[MOD-IMPORT] displayName cursor "
                        + (cursor == null ? "null" : "empty"));
                throw new IOException(
                        "Unable to read the selected mod folder name.");
            }
            String displayName = cursor.getString(0);
            Log.i("GeneralsX", "[MOD-IMPORT] root displayName=\""
                    + displayName + "\"");
            return displayName;
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
    }

    // GeneralsX @feature android-port 30/07/2026 Recursive tree walk using only
    // the API-21 DocumentsContract tree APIs and the classic (non-Bundle)
    // ContentResolver.query signature. Every Cursor and InputStream is closed on
    // every path; file destinations are confined via resolveSafeDest and copied
    // byte-for-byte via ModImporter.copyFile.
    private void walkAndCopy(Uri treeUri, String parentDocId, String relPrefix,
            File destination, ContentResolver resolver) throws IOException {
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                treeUri, parentDocId);
        Cursor cursor = resolver.query(childrenUri,
                new String[]{
                        Document.COLUMN_DOCUMENT_ID,
                        Document.COLUMN_DISPLAY_NAME,
                        Document.COLUMN_MIME_TYPE
                },
                null, null, null);
        try {
            if (cursor == null) {
                Log.e("GeneralsX", "[MOD-IMPORT] children cursor null parent="
                        + parentDocId);
                throw new IOException("Unable to read mod folder contents.");
            }
            Log.i("GeneralsX", "[MOD-IMPORT] walkAndCopy childCount="
                    + cursor.getCount() + " relPrefix=\"" + relPrefix + "\"");
            while (cursor.moveToNext()) {
                String docId = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                String childRel = relPrefix.isEmpty()
                        ? name : relPrefix + "/" + name;
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    walkAndCopy(treeUri, docId, childRel, destination, resolver);
                } else {
                    copyModFile(treeUri, docId, childRel, destination, resolver);
                }
            }
        } finally {
            if (cursor != null) {
                cursor.close();
            }
        }
    }

    private void copyModFile(Uri treeUri, String docId, String childRel,
            File destination, ContentResolver resolver) throws IOException {
        Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
        File dest = ModImporter.resolveSafeDest(destination, childRel);
        InputStream in = resolver.openInputStream(fileUri);
        if (in == null) {
            Log.e("GeneralsX", "[MOD-IMPORT] openInputStream null rel=\""
                    + childRel + "\"");
            throw new IOException("Unable to open mod file: " + childRel);
        }
        try {
            ModImporter.copyFile(in, dest);
        } finally {
            in.close();
        }
        Log.i("GeneralsX", "[MOD-IMPORT] copied rel=\"" + childRel
                + "\" size=" + dest.length() + " dest=" + dest);
    }

    private void replaceModTree(File modsRoot, File destination)
            throws IOException {
        if (!destination.exists()) {
            return;
        }
        File canonMods = modsRoot.getAbsoluteFile().getCanonicalFile();
        File canonDest = destination.getAbsoluteFile().getCanonicalFile();
        String rootPrefix = canonMods.getAbsolutePath() + File.separator;
        if (!canonDest.getAbsolutePath().startsWith(rootPrefix)) {
            throw new IOException(
                    "Refusing to replace a folder outside the app Mods root.");
        }
        deleteRecursively(canonDest);
    }

    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        file.delete();
    }

    private void showModImportError(String message) {
        new AlertDialog.Builder(this)
                .setMessage(message)
                .setPositiveButton("Close", null)
                .show();
    }
}
