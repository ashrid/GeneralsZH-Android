// GeneralsX @feature android-port 30/07/2026
//
// ModImporter — path-safe helpers for importing a user-selected mod folder
// (a Storage Access Framework document tree) into the app-owned game-data
// directory (getExternalFilesDir(null)/GameData/Mods/<Name>/). Android 16
// blocks ADB writes to the app-owned external dir, but the app itself can
// write there once the user has selected a document tree via SAF; a later
// task walks the tree with DocumentsContract and calls these helpers per
// file. Kept strictly Android-free (java.io only) so it is unit-testable on
// the host JVM. The caller owns every InputStream and is responsible for
// closing it (these methods do NOT close it); the caller typically obtains
// one from a ContentResolver and closes it after each call returns.

package me.generalsx.zh;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public final class ModImporter {

    private static final int NAME_MAX = 64;

    private ModImporter() {
    }

    /**
     * Copy {@code in} into {@code dest}, writing atomically through a
     * same-directory temp file and allowing an empty source (a mod folder may
     * legitimately contain empty placeholder/marker files).
     *
     * @param in   the user-selected mod-file stream (not closed by this method)
     * @param dest destination file; missing parent directories are created
     * @return number of bytes written (0 for an empty source)
     * @throws IOException on any I/O failure; the temp file is removed and an
     *     existing destination is left untouched (the rename is the last step)
     */
    // GeneralsX @feature android-port 30/07/2026 Unlike ScriptImporter, a mod
    // folder may legitimately contain empty placeholder files, so an empty
    // source is allowed: the temp is zero bytes and rename puts it into place.
    // temp and dest share the same directory => same filesystem => renameTo is
    // the POSIX rename(2) that replaces an existing destination without first
    // deleting it, so a failed rename cannot truncate an existing file.
    public static long copyFile(InputStream in, File dest) throws IOException {
        if (in == null) {
            throw new IOException("input stream must not be null");
        }
        if (dest == null) {
            throw new IOException("destination must not be null");
        }

        File parent = dest.getAbsoluteFile().getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        File temp = File.createTempFile("mod-import", ".tmp", parent);
        long total = 0;
        try {
            byte[] buffer = new byte[8192];
            // Only the FileOutputStream opened here is closed by this block; the
            // caller-owned `in` stream is intentionally left open.
            try (FileOutputStream out = new FileOutputStream(temp)) {
                int n;
                while ((n = in.read(buffer)) != -1) {
                    out.write(buffer, 0, n);
                    total += n;
                }
            }
            if (!temp.renameTo(dest)) {
                throw new IOException(
                        "Failed to move imported mod file into place: "
                                + temp + " -> " + dest);
            }
            return total;
        } finally {
            // On any failure path (read error, write error, or rename failure)
            // the temp still exists and must be removed so a failed import
            // leaves no partial temp next to a preserved destination. On
            // success temp was consumed by the rename, so this is a no-op.
            if (temp.exists()) {
                temp.delete();
            }
        }
    }

    /**
     * Resolve {@code relPath} against {@code modRoot}, refusing any path that
     * is absolute, drive-prefixed, traverses above the root, contains NUL or
     * other filesystem-invalid characters, and verifying the resolved
     * canonical destination stays strictly below the canonical root.
     *
     * @param modRoot the mod directory the relative path is confined to
     * @param relPath a forward-slash relative path under {@code modRoot}
     * @return the resolved canonical destination file (no files are created)
     * @throws IOException if {@code relPath} is unsafe or escapes the root
     */
    // GeneralsX @feature android-port 30/07/2026 SAF document trees are
    // untrusted input; resolveSafeDest rejects every path that could write
    // outside the chosen mod root (absolute, drive-prefixed, parent traversal,
    // NUL, empty/path-invalid segments) and re-checks the canonicalized result
    // so a symlinked segment cannot escape. It is a pure resolver: no mkdirs.
    public static File resolveSafeDest(File modRoot, String relPath)
            throws IOException {
        if (relPath == null) {
            throw new IOException("relPath must not be null");
        }
        if (relPath.indexOf('\u0000') >= 0) {
            throw new IOException("relPath must not contain NUL: " + relPath);
        }
        String trimmed = relPath.trim();
        if (trimmed.isEmpty()) {
            throw new IOException("relPath must not be empty");
        }
        if (trimmed.charAt(0) == '/' || trimmed.charAt(0) == '\\') {
            throw new IOException("relPath must not be absolute: " + relPath);
        }
        if (isDrivePrefix(trimmed)) {
            throw new IOException(
                    "relPath must not start with a drive prefix: " + relPath);
        }

        File resolved = modRoot.getAbsoluteFile();
        for (String seg : trimmed.split("/")) {
            if (seg.isEmpty()) {
                throw new IOException(
                        "relPath must not contain empty segments: " + relPath);
            }
            if ("..".equals(seg)) {
                throw new IOException(
                        "relPath must not contain parent traversal: " + relPath);
            }
            if (!isValidSegment(seg)) {
                throw new IOException(
                        "relPath contains an invalid segment: " + relPath);
            }
            resolved = new File(resolved, seg);
        }

        File canonRoot = modRoot.getAbsoluteFile().getCanonicalFile();
        File canonDest = resolved.getCanonicalFile();
        String rootPrefix = canonRoot.getAbsolutePath() + File.separator;
        if (!canonDest.getAbsolutePath().startsWith(rootPrefix)) {
            throw new IOException("relPath escapes the mod root: " + relPath);
        }
        return canonDest;
    }

    /**
     * Normalize a user-supplied mod folder name: trim, reject empty/dot/
     * leading-dot names, replace every filesystem-invalid character
     * ({@code / \ : * ? " < > |}) with {@code _}, and cap at 64 characters.
     *
     * @param raw the raw folder name (e.g. a SAF display name)
     * @return the sanitized name, safe to use as a single directory segment
     * @throws IllegalArgumentException if {@code raw} is null, empty, or a
     *     dot/leading-dot name
     */
    // GeneralsX @feature android-port 30/07/2026 The mod folder name becomes a
    // single filesystem segment; sanitize before mkdir so a hostile or careless
    // display name cannot inject separators, traversal, or hidden folders.
    public static String sanitizeModName(String raw) {
        if (raw == null) {
            throw new IllegalArgumentException("mod name must not be null");
        }
        String trimmed = raw.trim();
        if (trimmed.isEmpty() || trimmed.charAt(0) == '.') {
            throw new IllegalArgumentException(
                    "mod name must not be empty, dot, or start with a dot: <"
                            + raw + ">");
        }
        StringBuilder out = new StringBuilder(trimmed.length());
        for (int i = 0; i < trimmed.length(); i++) {
            char c = trimmed.charAt(i);
            if (c == '/' || c == '\\' || c == ':' || c == '*'
                    || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                out.append('_');
            } else {
                out.append(c);
            }
        }
        String result = out.toString();
        if (result.length() > NAME_MAX) {
            result = result.substring(0, NAME_MAX);
        }
        return result;
    }

    /** True iff {@code s} starts with a Windows-style drive prefix ({@code <letter>:}). */
    private static boolean isDrivePrefix(String s) {
        return s.length() >= 2
                && Character.isLetter(s.charAt(0))
                && s.charAt(1) == ':';
    }

    /**
     * True iff {@code seg} is a single safe path segment: no separator, no
     * drive colon, no wildcard/quote/angle/pipe, and no control character.
     */
    private static boolean isValidSegment(String seg) {
        for (int i = 0; i < seg.length(); i++) {
            char c = seg.charAt(i);
            if (c < 0x20) {
                return false;
            }
            switch (c) {
            case '\\': case ':': case '*': case '?':
            case '"': case '<': case '>': case '|':
                return false;
            default:
                break;
            }
        }
        return true;
    }
}
