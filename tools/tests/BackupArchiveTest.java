package com.halo.decomp;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.channels.OverlappingFileLockException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/** Standalone regression checks of the backup archive; no phone required. */
public final class BackupArchiveTest {
    private interface Case {
        void run() throws Exception;
    }

    private static final String HASH_A = "0123456789AB";
    private static final String HASH_B = "00000000CDEF";
    private static final String HASH_C = "5555AAAA5555";
    private static final String GOOD_MARKER =
        "format=openh1-backup\nversion=1\nbuild=7\nmade=2026-10-22T10:00:00+00:00\nprofiles=0\nsettings=false\n"
        + "touch-layout=false\nentries=0\n";

    private static File temp;

    private static void check(boolean condition, String message) {
        if (!condition)
            throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        temp = Files.createTempDirectory("backup-archive-test").toFile();
        try {
            run("round trip", BackupArchiveTest::roundTrip);
            run("the marker", BackupArchiveTest::marker);
            run("entry names", BackupArchiveTest::names);
            run("sizes and caps", BackupArchiveTest::sizes);
            run("truncated and incomplete archives", BackupArchiveTest::truncated);
            run("recover", BackupArchiveTest::recover);
            run("the lock", BackupArchiveTest::lock);
            run("damaged names and markers", BackupArchiveTest::damaged);
            run("extract choices", BackupArchiveTest::choices);
            run("skipped profile folders", BackupArchiveTest::skipped);
            run("a swap that fails after a profile moved", BackupArchiveTest::swapFailsLate);
            run("the profiles a restore replaces", BackupArchiveTest::replaced);
            run("the lock seen from outside", BackupArchiveTest::locked);
        } finally {
            delete(temp);
        }
        System.out.println("BackupArchiveTest: all cases passed");
    }

    private static void run(String name, Case body) throws Exception {
        body.run();
        System.out.println("ok " + name);
    }

    // ---- helpers

    private static int folders;

    private static File folder() throws IOException {
        File f = new File(temp, "d" + (folders++));
        if (!f.mkdirs())
            throw new IOException("mkdirs " + f);
        return f;
    }

    private static void delete(File f) {
        File[] children = f.listFiles();
        if (children != null)
            for (File c : children)
                delete(c);
        f.delete();
    }

