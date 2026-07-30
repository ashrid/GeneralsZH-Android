// GeneralsX @feature android-port 30/07/2026
//
// ScriptImporter — copies a user-selected script stream (e.g. a Storage Access
// Framework InputStream for SkirmishScripts.scb) into the app-owned game-data
// directory (getExternalFilesDir(null)/GameData/Data/Scripts/). Android 16
// blocks ADB writes to the app-owned external dir, but the app itself can write
// there once the user has selected a document via SAF.
//
// Kept strictly Android-free (java.io only) so it is unit-testable on the
// host JVM. The caller owns the InputStream and is responsible
// for closing it (this method does NOT close it); the caller typically obtains
// it from a ContentResolver and closes it after this call returns.

package me.generalsx.zh;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.SequenceInputStream;

public final class ScriptImporter {

    private ScriptImporter() {
    }

    /**
     * Copy {@code in} into {@code dest}, refusing empty input and writing
     * atomically via a same-directory temp file.
     *
     * @param in   the user-selected script stream (not closed by this method)
     * @param dest destination file; missing parent directories are created
     * @return number of bytes written
     * @throws IOException if {@code in} is empty, or on any I/O failure
     */
    // GeneralsX @feature android-port 30/07/2026 Reject an empty source up front
    // so a botched SAF read can never zero out an existing SkirmishScripts.scb;
    // write through a temp file in the same directory then atomically move it
    // into place (same filesystem => File.renameTo performs the POSIX rename
    // that replaces an existing destination without first deleting it).
    public static long copyAndValidate(InputStream in, File dest) throws IOException {
        // Peek one byte to detect an empty stream before touching the filesystem.
        byte[] first = new byte[1];
        if (in.read(first) == -1) {
            throw new IOException(
                    "Source script stream is empty; refusing to write " + dest);
        }

        File parent = dest.getAbsoluteFile().getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        File temp = File.createTempFile("script-import", ".tmp", parent);
        try {
            // Replay the peeked byte ahead of the remainder of the stream. The
            // SequenceInputStream is intentionally NOT closed: closing it would
            // close the caller's `in`, and the ByteArrayInputStream holds no
            // resource, so there is nothing to release here.
            InputStream seq = new SequenceInputStream(
                    new ByteArrayInputStream(first), in);
            // GeneralsX @bugfix android-port 30/07/2026 Drop java.nio.file
            // (Files.copy/move, File.toPath, StandardCopyOption), which Android
            // lint flags as API 26 on a minSdk 24 target. Buffer the stream into
            // the same-directory temp via FileOutputStream (counting bytes), then
            // replace the destination with File.renameTo. temp and dest share the
            // same directory => same filesystem => renameTo is the POSIX rename(2)
            // that replaces an existing destination without first deleting it, so a
            // failed rename cannot truncate an existing script.
            byte[] buffer = new byte[8192];
            long total = 0;
            // Only the FileOutputStream opened here is closed by this block; the
            // caller-owned seq/in streams are intentionally left open.
            try (FileOutputStream out = new FileOutputStream(temp)) {
                int n;
                while ((n = seq.read(buffer)) != -1) {
                    out.write(buffer, 0, n);
                    total += n;
                }
            }
            if (!temp.renameTo(dest)) {
                throw new IOException(
                        "Failed to move imported script into place: " + temp + " -> " + dest);
            }
            return total;
        } catch (IOException e) {
            temp.delete();
            throw e;
        }
    }
}
