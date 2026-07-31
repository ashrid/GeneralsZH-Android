// GeneralsX @feature android-port 30/07/2026
//
// JVM regression tests for ModImporter. Like ScriptImporter, ModImporter is
// deliberately Android-free (java.io only) so these tests run on the host JVM
// via :app:testDebugUnitTest without a device or emulator. They pin the three
// contracts a future GameActivity SAF caller will rely on:
//
//   - copyFile accepts an empty source (a mod folder may legitimately contain
//     empty placeholder/marker files, unlike a single SkirmishScripts.scb),
//     lands non-empty streams byte-for-byte, creates nested parents, and on any
//     I/O error removes the temp file and leaves an existing destination intact.
//   - resolveSafeDest confines every imported path strictly below the mod root,
//     rejecting absolute, drive-prefixed, traversing, NUL-bearing, and otherwise
//     path-invalid relative paths.
//   - sanitizeModName trims, drops empty/dot/leading-dot names, collapses the
//     filesystem-invalid character set to '_', and caps length at 64.

package me.generalsx.zh;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ModImporterTest {

    @Rule
    public final TemporaryFolder tmp = new TemporaryFolder();

    // ---- copyFile ----

    @Test
    public void copyFileAllowsEmptySource() throws IOException {
        // A mod folder may legitimately contain empty placeholder files, so an
        // empty source must NOT be rejected (this is the deliberate difference
        // from ScriptImporter); it produces a zero-byte destination through the
        // same temp + rename path.
        File dest = new File(tmp.getRoot(), "Mods/MyMod/keep.empty");

        long written = ModImporter.copyFile(
                new ByteArrayInputStream(new byte[0]), dest);

        assertEquals("empty source writes zero bytes", 0L, written);
        assertTrue("destination file must exist", dest.isFile());
        assertEquals("destination must be zero bytes",
                0, Files.readAllBytes(dest.toPath()).length);
        assertTrue("parent directories must be created",
                dest.getParentFile().isDirectory());
    }

    @Test
    public void copyFileWritesBytesExactly() throws IOException {
        byte[] payload = "mod-payload-bytes-123".getBytes("UTF-8");
        File dest = new File(tmp.getRoot(), "Mods/MyMod/Data/Art/blob.w3d");

        long written = ModImporter.copyFile(
                new ByteArrayInputStream(payload), dest);

        assertEquals(payload.length, written);
        assertArrayEquals(payload, Files.readAllBytes(dest.toPath()));
    }

    @Test
    public void copyFileOverwritesExistingDestination() throws IOException {
        // Safe-replacement contract: a successful copy overwrites an existing
        // destination byte-for-byte via the same-directory temp + rename path
        // (the existing file is never deleted before the rename succeeds).
        byte[] stale = "stale-mod-bytes".getBytes("UTF-8");
        byte[] fresh = "fresh-mod-bytes-here".getBytes("UTF-8");
        File dest = new File(tmp.getRoot(), "Mods/MyMod/Data/ini.txt");
        dest.getParentFile().mkdirs();
        Files.write(dest.toPath(), stale);

        long written = ModImporter.copyFile(
                new ByteArrayInputStream(fresh), dest);

        assertEquals(fresh.length, written);
        assertArrayEquals(fresh, Files.readAllBytes(dest.toPath()));
    }

    @Test
    public void copyFilePreservesDestinationOnReadFailure() throws IOException {
        // On any I/O error the temp file is removed and an existing destination
        // is preserved byte-for-byte (the rename is never reached), with no
        // leftover temp file next to the destination.
        byte[] existing = "untouched".getBytes("UTF-8");
        File dest = new File(tmp.getRoot(), "Mods/MyMod/Data/keep.txt");
        dest.getParentFile().mkdirs();
        Files.write(dest.toPath(), existing);

        InputStream broken = new InputStream() {
            @Override
            public int read() throws IOException {
                throw new IOException("simulated upstream read failure");
            }
        };

        try {
            ModImporter.copyFile(broken, dest);
            fail("expected IOException for a broken source stream");
        } catch (IOException expected) {
            // expected — a read failure must surface, not be swallowed
        }

        assertArrayEquals("existing destination must be preserved on error",
                existing, Files.readAllBytes(dest.toPath()));
        File[] leftover = dest.getParentFile().listFiles();
        assertNotNull(leftover);
        assertEquals("no leftover temp file in the destination directory",
                1, leftover.length);
    }

    // ---- resolveSafeDest ----

    @Test
    public void resolveSafeDestAcceptsNormalNestedPath() throws IOException {
        File root = new File(tmp.getRoot(), "Mods/MyMod");

        File resolved = ModImporter.resolveSafeDest(root, "Data/Maps/foo.ini");

        File expected = new File(root.getAbsoluteFile(), "Data/Maps/foo.ini")
                .getCanonicalFile();
        assertEquals(expected, resolved);
    }

    @Test
    public void resolveSafeDestCreatesNoFiles() throws IOException {
        // resolveSafeDest is a pure path resolver: it must not create the mod
        // root or any parent directory as a side effect (the caller decides when
        // to materialize the tree via copyFile).
        File root = new File(tmp.getRoot(), "Mods/MyMod");

        ModImporter.resolveSafeDest(root, "Data/Maps/foo.ini");

        assertFalse("resolveSafeDest must not create the mod root",
                root.exists());
    }

    @Test
    public void resolveSafeDestRejectsUnsafePaths() {
        File root = new File(tmp.getRoot(), "Mods/MyMod");
        // Every entry below must be refused: absolute, drive-prefixed,
        // traversing, NUL-bearing, empty/whitespace, and path-invalid.
        String[] bad = {
            "../escape.ini",           // parent traversal
            "Data/../../escape.ini",   // traversal after a clean segment
            "/etc/passwd",             // absolute Unix path
            "C:\\Users\\x\\evil.ini",  // Windows drive + backslash
            "C:evil.ini",              // drive prefix without separator
            "Data//double.ini",        // empty segment
            "Data/evil\u0000.ini",     // NUL byte
            "",                        // empty rel path
            "   ",                     // whitespace-only rel path
            "Data/rocks:stream.ini",   // colon (path-invalid segment)
            "Data/que?stion.ini",      // wildcard character
            "Data/back\\slash.ini",    // backslash separator
            "Data/less<han.ini",       // angle bracket
        };
        for (String rel : bad) {
            try {
                ModImporter.resolveSafeDest(root, rel);
                fail("expected rejection for unsafe relPath: <" + rel + ">");
            } catch (IOException expected) {
                // expected — every entry above must be refused
            }
        }
    }

    // ---- sanitizeModName ----

    @Test
    public void sanitizeModNameTrimsAndPreservesCleanName() {
        assertEquals("My Cool Mod", ModImporter.sanitizeModName("   My Cool Mod   "));
    }

    @Test
    public void sanitizeModNameReplacesInvalidCharacters() {
        // The filesystem-invalid set / \ : * ? " < > | collapses to '_'.
        assertEquals("My_Mod_x_y_z",
                ModImporter.sanitizeModName("My/Mod\\x:y*z"));
        assertEquals("a_b_c_d_e_f",
                ModImporter.sanitizeModName("a?b\"c<d>e|f"));
    }

    @Test
    public void sanitizeModNameCapsAt64Characters() {
        String out = ModImporter.sanitizeModName(repeat('A', 100));
        assertEquals("name must be capped at 64 characters", 64, out.length());
    }

    @Test
    public void sanitizeModNameRejectsInvalidNames() {
        // Empty, dot-only, and leading-dot names are refused outright (they
        // would yield an invisible or traversal-bearing directory).
        String[] bad = { null, "", "   ", ".", "..", "....", ".hidden" };
        for (String raw : bad) {
            try {
                ModImporter.sanitizeModName(raw);
                fail("expected rejection for invalid mod name: <" + raw + ">");
            } catch (IllegalArgumentException expected) {
                // expected — empty, dot, and leading-dot names must be refused
            }
        }
    }

    // ---- helpers ----

    private static String repeat(char c, int n) {
        StringBuilder sb = new StringBuilder(n);
        for (int i = 0; i < n; i++) {
            sb.append(c);
        }
        return sb.toString();
    }
}