    private static void put(File root, String path, byte[] bytes) throws IOException {
        File f = new File(root, path);
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), bytes);
    }

    private static byte[] utf16(String s) {
        return s.getBytes(StandardCharsets.UTF_16LE);
    }

    private static byte[] pattern(int length, int seed) {
        byte[] b = new byte[length];
        for (int i = 0; i < length; i++)
            b[i] = (byte) (i * 31 + seed);
        return b;
    }

    private static byte[] text(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    /** A marker for archives that are refused before the entry count is looked at. */
    private static String marker(int profiles, boolean settings, boolean layout) {
        return marker(profiles, settings, layout, 0);
    }

    private static String marker(int profiles, boolean settings, boolean layout, int entries) {
        return "format=openh1-backup\nversion=1\nbuild=7\nmade=2026-10-22T10:00:00+00:00\nprofiles=" + profiles
            + "\nsettings=" + settings + "\ntouch-layout=" + layout + "\nentries=" + entries + "\n";
    }

    /** A marker with the favorites line that the writer puts after settings. */
    private static String marker(int profiles, boolean settings, boolean favorites, boolean layout, int entries) {
        return marker(profiles, settings, layout, entries).replace("\ntouch-layout=",
            "\nfavorites=" + favorites + "\ntouch-layout=");
    }

    /** The text of an archive's first entry, the marker. */
    private static String markerOf(byte[] archive) throws IOException {
        try (ZipInputStream z = new ZipInputStream(new ByteArrayInputStream(archive))) {
            ZipEntry first = z.getNextEntry();
            check(first != null && "openh1-backup.properties".equals(first.getName()),
                "the first entry must be the marker");
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int n;
            while ((n = z.read(buffer)) > 0)
                bytes.write(buffer, 0, n);
            return new String(bytes.toByteArray(), StandardCharsets.ISO_8859_1);
        }
    }

    /** Whether the archive has an entry of that name. */
    private static boolean holds(byte[] archive, String name) throws IOException {
        try (ZipInputStream z = new ZipInputStream(new ByteArrayInputStream(archive))) {
            ZipEntry entry;
            while ((entry = z.getNextEntry()) != null)
                if (name.equals(entry.getName()))
                    return true;
        }
        return false;
    }

    /** Sets the last character of every occurrence of an ASCII name in the zip bytes to byte 0xFF. */
    private static byte[] poisoned(byte[] zip, String name) {
        byte[] a = text(name);
        for (int i = 0; i + a.length <= zip.length; i++)
            if (Arrays.equals(Arrays.copyOfRange(zip, i, i + a.length), a))
                zip[i + a.length - 1] = (byte) 0xFF;
        return zip;
    }

    /** A zip of the given names and contents, in order. */
    private static byte[] zip(Object... pairs) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(bytes)) {
            for (int i = 0; i < pairs.length; i += 2) {
                z.putNextEntry(new ZipEntry((String) pairs[i]));
                z.write((byte[]) pairs[i + 1]);
                z.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    /** Renames an entry inside the zip bytes, local header and directory alike; the names have equal length. */
    private static byte[] renamed(byte[] zip, String from, String to) {
        byte[] a = text(from);
        byte[] b = text(to);
        for (int i = 0; i + a.length <= zip.length; i++)
            if (Arrays.equals(Arrays.copyOfRange(zip, i, i + a.length), a))
                System.arraycopy(b, 0, zip, i, b.length);
        return zip;
    }

    private static String profilePath(String hash, String file) {
        return "save/u/UDATA/" + hash + "/" + file;
    }

    private static void refused(byte[] archive, String mustMention, String what) throws Exception {
        try {
            BackupArchive.scan(new ByteArrayInputStream(archive));
        } catch (IOException e) {
            check(e.getMessage() != null && e.getMessage().contains(mustMention),
                what + ": the message must mention " + mustMention + " but says: " + e.getMessage());
            return;
        }
        throw new AssertionError(what + " must be refused");
    }

    private static File dataRootWithTwoProfiles() throws IOException {
        File root = folder();
        put(root, profilePath(HASH_A, "SaveMeta.xbx"), utf16("Alpha"));
        put(root, profilePath(HASH_A, "blam.sav"), pattern(1024, 1));
        put(root, profilePath(HASH_A, "savegame.bin"), new byte[0x1400000]);
        put(root, profilePath(HASH_A, "notes.txt"), text("not saved"));
        put(root, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Beéta\0\0"));
        put(root, profilePath(HASH_B, "blam.lst"), pattern(2048, 2));
        put(root, "config.toml", text("[video]\nwidth = 1280\n"));
        put(root, "hardware_id.txt", text("device"));
        put(root, "join_link.txt", text("link"));
        put(root, "maps/ui.map", pattern(100, 3));
        put(root, "save/z/lastprof.txt", text("x"));
        return root;
    }

    private static byte[] read(File f) throws IOException {
        return Files.readAllBytes(f.toPath());
    }

    // ---- cases

    private static void roundTrip() throws Exception {
        File root = dataRootWithTwoProfiles();
        put(root, "favorites.txt", text("192.168.1.20:2302\n10.0.0.5:2303\n"));
        put(root, profilePath(HASH_A, "friends.txt"), text("key 00ff\nfriend 0a0b\n"));
        String layout = "sensitivity=2.0\nbutton0=1,2,3\n";
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.Summary written = BackupArchive.write(out, root, layout, 42, new Date(0));
        check(written.profiles == 2 && written.settings && written.favorites && written.touchLayout
            && written.build == 42, "write must report two profiles, the settings, the favorites and the layout");
        byte[] archive = out.toByteArray();
        check(archive.length < 200000, "the zeros of savegame.bin must deflate to almost nothing");
        check(markerOf(archive).contains("\nsettings=true\nfavorites=true\n"),
            "the marker must say favorites=true after settings: " + markerOf(archive));
        check(markerOf(archive).contains("\nentries=9\n"),
            "the marker must count favorites.txt and friends.txt as entries");

        BackupArchive.Summary scanned = BackupArchive.scan(new ByteArrayInputStream(archive));
        check(scanned.profiles == 2 && scanned.profileNames.equals(Arrays.asList("Beéta", "Alpha")),
            "scan must list the profile names in archive order: " + scanned.profileNames);
        check(scanned.settings && scanned.favorites && scanned.touchLayout && scanned.build == 42
            && scanned.made.length() > 10, "scan must read the marker's fields");
        check(layout.equals(scanned.layoutText), "scan must hand back the layout text");
        long files = 10 + 1024 + 0x1400000 + 2 * 5 + 2 * 5 + 2048 + read(new File(root, "config.toml")).length
            + read(new File(root, "favorites.txt")).length
            + read(new File(root, profilePath(HASH_A, "friends.txt"))).length + text(layout).length;
        // the name files are 5 and 4 characters of UTF-16LE: 10 bytes, and 10 with the two NULs
        long marked = scanned.bytes - files;
        check(marked > 0 && marked < 4096, "scan must count every inflated byte, marker included: " + marked);
        check(scanned.bytes == written.bytes, "write and scan must agree on the inflated bytes");

        File staging = new File(folder(), "stage");
        BackupArchive.Summary extracted = BackupArchive.extract(new ByteArrayInputStream(archive), staging,
            true, true, true);
        check(layout.equals(extracted.layoutText), "extract must hand back the layout text");
        String[][] same = {
            {profilePath(HASH_A, "SaveMeta.xbx")}, {profilePath(HASH_A, "blam.sav")},
            {profilePath(HASH_A, "savegame.bin")}, {profilePath(HASH_A, "friends.txt")},
            {profilePath(HASH_B, "SaveMeta.xbx")},
            {profilePath(HASH_B, "blam.lst")}, {"config.toml"}, {"favorites.txt"}};
        for (String[] path : same)
            check(Arrays.equals(read(new File(root, path[0])), read(new File(staging, path[0]))),
                "extract must reproduce " + path[0] + " byte for byte");
        check(countFiles(staging) == 8, "extract must write nothing else: " + countFiles(staging) + " files");
        check(!new File(staging, "hardware_id.txt").exists() && !new File(staging, "maps").exists(),
            "the archive must never hold device identity or maps");

        // swap onto a data root that has an older copy of one profile and a third one
        File other = folder();
        put(other, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Old"));
        put(other, profilePath(HASH_B, "blam.lst"), pattern(10, 9));
        put(other, profilePath(HASH_B, "savegame.bin"), pattern(20, 9));
        put(other, profilePath(HASH_B, "friends.txt"), text("old key\n"));
        put(other, profilePath(HASH_C, "SaveMeta.xbx"), utf16("Gamma"));
        put(other, profilePath(HASH_C, "blam.sav"), pattern(30, 4));
        put(other, "config.toml", text("old settings"));
        put(other, "favorites.txt", text("old favorites"));
        File otherStaging = new File(other, "save/u.restoring");
        BackupArchive.extract(new ByteArrayInputStream(archive), otherStaging, true, true, true);
        BackupArchive.swapIn(other, otherStaging);
        check(Arrays.equals(read(new File(root, profilePath(HASH_B, "blam.lst"))),
            read(new File(other, profilePath(HASH_B, "blam.lst")))),
            "the profile of the same name must be replaced");
        check(!new File(other, profilePath(HASH_B, "savegame.bin")).exists(),
            "a replaced profile is replaced whole");
        check(Arrays.equals(text("old key\n"), read(new File(other, profilePath(HASH_B, "friends.txt")))),
            "a replaced profile keeps its friends.txt when the backup has none for it");
        check(Arrays.equals(read(new File(root, profilePath(HASH_A, "friends.txt"))),
            read(new File(other, profilePath(HASH_A, "friends.txt")))), "friends.txt must come back byte for byte");
        check(Arrays.equals(utf16("Gamma"), read(new File(other, profilePath(HASH_C, "SaveMeta.xbx")))),
            "a profile that is not in the archive must stay");
        check(new File(other, profilePath(HASH_A, "savegame.bin")).length() == 0x1400000,
            "a new profile must arrive");
        check(Arrays.equals(read(new File(root, "config.toml")), read(new File(other, "config.toml"))),
            "config.toml must be replaced");
        check(Arrays.equals(read(new File(root, "favorites.txt")), read(new File(other, "favorites.txt"))),
            "favorites.txt must come back byte for byte");
        check(!otherStaging.exists() && !new File(other, "save/u.replaced").exists(),
            "the staging and aside folders must be gone");
        File[] listed = new File(other, "save/u/UDATA").listFiles();
        check(listed != null && listed.length == 3, "UDATA must hold exactly the three profiles");

        // most players have no favorites: the marker says so and no entry is written
        File none = dataRootWithTwoProfiles();
        ByteArrayOutputStream plain = new ByteArrayOutputStream();
        BackupArchive.Summary noFavorites = BackupArchive.write(plain, none, null, 1, new Date(0));
        byte[] without = plain.toByteArray();
        check(noFavorites.settings && !noFavorites.favorites, "write must report the settings without favorites");
        check(markerOf(without).contains("\nfavorites=false\n"),
            "the marker must say favorites=false: " + markerOf(without));
        check(!holds(without, "favorites.txt"), "no favorites.txt entry must be written");
        BackupArchive.Summary scannedWithout = BackupArchive.scan(new ByteArrayInputStream(without));
        check(scannedWithout.settings && !scannedWithout.favorites, "scan must report no favorites");
        File noneStaging = new File(folder(), "stage");
        BackupArchive.extract(new ByteArrayInputStream(without), noneStaging, true, true, true);
        check(!new File(noneStaging, "favorites.txt").exists(), "extract must write no favorites.txt");
    }

    private static int countFiles(File f) {
        File[] children = f.listFiles();
        if (children == null)
            return 1;
        int n = 0;
        for (File c : children)
            n += countFiles(c);
        return n;
    }

    private static void marker() throws Exception {
        byte[] config = text("a=1");
        refused(zip("config.toml", config), "openh1-backup.properties", "a zip without the marker");
        refused(zip("config.toml", config, "openh1-backup.properties", text(marker(0, true, false))),
            "config.toml", "a marker that is not first");
        refused(zip("openh1-backup.properties",
            text(GOOD_MARKER.replace("format=openh1-backup", "format=other"))), "openh1-backup", "a wrong format");
        refused(zip("openh1-backup.properties", text(GOOD_MARKER.replace("version=1", "version=2"))),
            "version", "version 2");
        refused(renamed(zip("openh1-backup.properties", text(GOOD_MARKER),
            "openh1-backup.propertieX", text(GOOD_MARKER)),
            "openh1-backup.propertieX", "openh1-backup.properties"), "second marker", "a second marker");
        refused(new byte[] {1, 2, 3, 4, 5, 6, 7, 8}, "not a GOAT H1 backup", "bytes that are not a zip");
        BackupArchive.Summary ok = BackupArchive.scan(new ByteArrayInputStream(
            zip("openh1-backup.properties", text(GOOD_MARKER))));
        check(ok.profiles == 0 && ok.build == 7 && !ok.settings, "an empty but valid backup must scan");
        BackupArchive.Summary first = BackupArchive.scan(new ByteArrayInputStream(
            zip("openh1-backup.properties", text(marker(0, true, false, 1)), "config.toml", config)));
        check(first.settings && !first.favorites, "a marker without the favorites key must read as none");
        refused(zip("openh1-backup.properties", text(marker(0, true, false, false, 0).replace("favorites=false",
            "favorites=maybe"))), "favorites", "a favorites field that is not true or false");
    }

    private static void names() throws Exception {
        String m = marker(0, false, false);
        byte[] one = text("x");
        String[] bad = {"../x", "/etc/passwd", "save/u/UDATA/0123456789ab/blam.sav",
            "save/u/UDATA/0123456789AB/notes.txt", "maps/ui.map", "hardware_id.txt", "save/z/lastprof.txt",
            "save/u/UDATA/0123456789AB/", "save/u/UDATA/../../x", "save\\u\\UDATA\\x"};
        for (String name : bad)
            refused(zip("openh1-backup.properties", text(m), name, one), name, "the entry " + name);
        // a duplicate: the zip writer refuses two equal names, so one is renamed in the bytes afterwards
        refused(renamed(zip("openh1-backup.properties", text(marker(0, true, false)), "config.toml", one,
            "config.tomX", one), "config.tomX", "config.toml"), "config.toml", "a duplicate config.toml");
    }

    private static void sizes() throws Exception {
        String full = marker(1, false, false, 3);
        byte[] meta = utf16("P");
        byte[] blam = pattern(16, 1);
        refused(zip("openh1-backup.properties", text(full), profilePath(HASH_A, "SaveMeta.xbx"), meta,
            profilePath(HASH_A, "blam.sav"), blam, profilePath(HASH_A, "savegame.bin"), new byte[33554433]),
            "savegame.bin", "a savegame.bin of 33554433 bytes");
        BackupArchive.scan(new ByteArrayInputStream(zip("openh1-backup.properties", text(full),
            profilePath(HASH_A, "SaveMeta.xbx"), meta, profilePath(HASH_A, "blam.sav"), blam,
            profilePath(HASH_A, "savegame.bin"), new byte[33554432])));
        refused(zip("openh1-backup.properties", text(full), profilePath(HASH_A, "SaveMeta.xbx"), meta,
            profilePath(HASH_A, "blam.sav"), blam, profilePath(HASH_A, "friends.txt"), new byte[65537]),
            "friends.txt", "a friends.txt of 65537 bytes");
        // (and friends.txt is not a second blam file)
        BackupArchive.scan(new ByteArrayInputStream(zip("openh1-backup.properties", text(full),
            profilePath(HASH_A, "SaveMeta.xbx"), meta, profilePath(HASH_A, "blam.sav"), blam,
            profilePath(HASH_A, "friends.txt"), new byte[65536])));
        refused(zip("openh1-backup.properties", text(marker(0, true, false)), "config.toml",
            new byte[1024 * 1024 + 1]), "config.toml", "a config.toml of 1 MiB + 1");
        refused(zip("openh1-backup.properties", text(marker(0, true, true, false, 2)), "config.toml", text("a=1"),
            "favorites.txt", new byte[65537]), "favorites.txt", "a favorites.txt of 65537 bytes");
        BackupArchive.scan(new ByteArrayInputStream(zip("openh1-backup.properties",
            text(marker(0, true, true, false, 2)), "config.toml", text("a=1"), "favorites.txt",
            new byte[65536])));
        refused(zip("openh1-backup.properties", text(marker(0, false, true)), "touch-layout.halolayout",
            new byte[65537]), "touch-layout.halolayout", "a layout of 65537 bytes");
        refused(zip("openh1-backup.properties", text(marker(1, false, false)),
            profilePath(HASH_A, "SaveMeta.xbx"), new byte[4097]), "SaveMeta.xbx", "a name file of 4097 bytes");

        List<Object> many = new ArrayList<>();
        many.add("openh1-backup.properties");
        many.add(text(marker(200, false, false, 400)));
        for (int i = 0; i < 201; i++) {
            String hash = String.format("%012X", i);
            many.add(profilePath(hash, "SaveMeta.xbx"));
            many.add(meta);
            many.add(profilePath(hash, "blam.lst"));
            many.add(blam);
        }
        refused(zip(many.toArray()), "200 profiles", "201 profile directories");
        many.subList(many.size() - 4, many.size()).clear();
        check(BackupArchive.scan(new ByteArrayInputStream(zip(many.toArray()))).profiles == 200,
            "200 profile directories must be accepted");
        refused(zip("openh1-backup.properties", text(marker(201, false, false))), "profiles",
            "a marker listing 201 profiles");

        long before = BackupArchive.totalCapForTests;
        BackupArchive.totalCapForTests = 1000;
        try {
            refused(zip("openh1-backup.properties", text(marker(0, true, false)), "config.toml", new byte[2000]),
                "config.toml", "an archive over the total cap");
        } finally {
            BackupArchive.totalCapForTests = before;
        }
    }

    private static void truncated() throws Exception {
        File root = dataRootWithTwoProfiles();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.write(out, root, "layout=1\n", 1, new Date(0));
        byte[] whole = out.toByteArray();
        refused(Arrays.copyOf(whole, whole.length / 2), "backup", "a zip cut in half");
        refused(Arrays.copyOf(whole, 100), "backup", "a zip cut inside its first entries");

        // cut at an entry boundary: the zip reads as a clean end, so the marker's counts must catch it
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        int boundary;
        try (ZipOutputStream z = new ZipOutputStream(bytes)) {
            put(z, "openh1-backup.properties", text(marker(2, true, true)));
            put(z, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Beta"));
            put(z, profilePath(HASH_B, "blam.lst"), pattern(64, 2));
            z.flush();
            boundary = bytes.size();
            put(z, profilePath(HASH_A, "SaveMeta.xbx"), utf16("Alpha"));
            put(z, profilePath(HASH_A, "blam.sav"), pattern(64, 1));
            put(z, "config.toml", text("a=1"));
            put(z, "touch-layout.halolayout", text("l=1"));
        }
        byte[] cut = Arrays.copyOf(bytes.toByteArray(), boundary);
        refused(cut, "ends early", "a zip cut at an entry boundary");

        String m2 = marker(1, false, false);
        refused(zip("openh1-backup.properties", text(m2), profilePath(HASH_A, "SaveMeta.xbx"), utf16("Solo")),
            "Solo", "a profile with neither blam.sav nor blam.lst");
        refused(zip("openh1-backup.properties", text(m2), profilePath(HASH_A, "SaveMeta.xbx"), utf16("Twin"),
            profilePath(HASH_A, "blam.sav"), pattern(8, 1), profilePath(HASH_A, "blam.lst"), pattern(8, 2)),
            "Twin", "a profile with both blam.sav and blam.lst");
        refused(zip("openh1-backup.properties", text(m2), profilePath(HASH_A, "blam.sav"), pattern(8, 1)),
            HASH_A, "a profile without SaveMeta.xbx");
        refused(zip("openh1-backup.properties", text(marker(2, false, false)),
            profilePath(HASH_A, "SaveMeta.xbx"), utf16("One"), profilePath(HASH_A, "blam.sav"), pattern(8, 1)),
            "2 profiles", "a marker with more profiles than entries");
        refused(zip("openh1-backup.properties", text(marker(0, false, false)),
            profilePath(HASH_A, "SaveMeta.xbx"), utf16("One"), profilePath(HASH_A, "blam.sav"), pattern(8, 1)),
            "0 profiles", "a marker with fewer profiles than entries");
        refused(zip("openh1-backup.properties", text(marker(0, true, false))), "config.toml",
            "a marker promising a missing config.toml");
        refused(zip("openh1-backup.properties", text(marker(0, true, true, false, 2)), "config.toml", text("a=1")),
            "favorites.txt is missing", "a marker promising a missing favorites.txt");
        refused(zip("openh1-backup.properties", text(marker(0, true, false, false, 2)), "config.toml", text("a=1"),
            "favorites.txt", text("x")), "favorites.txt", "a favorites.txt the marker does not list");

        // cut right before the last savegame.bin, with no config.toml and no layout after it: only the entry
        // count in the marker can tell
        ByteArrayOutputStream before = new ByteArrayOutputStream();
        try (ZipOutputStream z = new ZipOutputStream(before)) {
            put(z, "openh1-backup.properties", text(marker(2, false, false, 5)));
            put(z, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Beta"));
            put(z, profilePath(HASH_B, "blam.lst"), pattern(64, 2));
            put(z, profilePath(HASH_A, "SaveMeta.xbx"), utf16("Alpha"));
            put(z, profilePath(HASH_A, "blam.sav"), pattern(64, 1));
        }
        refused(before.toByteArray(), "4 of 5 entries", "an archive cut before the last savegame.bin");
        BackupArchive.scan(new ByteArrayInputStream(zip("openh1-backup.properties",
            text(marker(1, false, false, 2)), profilePath(HASH_A, "SaveMeta.xbx"), utf16("Whole"),
            profilePath(HASH_A, "blam.sav"), pattern(8, 1))));
        refused(zip("openh1-backup.properties", text(marker(1, false, false, 1)),
            profilePath(HASH_A, "SaveMeta.xbx"), utf16("Extra"), profilePath(HASH_A, "blam.sav"), pattern(8, 1)),
            "more entries", "more entries than listed");
    }

    private static void put(ZipOutputStream z, String name, byte[] bytes) throws IOException {
        z.putNextEntry(new ZipEntry(name));
        z.write(bytes);
        z.closeEntry();
    }

    private static void recover() throws Exception {
        // a stale aside copy beside a complete profile is deleted
        File a = folder();
        put(a, profilePath(HASH_A, "SaveMeta.xbx"), utf16("New"));
        put(a, profilePath(HASH_A, "blam.sav"), pattern(8, 1));
        put(a, "save/u.replaced/" + HASH_A + "/SaveMeta.xbx", utf16("Old"));
        String said = BackupArchive.recover(a);
        check(!said.isEmpty(), "recover must say what it did");
        check(!new File(a, "save/u.replaced").exists(), "the old copy beside a complete profile must be deleted");
        check(Arrays.equals(utf16("New"), read(new File(a, profilePath(HASH_A, "SaveMeta.xbx")))),
            "the new profile must stay");

        // an aside copy with no sibling is renamed back
        File b = folder();
        put(b, "save/u.replaced/" + HASH_B + "/SaveMeta.xbx", utf16("Kept"));
        put(b, "save/u.replaced/" + HASH_B + "/blam.lst", pattern(8, 2));
        BackupArchive.recover(b);
        check(Arrays.equals(utf16("Kept"), read(new File(b, profilePath(HASH_B, "SaveMeta.xbx")))),
            "the old profile must come back");
        check(!new File(b, "save/u.replaced").exists(), "the aside folder must be gone after the rename back");

        // a sibling that never became complete is replaced by the old copy
        File c = folder();
        put(c, profilePath(HASH_C, "blam.sav"), pattern(8, 5));
        put(c, "save/u.replaced/" + HASH_C + "/SaveMeta.xbx", utf16("Whole"));
        BackupArchive.recover(c);
        check(Arrays.equals(utf16("Whole"), read(new File(c, profilePath(HASH_C, "SaveMeta.xbx"))))
            && !new File(c, profilePath(HASH_C, "blam.sav")).exists(),
            "an incomplete sibling must give way to the old copy");

        // a leftover staging folder is deleted
        File d = folder();
        put(d, "save/u.restoring/save/u/UDATA/" + HASH_A + "/SaveMeta.xbx", utf16("Half"));
        put(d, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Live"));
        BackupArchive.recover(d);
        check(!new File(d, "save/u.restoring").exists(), "a leftover staging folder must be deleted");
        check(new File(d, profilePath(HASH_B, "SaveMeta.xbx")).isFile(),
            "recover must not touch live profiles");

        // nothing to do, and a fresh install with no save folder at all
        check("".equals(BackupArchive.recover(d)), "recover must say nothing when nothing was there");
        File fresh = folder();
        check("".equals(BackupArchive.recover(fresh)), "a fresh install must be handled without error");
        check(!new File(fresh, "save").exists(), "recover must not create folders on a fresh install");
    }

    private static void lock() throws Exception {
        File root = folder();
        BackupArchive.Lock first = BackupArchive.tryLock(root);
        check(first != null, "the first holder must get the lock");
        check(new File(root, "save/.backup-lock").isFile(), "the lock file must live in save/");
        try {
            BackupArchive.Lock second = null;
            try {
                second = BackupArchive.tryLock(root);
            } catch (OverlappingFileLockException e) {
                // a second channel on one file may say so by throwing, which also means held
            }
            check(second == null, "a second holder must not get the lock");
            put(root, "save/u.restoring/x", text("half"));
            check("".equals(BackupArchive.recover(root)), "recover must stand aside while the lock is held");
            check(new File(root, "save/u.restoring").exists(),
                "recover must touch nothing while the lock is held");
        } finally {
            first.close();
        }
        check(!BackupArchive.recover(root).isEmpty(), "recover must work once the lock is released");
        check(!new File(root, "save/u.restoring").exists(), "recover must delete the leftover once it can");
        BackupArchive.Lock again = BackupArchive.tryLock(root);
        check(again != null, "the lock must be free again after close");
        again.close();
        check(new File(root, "save/.backup-lock").isFile(), "the lock file is never deleted");
    }

    private static void replaced() throws Exception {
        File root = dataRootWithTwoProfiles();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.Summary written = BackupArchive.write(out, root, null, 1, new Date(0));
        check(written.profileHashes.equals(Arrays.asList(HASH_B, HASH_A)),
            "write must list the profile hashes in archive order: " + written.profileHashes);
        BackupArchive.Summary scanned = BackupArchive.scan(new ByteArrayInputStream(out.toByteArray()));
        check(scanned.profileHashes.equals(Arrays.asList(HASH_B, HASH_A)),
            "scan must list the profile hashes beside their names: " + scanned.profileHashes);

        // the phone has HASH_B under another name, a half-made HASH_A and a HASH_C the backup does not hold
        File phone = folder();
        put(phone, profilePath(HASH_B, "SaveMeta.xbx"), utf16("Newer\0\0"));
        put(phone, profilePath(HASH_B, "blam.lst"), pattern(8, 1));
        put(phone, profilePath(HASH_A, "blam.sav"), pattern(8, 2));
        put(phone, profilePath(HASH_C, "SaveMeta.xbx"), utf16("Gamma"));
        put(phone, profilePath(HASH_C, "blam.sav"), pattern(8, 3));
        List<String> names = BackupArchive.replacedProfiles(phone, scanned);
        check(names.equals(Arrays.asList("Newer", HASH_A)),
            "the replaced profiles must be named as the phone names them, a nameless one by its hash: " + names);

        // the list is what swapIn does: exactly those folders are replaced
        File staging = new File(phone, "save/u.restoring");
        BackupArchive.extract(new ByteArrayInputStream(out.toByteArray()), staging, true, false, false);
        BackupArchive.swapIn(phone, staging);
        check(Arrays.equals(utf16("Beéta\0\0"), read(new File(phone, profilePath(HASH_B, "SaveMeta.xbx"))))
            && Arrays.equals(utf16("Alpha"), read(new File(phone, profilePath(HASH_A, "SaveMeta.xbx"))))
            && Arrays.equals(utf16("Gamma"), read(new File(phone, profilePath(HASH_C, "SaveMeta.xbx")))),
            "swapIn must replace the listed profiles and keep the other");

        File empty = folder();
        check(BackupArchive.replacedProfiles(empty, scanned).isEmpty(), "a phone with no profiles loses none");
    }

    private static void locked() throws Exception {
        File fresh = folder();
        check(!BackupArchive.locked(fresh), "a fresh install has no lock held");
        check(!new File(fresh, "save").exists(), "looking at the lock must not create folders on a fresh install");
        File root = folder();
        BackupArchive.Lock held = BackupArchive.tryLock(root);
        check(held != null, "the lock must be free at first");
        try {
            check(BackupArchive.locked(root), "a held lock must be seen");
            check(BackupArchive.locked(root), "looking must not release a lock another holder has");
        } finally {
            held.close();
        }
        check(!BackupArchive.locked(root), "a released lock must not be seen as held");
        BackupArchive.Lock again = BackupArchive.tryLock(root);
        check(again != null, "looking must leave the lock free");
        again.close();
    }

    private static void damaged() throws Exception {
        String m = marker(1, false, false, 1);
        refused(poisoned(zip("openh1-backup.properties", text(m), profilePath(HASH_A, "blam.sax"), text("x")),
            profilePath(HASH_A, "blam.sax")), "damaged", "an entry name that is not UTF-8");
        refused(zip("openh1-backup.properties", text(GOOD_MARKER.replace("made=2026-10-22T10:00:00+00:00",
            "made=\\u12"))), "marker is damaged", "a marker with a bad unicode escape");
        // a long name with a control character is cut and cleaned in the message
        StringBuilder longName = new StringBuilder("maps/");
        for (int i = 0; i < 300; i++)
            longName.append(i == 10 ? (char) 7 : 'a');
        try {
            BackupArchive.scan(new ByteArrayInputStream(zip("openh1-backup.properties", text(GOOD_MARKER),
                longName.toString(), text("x"))));
            throw new AssertionError("a long odd name must be refused");
        } catch (IOException e) {
            check(e.getMessage().length() < 250 && e.getMessage().indexOf((char) 7) < 0,
                "the message must hold a cut, clean entry name: " + e.getMessage().length());
        }
    }

    private static void choices() throws Exception {
        File root = dataRootWithTwoProfiles();
        put(root, "favorites.txt", text("10.0.0.5:2302\n"));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.write(out, root, "layout=1\n", 1, new Date(0));
        byte[] archive = out.toByteArray();

        File noProfiles = new File(folder(), "s");
        BackupArchive.Summary s = BackupArchive.extract(new ByteArrayInputStream(archive), noProfiles,
            false, true, true);
        check(!new File(noProfiles, "save").exists() && new File(noProfiles, "config.toml").isFile(),
            "profiles=false must write no profile");
        check(new File(noProfiles, "favorites.txt").isFile(), "the favorites must come with the settings");
        check(s.profiles == 2, "the summary still says what the archive holds");

        File noSettings = new File(folder(), "s");
        BackupArchive.extract(new ByteArrayInputStream(archive), noSettings, true, false, true);
        check(!new File(noSettings, "config.toml").exists() && !new File(noSettings, "favorites.txt").exists()
            && new File(noSettings, profilePath(HASH_A, "blam.sav")).isFile(),
            "settings=false must write no config.toml and no favorites.txt");

        File noLayout = new File(folder(), "s");
        BackupArchive.Summary l = BackupArchive.extract(new ByteArrayInputStream(archive), noLayout,
            true, true, false);
        check(l.layoutText == null, "touchLayout=false must leave layoutText null");

        File nothing = new File(folder(), "s");
        BackupArchive.extract(new ByteArrayInputStream(archive), nothing, false, false, false);
        check(nothing.list().length == 0, "choosing nothing must write nothing");

        File used = folder();
        put(used, "stray", text("x"));
        try {
            BackupArchive.extract(new ByteArrayInputStream(archive), used, true, true, true);
            throw new AssertionError("a staging folder that is not empty must be refused");
        } catch (IOException expected) {
            check(expected.getMessage().contains("not empty"), "the message must say the folder is not empty");
        }
    }

    private static void skipped() throws Exception {
        File root = dataRootWithTwoProfiles();
        // a folder with a name file and both blam files cannot be restored, so the export leaves it out
        put(root, profilePath(HASH_C, "SaveMeta.xbx"), utf16("Broken"));
        put(root, profilePath(HASH_C, "blam.sav"), pattern(8, 1));
        put(root, profilePath(HASH_C, "blam.lst"), pattern(8, 2));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.Summary written = BackupArchive.write(out, root, null, 1, new Date(0));
        check(written.profiles == 2, "the export must hold the two good profiles: " + written.profiles);
        check(written.skipped == 1, "the export must count the folder it left out: " + written.skipped);
        BackupArchive.Summary scanned = BackupArchive.scan(new ByteArrayInputStream(out.toByteArray()));
        check(scanned.profiles == 2 && scanned.skipped == 0, "a scan never reports skipped folders");
        File clean = dataRootWithTwoProfiles();
        BackupArchive.Summary all = BackupArchive.write(new ByteArrayOutputStream(), clean, null, 1, new Date(0));
        check(all.skipped == 0, "a data root with only good profiles skips nothing: " + all.skipped);
    }

    private static void swapFailsLate() throws Exception {
        String prefix = "The profiles were restored, but ";
        File root = dataRootWithTwoProfiles();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        BackupArchive.write(out, root, null, 1, new Date(0));
        File other = folder();
        File staging = new File(other, "save/u.restoring");
        BackupArchive.extract(new ByteArrayInputStream(out.toByteArray()), staging, true, true, false);
        // a folder that is not empty at config.toml makes the settings move fail after the profiles moved
        put(other, "config.toml/inside", text("x"));
        try {
            BackupArchive.swapIn(other, staging);
            throw new AssertionError("a settings move that cannot happen must be reported");
        } catch (IOException e) {
            String said = e.getMessage();
            check(said != null && said.startsWith(prefix),
                "the message must say the profiles were restored: " + said);
            check(Character.isLowerCase(said.charAt(prefix.length())),
                "the original message must start with a lower-case letter: " + said);
        }
        check(new File(other, profilePath(HASH_A, "blam.sav")).isFile()
            && new File(other, profilePath(HASH_B, "blam.lst")).isFile(),
            "the profiles must be in place after the failure");
        // a move that fails part-way counts the profiles in place: HASH_B sorts first and moves, then
        // HASH_A finds its old self in place and the set-aside folder cannot be made
        File partWay = folder();
        File laterStaging = new File(partWay, "save/u.restoring");
        BackupArchive.extract(new ByteArrayInputStream(out.toByteArray()), laterStaging, true, false, false);
        put(partWay, profilePath(HASH_A, "SaveMeta.xbx"), utf16("Old"));
        put(partWay, profilePath(HASH_A, "blam.sav"), pattern(8, 9));
        put(partWay, "save/u.replaced", text("a file where the set-aside folder should be"));
        try {
            BackupArchive.swapIn(partWay, laterStaging);
            throw new AssertionError("a set-aside folder that cannot be made must be reported");
        } catch (IOException e) {
            String said = e.getMessage();
            check(said != null && said.startsWith("1 of 2 profiles was restored, but could not create "),
                "the message must count the profiles in place: " + said);
        }
        check(new File(partWay, profilePath(HASH_B, "blam.lst")).isFile(),
            "the first profile must be in place after the failure");
        byte[] meta = Files.readAllBytes(new File(partWay, profilePath(HASH_A, "SaveMeta.xbx")).toPath());
        check(Arrays.equals(meta, utf16("Old")), "the old profile must be back in place after the failure");
        // a failure before anything moved is not prefixed
        File third = folder();
        File emptyStaging = new File(third, "save/u.restoring");
        check(emptyStaging.mkdirs(), "the empty staging folder must be made");
        put(third, "save/u", text("a file where the profiles folder should be"));
        try {
            BackupArchive.swapIn(third, emptyStaging);
            throw new AssertionError("a swap that cannot create the profiles folder must be reported");
        } catch (IOException e) {
            check(e.getMessage() != null && !e.getMessage().contains("restored, but"),
                "a failure before any move must not claim a restore: " + e.getMessage());
        }
    }
}
