// GeneralsX @feature android-port 30/07/2026
//
// JVM regression tests for ScriptImporter. ScriptImporter is deliberately
// Android-free (plain java.io / java.nio.file) so these tests run on the host
// JVM via :app:testDebugUnitTest without a device or emulator. The two cases
// pin the contract callers rely on: an empty source stream must be rejected
// (Android 16 blocks ADB writes to the app-owned data dir, so the app itself
// writes the user-selected SAF stream — never silently write a zero-byte
// script), and a non-empty stream must land byte-for-byte at the destination.

package me.generalsx.zh;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

public class ScriptImporterTest {

    @Rule
    public final TemporaryFolder tmp = new TemporaryFolder();

    @Test(expected = IOException.class)
    public void rejectsEmptySource() throws IOException {
        // Empty source must be rejected BEFORE any destination file is created,
        // so a botched SAF read cannot zero out an existing SkirmishScripts.scb.
        File dest = new File(tmp.getRoot(), "Scripts/SkirmishScripts.scb");

        ScriptImporter.copyAndValidate(
                new ByteArrayInputStream(new byte[0]), dest);
    }

    @Test
    public void copiesNonEmptyBytes() throws IOException {
        byte[] payload = "hello-skirmish-script".getBytes("UTF-8");
        File dest = new File(tmp.getRoot(), "Scripts/SkirmishScripts.scb");

        long written = ScriptImporter.copyAndValidate(
                new ByteArrayInputStream(payload), dest);

        assertEquals(payload.length, written);
        assertTrue("destination file must exist", dest.isFile());
        assertTrue("parent directories must be created",
                dest.getParentFile().isDirectory());
        assertArrayEquals(payload, Files.readAllBytes(dest.toPath()));
    }

    @Test
    public void overwritesExistingDestination() throws IOException {
        // Characterizes the safe-replacement contract: a non-empty import must
        // overwrite an existing destination byte-for-byte, going through the
        // same-directory temp + rename path (the destination is never deleted
        // before the rename succeeds). This pins the behaviour callers rely on
        // before the implementation is ported off java.nio.file for API 24.
        byte[] stale = "stale-bytes".getBytes("UTF-8");
        byte[] fresh = "fresh-bytes-here".getBytes("UTF-8");
        File dest = new File(tmp.getRoot(), "Scripts/SkirmishScripts.scb");
        dest.getParentFile().mkdirs();
        Files.write(dest.toPath(), stale);

        long written = ScriptImporter.copyAndValidate(
                new ByteArrayInputStream(fresh), dest);

        assertEquals(fresh.length, written);
        assertTrue("destination file must still exist", dest.isFile());
        assertArrayEquals(fresh, Files.readAllBytes(dest.toPath()));
    }
}
