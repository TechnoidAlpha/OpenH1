package com.halo.decomp;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Writes and reads the backup of the player's saved games and settings, for
 * BackupActivity: one zip, made with java.util.zip only, whose entry names
 * mirror the data root (the app's external files folder). It holds each
 * profile under save/u/UDATA/<12 hex digits>/ (its name file, its profile or
 * saved variant and its campaign save), config.toml, favorites.txt (the
 * server browsers' favorite games, which go with the settings) and the touch
 * layout text. It never holds the maps (the disc image gives them back), the
 * device's hardware_id.txt, the network files, debug.txt or mods. The first
 * entry is a small marker, openh1-backup.properties, that says what follows.
 *
 * The archive is the player's file and nothing read from it is trusted:
 * every entry name is checked against a short allow-list, each entry is
 * capped while it is inflated (never by its header), and the whole archive
 * is capped. A zip cut at an entry boundary reads as a clean end, so the
 * marker's counts (profiles, parts and entries) are compared with what was
 * seen. A restore is two passes:
 * scan reads everything and writes nothing, then extract writes into a
 * staging folder and swapIn moves the profiles into place. Nothing
 * transient ever lives under save/u/UDATA, because the game can start
 * without passing the launcher's recover and would list it as a profile. A
 * file lock beside save/u keeps a sweep in one process from running while a
 * restore in another is moving files.
 *
 * No android.* class is used here, so the checks in tools/tests run it on a
 * desktop JDK.
 */
final class BackupArchive {
    /** What an archive holds, from a scan or a write. */
    static final class Summary {
        final List<String> profileNames;
        /** each profile's folder name (its hash), in the order of profileNames */
        final List<String> profileHashes;
        final int profiles;
        final boolean settings;
        final boolean favorites;
        final boolean touchLayout;
        final int build;
        final String made;
        final long bytes;
        final String layoutText;
        final int skipped;

        Summary(List<String> profileNames, List<String> profileHashes, int profiles, boolean settings,
                boolean favorites, boolean touchLayout, int build, String made, long bytes, String layoutText,
                int skipped) {
            this.profileNames = profileNames;
            this.profileHashes = profileHashes;
            this.profiles = profiles;
            this.settings = settings;
            this.favorites = favorites;
            this.touchLayout = touchLayout;
            this.build = build;
            this.made = made;
            this.bytes = bytes;
            this.layoutText = layoutText;
            this.skipped = skipped;
        }
    }

    private static final String MARKER_NAME = "openh1-backup.properties";
    private static final String CONFIG_NAME = "config.toml";
    private static final String FAVORITES_NAME = "favorites.txt";
    private static final String LAYOUT_NAME = "touch-layout.halolayout";
    private static final String META_NAME = "SaveMeta.xbx";
    private static final String SAVE_NAME = "blam.sav";
    private static final String LIST_NAME = "blam.lst";
    private static final String GAME_NAME = "savegame.bin";
    // the profile's key and friends (port/linux/src/p2p_friends.c)
    private static final String FRIENDS_NAME = "friends.txt";
    private static final String PROFILE_PREFIX = "save/u/UDATA/";
    private static final String FORMAT = "openh1-backup";
    private static final int VERSION = 1;
    private static final Pattern PROFILE_ENTRY = Pattern.compile(
        "^save/u/UDATA/([0-9A-F]{12})/(SaveMeta\\.xbx|blam\\.sav|blam\\.lst|savegame\\.bin|friends\\.txt)$");
    private static final Pattern PROFILE_DIRECTORY = Pattern.compile("[0-9A-F]{12}");

    private static final long MARKER_CAP_BYTES = 4096;
    private static final long META_CAP_BYTES = 4096;
    private static final long BLAM_CAP_BYTES = 65536;
    private static final long GAME_CAP_BYTES = 33554432;
    // (p2p_friends.c reads no more than this of the file)
    private static final long FRIENDS_CAP_BYTES = 65536;
    private static final long CONFIG_CAP_BYTES = 1024 * 1024;
    private static final long FAVORITES_CAP_BYTES = 65536;
    private static final long LAYOUT_CAP_BYTES = 65536;
    private static final long TOTAL_CAP_BYTES = 4608L * 1024 * 1024;
    private static final int PROFILE_CAP = 200;
    private static final int NAME_CAP_CHARS = 64;
    private static final int ENTRY_NAME_SHOWN_CHARS = 100;
    private static final int ENTRY_CAP = PROFILE_CAP * 5 + 3;
    private static final String LOCK_NAME = "save/.backup-lock";
    private static final int BUFFER_BYTES = 64 * 1024;

    /** the inflated-bytes cap of one archive; a field so the checks can lower it */
    static long totalCapForTests = TOTAL_CAP_BYTES;

    private BackupArchive() {
    }

    /** What the reader has counted so far, shared by every entry. */
    private static final class Counter {
        long total;
    }

    /** A sink that only lets the entry be counted (OutputStream.nullOutputStream needs a newer Android). */
    private static final class Discard extends OutputStream {
        @Override
        public void write(int b) {
        }

        @Override
        public void write(byte[] b, int off, int len) {
        }
    }

    /** What the reader has seen of one profile directory. */
    private static final class Profile {
        String name;
        boolean meta;
        int blam;
    }

    /** One file the writer will add: its entry name, its file and its cap. */
    private static final class Planned {
        final String name;
        final File file;
        final long cap;

        Planned(String name, File file, long cap) {
            this.name = name;
            this.file = file;
            this.cap = cap;
        }
    }

    /** The lock files this process holds; a second channel on one file would drop the first's lock when closed. */
    private static final Set<String> HELD = new HashSet<>();

    /** Held while a restore moves files, so a sweep in another process stands aside. */
    static final class Lock implements Closeable {
        private final String key;
        private final FileChannel channel;
        private final FileLock lock;

        Lock(String key, FileChannel channel, FileLock lock) {
            this.key = key;
            this.channel = channel;
            this.lock = lock;
        }

        @Override
        public void close() throws IOException {
            try {
                lock.release();
            } finally {
                try {
                    channel.close();
                } finally {
                    synchronized (HELD) {
                        HELD.remove(key);
                    }
                }
            }
        }
    }

    /** Takes the lock, or returns null when another process or another holder in this one has it. */
    static Lock tryLock(File dataRoot) throws IOException {
        File file = new File(dataRoot, LOCK_NAME);
        File parent = file.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Could not create " + parent.getPath() + ".");
        String key = file.getCanonicalPath();
        synchronized (HELD) {
            if (!HELD.add(key))
                return null;
        }
        FileChannel channel = null;
        try {
            channel = FileChannel.open(file.toPath(), StandardOpenOption.CREATE, StandardOpenOption.WRITE);
            FileLock lock = channel.tryLock();
            if (lock != null)
                return new Lock(key, channel, lock);
        } catch (OverlappingFileLockException e) {
            // another channel in this process holds it, which is the same answer as another process
        } catch (IOException | RuntimeException e) {
            closeQuietly(channel, key);
            throw e;
        }
        closeQuietly(channel, key);
        return null;
    }

    private static void closeQuietly(FileChannel channel, String key) {
        synchronized (HELD) {
            HELD.remove(key);
        }
        if (channel == null)
            return;
        try {
            channel.close();
        } catch (IOException e) {
            // nothing more can be done with a channel that will not close
        }
    }

    /**
     * Writes the archive of dataRoot: the marker, then the profiles, config.toml with favorites.txt and the
     * layout text if given.
     */
    static Summary write(OutputStream out, File dataRoot, String layoutText, int build, Date now)
            throws IOException {
        int[] skipped = new int[1];
        List<File> directories = profileDirectories(dataRoot, skipped);
        File config = new File(dataRoot, CONFIG_NAME);
        boolean hasConfig = config.isFile();
        File favorites = new File(dataRoot, FAVORITES_NAME);
        // most players have no favorites, so a missing file is not an error
        boolean hasFavorites = hasConfig && favorites.isFile();
        byte[] layout = null;
        if (layoutText != null && !layoutText.isEmpty())
            layout = layoutText.getBytes(StandardCharsets.UTF_8);
        if (layout != null && layout.length > LAYOUT_CAP_BYTES)
            throw new IOException("The touch layout is larger than " + LAYOUT_CAP_BYTES + " bytes.");
        // the plan comes first so that the marker can say how many entries follow it
        List<Planned> plan = new ArrayList<>();
        List<String> names = new ArrayList<>();
        List<String> hashes = new ArrayList<>();
        for (File directory : directories) {
            String prefix = PROFILE_PREFIX + directory.getName() + "/";
            File meta = new File(directory, META_NAME);
            names.add(profileName(readSmallFile(meta, META_CAP_BYTES)));
            hashes.add(directory.getName());
            plan.add(new Planned(prefix + META_NAME, meta, META_CAP_BYTES));
            for (String name : new String[] {SAVE_NAME, LIST_NAME}) {
                File file = new File(directory, name);
                if (file.isFile())
                    plan.add(new Planned(prefix + name, file, BLAM_CAP_BYTES));
            }
            File game = new File(directory, GAME_NAME);
            if (game.isFile())
                plan.add(new Planned(prefix + GAME_NAME, game, GAME_CAP_BYTES));
            File friends = new File(directory, FRIENDS_NAME);
            if (friends.isFile())
                plan.add(new Planned(prefix + FRIENDS_NAME, friends, FRIENDS_CAP_BYTES));
        }
        if (hasConfig)
            plan.add(new Planned(CONFIG_NAME, config, CONFIG_CAP_BYTES));
        if (hasFavorites)
            plan.add(new Planned(FAVORITES_NAME, favorites, FAVORITES_CAP_BYTES));
        int entries = plan.size() + (layout != null ? 1 : 0);
        String made = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssXXX", Locale.ROOT).format(now);
        byte[] marker = ("format=" + FORMAT + "\n"
            + "version=" + VERSION + "\n"
            + "build=" + build + "\n"
            + "made=" + made + "\n"
            + "profiles=" + directories.size() + "\n"
            + "settings=" + hasConfig + "\n"
            + "favorites=" + hasFavorites + "\n"
            + "touch-layout=" + (layout != null) + "\n"
            + "entries=" + entries + "\n").getBytes(StandardCharsets.ISO_8859_1);
        byte[] buffer = new byte[BUFFER_BYTES];
        Counter counter = new Counter();
        ZipOutputStream zip = new ZipOutputStream(out);
        addBytes(zip, MARKER_NAME, marker, counter);
        for (Planned each : plan)
            addFile(zip, each.name, each.file, each.cap, buffer, counter);
        if (layout != null)
            addBytes(zip, LAYOUT_NAME, layout, counter);
        zip.finish();
        out.flush();
        return new Summary(names, hashes, directories.size(), hasConfig, hasFavorites, layout != null, build, made,
            counter.total, null, skipped[0]);
    }

    /** Reads the whole archive, checking every rule, writing nothing; what it holds. */
    static Summary scan(InputStream in) throws IOException {
        return read(in, null, true, true, true);
    }

    /**
     * Extracts the allowed entries into staging (which must be empty): profiles under
     * staging/save/u/UDATA/<hash>/, config.toml and favorites.txt as staging/config.toml and
     * staging/favorites.txt; returns the summary with layoutText.
     */
    static Summary extract(InputStream in, File staging, boolean profiles, boolean settings,
                           boolean touchLayout) throws IOException {
        if (staging == null)
            throw new IOException("There is no staging folder to restore into.");
        return read(in, staging, profiles, settings, touchLayout);
    }

    /**
     * Moves each staged profile directory into dataRoot/save/u/UDATA, replacing one of the same name whole,
     * keeping the others; then config.toml and favorites.txt by rename if staged. A replaced profile's
     * friends.txt stays when the backup has none for it (one made before friends, or before the key was).
     */
    static void swapIn(File dataRoot, File staging) throws IOException {
        File profiles = new File(dataRoot, "save/u/UDATA");
        File aside = new File(dataRoot, "save/u.replaced");
        List<File> sources = new ArrayList<>();
        File[] staged = new File(staging, "save/u/UDATA").listFiles();
        if (staged != null) {
            Arrays.sort(staged);
            for (File source : staged)
                if (source.isDirectory() && PROFILE_DIRECTORY.matcher(source.getName()).matches())
                    sources.add(source);
        }
        int moved = 0;
        try {
            if (!profiles.isDirectory() && !profiles.mkdirs())
                throw new IOException("Could not create " + profiles.getPath() + ".");
            for (File source : sources) {
                File target = new File(profiles, source.getName());
                File old = new File(aside, source.getName());
                if (target.exists()) {
                    if (!aside.isDirectory() && !aside.mkdirs())
                        throw new IOException("Could not create " + aside.getPath() + ".");
                    File friends = new File(target, FRIENDS_NAME);
                    File stagedFriends = new File(source, FRIENDS_NAME);
                    if (friends.isFile() && !stagedFriends.exists()) {
                        try {
                            Files.copy(friends.toPath(), stagedFriends.toPath());
                        } catch (IOException e) {
                            throw new IOException("Could not keep " + friends.getPath() + ".", e);
                        }
                    }
                    deleteRecursively(old);
                    rename(target, old);
                    try {
                        rename(source, target);
                    } catch (IOException e) {
                        // put the old profile back so a failed swap loses nothing
                        old.renameTo(target);
                        throw e;
                    }
                    deleteRecursively(old);
                } else {
                    rename(source, target);
                }
                moved++;
            }
            aside.delete();
            File config = new File(staging, CONFIG_NAME);
            if (config.isFile())
                moveReplacing(config, new File(dataRoot, CONFIG_NAME));
            File favorites = new File(staging, FAVORITES_NAME);
            if (favorites.isFile())
                moveReplacing(favorites, new File(dataRoot, FAVORITES_NAME));
        } catch (IOException e) {
            if (moved == 0)
                throw e;
            // the player must know which profiles are already in place when a later step fails
            String message = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            if (!message.isEmpty())
                message = Character.toLowerCase(message.charAt(0)) + message.substring(1);
            String done = moved == sources.size()
                ? "The profiles were restored, but "
                : moved + " of " + sources.size() + (moved == 1 ? " profiles was" : " profiles were")
                    + " restored, but ";
            throw new IOException(done + message, e);
        }
        deleteRecursively(staging);
    }

    /**
     * Finishes or undoes a swap that was interrupted: a profile left as save/u.replaced/<hash> beside a
     * complete save/u/UDATA/<hash> is deleted, one without a sibling is renamed back; a leftover
     * save/u.restoring folder is deleted. It stands aside, returning "", while another process holds the
     * lock of a restore. Returns what it did, for the log.
     */
    static String recover(File dataRoot) {
        File aside = new File(dataRoot, "save/u.replaced");
        File restoring = new File(dataRoot, "save/u.restoring");
        // a fresh install has no save folder, and the lock would make one
        if (!aside.exists() && !restoring.exists())
            return "";
        StringBuilder done = new StringBuilder();
        try (Lock lock = tryLock(dataRoot)) {
            if (lock == null)
                return "";
            File profiles = new File(dataRoot, "save/u/UDATA");
            File[] left = aside.listFiles();
            if (left != null) {
                for (File old : left) {
                    File sibling = new File(profiles, old.getName());
                    if (new File(sibling, META_NAME).isFile()) {
                        deleteRecursively(old);
                        note(done, "deleted the old copy of " + old.getName());
                    } else {
                        // the new profile never arrived whole: bring the old one back
                        deleteRecursively(sibling);
                        profiles.mkdirs();
                        if (old.renameTo(sibling))
                            note(done, "restored " + old.getName());
                        else
                            note(done, "could not restore " + old.getName());
                    }
                }
                aside.delete();
            }
            if (restoring.exists()) {
                deleteRecursively(restoring);
                note(done, "deleted an unfinished restore");
            }
        } catch (IOException e) {
            note(done, "could not take the backup lock: " + e.getMessage());
        }
        return done.toString();
    }

    private static void note(StringBuilder done, String text) {
        if (done.length() > 0)
            done.append("; ");
        done.append(text);
    }

    /**
     * The profiles in dataRoot that a restore of the scanned backup's profiles replaces whole (swapIn's test:
     * a folder of the same hash), by the names this phone has for them; a profile whose name file cannot be
     * read is shown by its hash.
     */
    static List<String> replacedProfiles(File dataRoot, Summary scanned) {
        List<String> replaced = new ArrayList<>();
        for (String hash : scanned.profileHashes) {
            File directory = new File(dataRoot, PROFILE_PREFIX + hash);
            if (!directory.exists())
                continue;
            String name = "";
            try {
                name = profileName(readSmallFile(new File(directory, META_NAME), META_CAP_BYTES)).trim();
            } catch (IOException e) {
                // a half-made profile: its hash is all there is to show
            }
            replaced.add(name.isEmpty() ? hash : name);
        }
        return replaced;
    }

    /**
     * Whether another process, or another holder in this one, has the lock right now: a restore moving files
     * or a recovery, which the game and the backup screen wait out. A data root where no restore ever ran has
     * no lock file and is not made one.
     */
    static boolean locked(File dataRoot) {
        if (!new File(dataRoot, LOCK_NAME).isFile())
            return false;
        try (Lock lock = tryLock(dataRoot)) {
            return lock == null;
        } catch (IOException e) {
            // a lock that cannot be looked at is no sign of a restore
            return false;
        }
    }

    /**
     * The profile directories worth saving: a hash name, SaveMeta.xbx and exactly one of blam.sav, blam.lst.
     * The hash-named directories that fail that test are counted in skipped[0].
     */
    private static List<File> profileDirectories(File dataRoot, int[] skipped) {
        List<File> found = new ArrayList<>();
        File[] all = new File(dataRoot, "save/u/UDATA").listFiles();
        if (all == null)
            return found;
        Arrays.sort(all);
        for (File directory : all) {
            if (!directory.isDirectory() || !PROFILE_DIRECTORY.matcher(directory.getName()).matches())
                continue;
            boolean sav = new File(directory, SAVE_NAME).isFile();
            boolean lst = new File(directory, LIST_NAME).isFile();
            // a half-made profile could not be restored, so it is left out
            if (new File(directory, META_NAME).isFile() && sav != lst)
                found.add(directory);
            else
                skipped[0]++;
        }
        return found;
    }

    private static byte[] readSmallFile(File file, long cap) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[BUFFER_BYTES];
            int n;
            while ((n = in.read(buffer)) > 0) {
                bytes.write(buffer, 0, n);
                if (bytes.size() > cap)
                    throw new IOException("The file " + file.getPath() + " is larger than " + cap + " bytes.");
            }
        }
        return bytes.toByteArray();
    }

    private static void addBytes(ZipOutputStream zip, String name, byte[] bytes, Counter counter)
            throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(bytes);
        zip.closeEntry();
        counter.total += bytes.length;
    }

    private static void addFile(ZipOutputStream zip, String name, File file, long cap, byte[] buffer,
                                Counter counter) throws IOException {
        zip.putNextEntry(new ZipEntry(name));
        long size = 0;
        try (InputStream in = new FileInputStream(file)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                size += n;
                if (size > cap)
                    throw new IOException("The file " + file.getPath() + " is larger than " + cap
                        + " bytes, so it looks damaged.");
                zip.write(buffer, 0, n);
            }
        }
        zip.closeEntry();
        counter.total += size;
    }

    /** The profile's display name from SaveMeta.xbx: UTF-16LE, no NULs or controls, 64 characters at most. */
    private static String profileName(byte[] meta) {
        String text = new String(meta, StandardCharsets.UTF_16LE);
        int end = text.length();
        while (end > 0 && text.charAt(end - 1) == 0)
            end--;
        StringBuilder name = new StringBuilder();
        for (int i = 0; i < end && name.length() < NAME_CAP_CHARS; i++) {
            char c = text.charAt(i);
            name.append(Character.isISOControl(c) ? ' ' : c);
        }
        return name.toString();
    }

    /** An entry name as a message shows it: control characters replaced, cut at 100 characters. */
    private static String shown(String name) {
        StringBuilder text = new StringBuilder();
        for (int i = 0; i < name.length() && i < ENTRY_NAME_SHOWN_CHARS; i++) {
            char c = name.charAt(i);
            text.append(Character.isISOControl(c) ? ' ' : c);
        }
        return text.toString();
    }

    /** One reader for scan and extract; staging is null for a scan. */
    private static Summary read(InputStream in, File staging, boolean wantProfiles, boolean wantSettings,
                                boolean wantLayout) throws IOException {
        String stagingPrefix = null;
        if (staging != null) {
            String[] existing = staging.list();
            if (existing != null && existing.length > 0)
                throw new IOException("The staging folder is not empty: " + staging.getPath() + ".");
            if (!staging.isDirectory() && !staging.mkdirs())
                throw new IOException("Could not create " + staging.getPath() + ".");
            stagingPrefix = staging.getCanonicalPath() + File.separator;
        }
        Properties marker = null;
        int expectedProfiles = 0;
        int expectedEntries = 0;
        boolean expectedSettings = false;
        boolean expectedFavorites = false;
        boolean expectedLayout = false;
        int build = 0;
        String made = "";
        Map<String, Profile> profiles = new LinkedHashMap<>();
        Set<String> seen = new HashSet<>();
        int entriesSeen = 0;
        boolean sawConfig = false;
        boolean sawFavorites = false;
        boolean sawLayout = false;
        String layoutText = null;
        Counter counter = new Counter();
        byte[] buffer = new byte[BUFFER_BYTES];
        String current = "the start of the file";
        ZipInputStream zip = new ZipInputStream(in);
        try {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                String label = shown(name);
                current = label;
                if (marker == null) {
                    if (!MARKER_NAME.equals(name))
                        throw new IOException("This file is not a GOAT H1 backup: its first entry is "
                            + label + ", not " + MARKER_NAME + ".");
                    marker = parseMarker(drain(zip, name, MARKER_CAP_BYTES, null, buffer, counter)
                        .toByteArray());
                    seen.add(name);
                    expectedProfiles = Integer.parseInt(marker.getProperty("profiles").trim());
                    expectedEntries = Integer.parseInt(marker.getProperty("entries").trim());
                    expectedSettings = "true".equals(marker.getProperty("settings").trim());
                    // the first markers had no favorites key, which reads as none
                    expectedFavorites = "true".equals(marker.getProperty("favorites", "false").trim());
                    expectedLayout = "true".equals(marker.getProperty("touch-layout").trim());
                    build = parseOptionalInt(marker.getProperty("build"));
                    made = marker.getProperty("made", "").trim();
                    continue;
                }
                entriesSeen++;
                if (MARKER_NAME.equals(name))
                    throw new IOException("The backup has a second marker entry, " + label + ".");
                if (entry.isDirectory() || name.endsWith("/"))
                    throw new IOException("The entry " + label + " is a folder, which a backup never holds.");
                if (name.startsWith("/") || name.contains("\\") || name.contains("..")
                        || name.indexOf(':') >= 0)
                    throw new IOException("The entry " + label + " has an unsafe name.");
                Matcher match = PROFILE_ENTRY.matcher(name);
                boolean profileEntry = match.matches();
                if (!profileEntry && !CONFIG_NAME.equals(name) && !FAVORITES_NAME.equals(name)
                        && !LAYOUT_NAME.equals(name))
                    throw new IOException("The entry " + label + " is not part of a GOAT H1 backup.");
                if (!seen.add(name))
                    throw new IOException("The entry " + label + " appears twice in the backup.");
                if (profileEntry) {
                    String hash = match.group(1);
                    String file = match.group(2);
                    Profile profile = profiles.get(hash);
                    if (profile == null) {
                        if (profiles.size() >= PROFILE_CAP)
                            throw new IOException("The backup holds more than " + PROFILE_CAP
                                + " profiles; the entry " + label + " is one too many.");
                        profile = new Profile();
                        profiles.put(hash, profile);
                    }
                    if (META_NAME.equals(file)) {
                        byte[] meta = drain(zip, label, META_CAP_BYTES, null, buffer, counter).toByteArray();
                        profile.meta = true;
                        profile.name = profileName(meta);
                        if (staging != null && wantProfiles)
                            writeBytes(staging, stagingPrefix, name, meta);
                    } else {
                        if (!GAME_NAME.equals(file) && !FRIENDS_NAME.equals(file))
                            profile.blam++;
                        long cap = GAME_NAME.equals(file) ? GAME_CAP_BYTES
                            : FRIENDS_NAME.equals(file) ? FRIENDS_CAP_BYTES : BLAM_CAP_BYTES;
                        copyEntry(zip, staging, stagingPrefix, wantProfiles, name, label, cap, buffer, counter);
                    }
                } else if (CONFIG_NAME.equals(name)) {
                    sawConfig = true;
                    copyEntry(zip, staging, stagingPrefix, wantSettings, name, label, CONFIG_CAP_BYTES, buffer,
                        counter);
                } else if (FAVORITES_NAME.equals(name)) {
                    sawFavorites = true;
                    copyEntry(zip, staging, stagingPrefix, wantSettings, name, label, FAVORITES_CAP_BYTES,
                        buffer, counter);
                } else {
                    sawLayout = true;
                    byte[] text = drain(zip, label, LAYOUT_CAP_BYTES, null, buffer, counter).toByteArray();
                    if (wantLayout)
                        layoutText = new String(text, StandardCharsets.UTF_8);
                }
            }
        } catch (ZipException | EOFException | IllegalArgumentException e) {
            // IllegalArgumentException is what the zip reader throws for an entry name that is not UTF-8
            throw new IOException("The backup is damaged or ends early, near " + current + ".", e);
        }
        if (marker == null)
            throw new IOException("This file is not a GOAT H1 backup: it has no " + MARKER_NAME + " entry.");
        if (profiles.size() != expectedProfiles)
            throw new IOException("The backup ends early: it should hold " + expectedProfiles
                + " profiles but holds " + profiles.size() + ".");
        if (sawConfig != expectedSettings)
            throw new IOException("The backup ends early: " + CONFIG_NAME
                + (expectedSettings ? " is missing." : " is there but the marker does not list it."));
        if (sawFavorites != expectedFavorites)
            throw new IOException("The backup ends early: " + FAVORITES_NAME
                + (expectedFavorites ? " is missing." : " is there but the marker does not list it."));
        if (sawLayout != expectedLayout)
            throw new IOException("The backup ends early: " + LAYOUT_NAME
                + (expectedLayout ? " is missing." : " is there but the marker does not list it."));
        List<String> names = new ArrayList<>();
        List<String> hashes = new ArrayList<>();
        for (Map.Entry<String, Profile> each : profiles.entrySet()) {
            Profile profile = each.getValue();
            if (!profile.meta)
                throw new IOException("The profile " + each.getKey() + " in the backup is incomplete: "
                    + META_NAME + " is missing.");
            if (profile.blam != 1)
                throw new IOException("The profile " + profile.name + " in the backup is incomplete: it needs"
                    + " exactly one of " + SAVE_NAME + " and " + LIST_NAME + ".");
            names.add(profile.name);
            hashes.add(each.getKey());
        }
        if (entriesSeen < expectedEntries)
            throw new IOException("The backup ends early: " + entriesSeen + " of " + expectedEntries
                + " entries.");
        if (entriesSeen > expectedEntries)
            throw new IOException("The backup holds more entries than its marker lists: " + entriesSeen
                + " of " + expectedEntries + ".");
        return new Summary(names, hashes, profiles.size(), sawConfig, sawFavorites, sawLayout, build, made,
            counter.total, layoutText, 0);
    }

    private static Properties parseMarker(byte[] bytes) throws IOException {
        Properties marker = new Properties();
        try {
            marker.load(new ByteArrayInputStream(bytes));
        } catch (IllegalArgumentException e) {
            // a malformed unicode escape in the marker text
            throw new IOException("The backup's marker is damaged.", e);
        }
        if (!FORMAT.equals(marker.getProperty("format")))
            throw new IOException("This file is not a GOAT H1 backup: the marker does not say " + FORMAT + ".");
        int version;
        try {
            version = Integer.parseInt(marker.getProperty("version", "").trim());
        } catch (NumberFormatException e) {
            throw new IOException("The backup marker has no readable version.");
        }
        if (version > VERSION)
            throw new IOException("This backup was made by a newer version of the app (format " + version
                + "). Update the app to restore it.");
        markerCount(marker, "profiles", PROFILE_CAP);
        markerCount(marker, "entries", ENTRY_CAP);
        for (String key : new String[] {"settings", "touch-layout"}) {
            String value = marker.getProperty(key, "").trim();
            if (!"true".equals(value) && !"false".equals(value))
                throw new IOException("The backup marker has no readable " + key + " field.");
        }
        String favorites = marker.getProperty("favorites", "false").trim();
        if (!"true".equals(favorites) && !"false".equals(favorites))
            throw new IOException("The backup marker has no readable favorites field.");
        return marker;
    }

    /** A count field of the marker: a whole number from 0 to its limit. */
    private static int markerCount(Properties marker, String key, int limit) throws IOException {
        int count;
        try {
            count = Integer.parseInt(marker.getProperty(key, "").trim());
        } catch (NumberFormatException e) {
            throw new IOException("The backup marker has no readable " + key + " count.");
        }
        if (count < 0 || count > limit)
            throw new IOException("The backup marker lists " + count + " " + key + "; at most " + limit
                + " are allowed.");
        return count;
    }

    private static int parseOptionalInt(String text) {
        if (text == null)
            return 0;
        try {
            return Integer.parseInt(text.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /**
     * Reads the current entry to its end, counting what it inflates to; throws at the entry cap and at the
     * total cap. Returns the bytes when sink is null.
     */
    private static ByteArrayOutputStream drain(ZipInputStream zip, String name, long cap, OutputStream sink,
                                               byte[] buffer, Counter counter) throws IOException {
        ByteArrayOutputStream kept = sink == null ? new ByteArrayOutputStream() : null;
        long size = 0;
        int n;
        while ((n = zip.read(buffer)) > 0) {
            size += n;
            counter.total += n;
            if (size > cap)
                throw new IOException("The entry " + name + " is larger than " + cap
                    + " bytes, so the backup looks damaged.");
            if (counter.total > totalCapForTests)
                throw new IOException("The backup is larger than it can be (more than " + totalCapForTests
                    + " bytes inflated), at the entry " + name + ".");
            if (kept != null)
                kept.write(buffer, 0, n);
            else
                sink.write(buffer, 0, n);
        }
        return kept;
    }

    /** Counts an entry, and writes it into staging when staging is given and the part is wanted. */
    private static void copyEntry(ZipInputStream zip, File staging, String stagingPrefix, boolean wanted,
                                  String name, String label, long cap, byte[] buffer, Counter counter)
            throws IOException {
        if (staging == null || !wanted) {
            drain(zip, label, cap, new Discard(), buffer, counter);
            return;
        }
        File target = stagingTarget(stagingPrefix, staging, name, label);
        File temp = new File(target.getParentFile(), target.getName() + ".part");
        try {
            try (OutputStream out = new FileOutputStream(temp)) {
                drain(zip, label, cap, out, buffer, counter);
            }
            Files.move(temp.toPath(), target.toPath());
        } finally {
            temp.delete();
        }
    }

    private static void writeBytes(File staging, String stagingPrefix, String name, byte[] bytes)
            throws IOException {
        File target = stagingTarget(stagingPrefix, staging, name, shown(name));
        File temp = new File(target.getParentFile(), target.getName() + ".part");
        try {
            Files.write(temp.toPath(), bytes);
            Files.move(temp.toPath(), target.toPath());
        } finally {
            temp.delete();
        }
    }

    /** The file an entry goes to, after proving that it lies inside staging; its folder is made. */
    private static File stagingTarget(String stagingPrefix, File staging, String name, String label)
            throws IOException {
        File target = new File(staging, name);
        if (!target.getCanonicalPath().startsWith(stagingPrefix))
            throw new IOException("The entry " + label + " would land outside the restore folder.");
        File parent = target.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs())
            throw new IOException("Could not create " + parent.getPath() + ".");
        return target;
    }

    private static void rename(File from, File to) throws IOException {
        if (!from.renameTo(to))
            throw new IOException("Could not move " + from.getPath() + " to " + to.getPath() + ".");
    }

    /** Replaces a file; a plain rename over an existing file fails on Windows. */
    private static void moveReplacing(File from, File to) throws IOException {
        try {
            try {
                Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            // one sentence in the words rename() uses; the system's own text
            // differs between platforms and stays as the cause
            throw new IOException("Could not move " + from.getPath() + " to " + to.getPath() + ".", e);
        }
    }

    /** Deletes a file or a folder with everything in it; symbolic links are removed, never followed. */
    static void deleteRecursively(File file) {
        if (!Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children != null)
                for (File child : children)
                    deleteRecursively(child);
        }
        file.delete();
    }
}
