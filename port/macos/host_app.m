/*
HOST_APP.M

The macOS app (OpenH1.app, tools/macos_build.py): what the host does when its
executable runs inside the app's bundle, before the game starts, so that it
behaves as a Mac app is expected to. A bare executable (build/macos/halo)
does none of it, and the HALO_ settings of the environment (HALO_DATA_ROOT,
HALO_SAVE_ROOT) and config.toml's paths.data and paths.saves are kept.

- The app's files are in ~/Library/Application Support/OpenH1 (the folder
  the game finds config.toml in, its data root and its saves) and the
  host's log in ~/Library/Logs/OpenH1 (halo.log, and the previous start's
  halo.previous.log), where Console shows it, unless the error output is a
  terminal. The Metal pipelines' archive is in ~/Library/Caches/OpenH1
  (xgpu_metal.m). ~ is HOME, so that a test or a script that sets HOME to
  a folder of its own keeps all three there, away from the player's.
- The game data: the players supply the Xbox game's disc image or its maps
  folder, whose maps are copied into the app's folder (its maps), as the
  Linux builds do: the game's data is the game's own, whatever becomes of
  the image or the folder, and wherever the app is. Without it, the game's
  first start (port/linux/src/first_start.c) shows in the game's window
  what it finds (in Downloads, on the Desktop, on drives, and the maps
  folders the app's earlier versions used: maps_used_before) and copies the
  one chosen, with its progress; Browse... there is the open panel, a sheet
  of the window (host_app_choose_game_data). So the player is never left
  with a dialog alone, behind other windows or in another Space, and a
  window that shows nothing.
- Links: the app is the handler of openh1:// links (Info.plist,
  tools/macos_app.py). macOS gives SDL's application delegate the link
  (the GetURL Apple event), running or started by it, and host_links.c
  keeps it for the guest's internet play.
- The window: its place and size, and whether it was fullscreen, are kept
  (window.plist) and the next start opens it so (display.fullscreen, through
  HALO_FULLSCREEN, and the window's frame once it is a window).

- Updates: a release's app updates itself with Sparkle (Sparkle.framework
  in the bundle, loaded at run time; tools/macos_app.py gives a release's
  Info.plist its appcast and public key). It looks when the game reaches
  its main menu and every four hours after, unless update.auto is false; a
  new version's window (its notes, Install Update, Remind Me Later, Skip
  This Version) is shown only at the main menu: found during a game, it
  waits until the game is over (the guest says which, updater.c's
  updater_poll). Installing replaces the app and starts it again; the
  player's files, here in Application Support, are untouched. The app's
  menu has Check for Updates... and Automatically Check for Updates, which
  is update.auto (written to config.toml; Settings > Network Setup > Check
  for Updates is the same setting).

SDL makes the app's menus (About, Hide, Quit, the Window menu with Toggle
Full Screen) and turns Quit into the game's quit event.
*/

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>
#include <SDL3/SDL.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "tomlc17.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* the folder's name, under Application Support, Logs and Caches (not Halo:
Halo for Mac, MacSoft's port, has ~/Library/Application Support/Halo) */
#define APP_FOLDER @"OpenH1"

static NSString *support_folder;
static char settings_folder[1024];

const char *host_app_settings_folder(void)
{
	return settings_folder[0] ? settings_folder : NULL;
}

/* ---------- folders */

/* HOME, which the tests and scripts point at a folder of their own (as the
Metal archive's folder follows it, host_metal.m), else the account's
(NSHomeDirectory, which does not read HOME) */
static NSString *home_folder(void)
{
	const char *home = getenv("HOME");

	if (home && home[0] == '/' && strcmp(home, "/"))
		return [NSString stringWithUTF8String:home];
	return NSHomeDirectory();
}

/* ---------- the log */

static void log_open(NSURL *logs)
{
	NSString *log = [logs URLByAppendingPathComponent:@"halo.log"].path;
	NSString *previous = [logs URLByAppendingPathComponent:@"halo.previous.log"].path;

	if (isatty(STDERR_FILENO))
		return;
	rename(log.fileSystemRepresentation, previous.fileSystemRepresentation);
	if (!freopen(log.fileSystemRepresentation, "w", stderr))
		return;
	dup2(STDERR_FILENO, STDOUT_FILENO);
	setvbuf(stderr, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);
}

/* ---------- config.toml's settings the app reads */

static NSString *config_path_setting(const char *name)
{
	NSString *path = [support_folder stringByAppendingPathComponent:@"config.toml"];
	toml_result_t result = toml_parse_file_ex(path.fileSystemRepresentation);
	NSString *value = nil;

	if (!result.ok)
		return nil;
	{
		toml_datum_t datum = toml_seek(result.toptab, name);

		if (datum.type == TOML_STRING && datum.u.s[0])
			value = [NSString stringWithUTF8String:datum.u.s];
	}
	toml_free(result);
	return value;
}

/* a boolean setting of config.toml, else its default */
static BOOL config_boolean_setting(const char *name, BOOL default_value)
{
	NSString *path = [support_folder stringByAppendingPathComponent:@"config.toml"];
	toml_result_t result = toml_parse_file_ex(path.fileSystemRepresentation);
	BOOL value = default_value;

	if (!result.ok)
		return value;
	{
		toml_datum_t datum = toml_seek(result.toptab, name);

		if (datum.type == TOML_BOOLEAN)
			value = datum.u.boolean ? YES : NO;
	}
	toml_free(result);
	return value;
}

/* ---------- the maps */

/* the folder before the project was OpenH1, under Application Support:
OpenCE's, which the app used until 2026-10-19 */
#define PREVIOUS_APP_FOLDER @"OpenCE"

static BOOL is_folder(NSString *path)
{
	BOOL directory = NO;

	return [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&directory] && directory;
}

/* whether url names a disc image: .iso or .xiso, in any case (and
halo1.xiso.iso) */
static BOOL is_disc_image(NSURL *url)
{
	NSString *extension = url.pathExtension.lowercaseString;

	return [extension isEqualToString:@"iso"] || [extension isEqualToString:@"xiso"];
}

/* the folder a bookmark file names, or nil */
static NSString *bookmark_folder(NSString *path)
{
	NSData *bookmark = [NSData dataWithContentsOfFile:path];
	BOOL stale = NO;

	if (!bookmark)
		return nil;
	return [NSURL URLByResolvingBookmarkData:bookmark
		options:NSURLBookmarkResolutionWithoutUI | NSURLBookmarkResolutionWithoutMounting relativeToURL:nil
		bookmarkDataIsStale:&stale error:NULL].path;
}

/* HALO_MAPS_USED_BEFORE, for the game's first start (game_data_scan.c): the
maps folders the app's earlier versions played, which it offers first, to
copy into the app's folder. Those versions used the player's maps folder
where it was, through a bookmark (maps.bookmark) and a link (maps) in the
app's folder, and OpenCE's folder before the project was OpenH1: an update
that stopped reading OpenCE's left its players without maps, and a folder
moved or a second copy of the app did the same. */
static void maps_used_before(void)
{
	NSString *own = [support_folder stringByAppendingPathComponent:@"maps"];
	NSString *previous = [[support_folder stringByDeletingLastPathComponent]
		stringByAppendingPathComponent:PREVIOUS_APP_FOLDER];
	NSMutableArray<NSString *> *found = [NSMutableArray array];
	NSDictionary *attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:own error:NULL];

	/* (the app's own maps folder, copied there: the bookmark is of no use) */
	if ([attributes.fileType isEqualToString:NSFileTypeDirectory])
	{
		[[NSFileManager defaultManager] removeItemAtPath:[support_folder
			stringByAppendingPathComponent:@"maps.bookmark"] error:NULL];
		return;
	}
	if (getenv("HALO_MAPS_USED_BEFORE"))
		return;
	for (NSString *folder in @[ support_folder, previous ])
	{
		NSString *bookmarked = bookmark_folder([folder stringByAppendingPathComponent:@"maps.bookmark"]);
		NSString *linked = [folder stringByAppendingPathComponent:@"maps"];
		NSString *target = [[NSFileManager defaultManager] destinationOfSymbolicLinkAtPath:linked error:NULL];

		/* (a link's folder, else the folder itself: as the player knows
		their paths, not as NSString resolves /tmp's) */
		if (target)
			linked = target.isAbsolutePath ? target : [folder stringByAppendingPathComponent:target];

		for (NSString *maps in @[ bookmarked ?: @"", linked ])
		{
			if (maps.length && is_folder(maps) && ![maps isEqualToString:own] && ![found containsObject:maps])
				[found addObject:maps];
		}
	}
	if (!found.count)
		return;
	for (NSString *maps in found)
		host_logf(HOST_LOG_INFO, "maps: an earlier version's: %s", maps.fileSystemRepresentation);
	setenv("HALO_MAPS_USED_BEFORE", [found componentsJoinedByString:@"\n"].fileSystemRepresentation, 1);
}

/* whether url names a Windows program: Custom Edition's installer
(halocesetup_en_1.00.exe), whose maps the first start adds to custom_maps
(port/linux/src/map_import.c) */
static BOOL is_program(NSURL *url)
{
	return [url.pathExtension.lowercaseString isEqualToString:@"exe"];
}

/* the open panel's choices: folders, and of the files only disc images and
Custom Edition's installer (the panel is given no types: .xiso has none of
its own, and would be greyed out) */
@interface HaloMapsPanelDelegate : NSObject <NSOpenSavePanelDelegate>
@end

@implementation HaloMapsPanelDelegate

- (BOOL)panel:(id)sender shouldEnableURL:(NSURL *)url
{
	(void)sender;
	return is_folder(url.path) || is_disc_image(url) || is_program(url);
}

@end

/* the first start's Browse... (port/linux/src/first_start.c, through
host_desktop.c's host_sdl_open_file_dialog): the open panel, a sheet of the
game's window (so that it shows over it, fullscreen or not, in its Space),
for the disc image or the maps folder. 1 with its path, 0 for none.
HALO_MAPS_PANEL_ANSWER, for the automated tests: the folder or disc image
(or "cancel") the panel is answered with, a second after it shows */
int host_app_choose_game_data(SDL_Window *window, char *path, uint32_t size)
{
	@autoreleasepool
	{
		NSOpenPanel *panel = [NSOpenPanel openPanel];
		HaloMapsPanelDelegate *delegate = [HaloMapsPanelDelegate new];
		NSWindow *parent = window ? (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL) : nil;
		NSString *downloads = [home_folder() stringByAppendingPathComponent:@"Downloads"];
		const char *test = getenv("HALO_MAPS_PANEL_ANSWER");
		NSString *answer = test && *test ? [NSString stringWithUTF8String:test] : nil;
		__block NSModalResponse response = NSModalResponseCancel;
		__block BOOL done = NO;
		NSString *picked;

		panel.canChooseDirectories = YES;
		panel.canChooseFiles = YES;
		panel.allowsMultipleSelection = NO;
		panel.delegate = delegate;
		panel.prompt = @"Use These Maps";
		panel.message = @"Choose the disc image (.iso or .xiso) or the maps folder of Halo: Combat Evolved for the "
			"Xbox, from your own copy of the game, or Custom Edition’s installer (halocesetup_en_1.00.exe) for its "
			"multiplayer maps.\nThe maps are copied into the game’s folder; the image, folder or installer stays as it "
			"is.";
		if (is_folder(downloads))
			panel.directoryURL = [NSURL fileURLWithPath:downloads isDirectory:YES];
		if (answer)
		{
			NSTimer *timer = [NSTimer timerWithTimeInterval:1.0 repeats:NO block:^(NSTimer *unused) {
				NSModalResponse code = [answer isEqualToString:@"cancel"] ? NSModalResponseCancel : NSModalResponseOK;

				host_logf(HOST_LOG_INFO, "test: the panel answered with %s", answer.UTF8String);
				if (parent)
					[parent endSheet:panel returnCode:code];
				else
					[NSApp stopModalWithCode:code];
			}];

			[[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
		}
		if (parent)
		{
			[panel beginSheetModalForWindow:parent completionHandler:^(NSModalResponse result) {
				response = result;
				done = YES;
			}];
			/* (the sheet answers through the window's events) */
			while (!done)
			{
				SDL_PumpEvents();
				SDL_Delay(20);
			}
		}
		else
		{
			[NSApp activate];
			response = [panel runModal];
		}
		path[0] = 0;
		if (response != NSModalResponseOK)
			return 0;
		picked = answer ?: panel.URL.path;
		if (!picked.length)
			return 0;
		snprintf(path, size, "%s", picked.fileSystemRepresentation);
		return 1;
	}
}

/* ---------- the window */

static NSString *window_path(void)
{
	return [support_folder stringByAppendingPathComponent:@"window.plist"];
}

static NSMutableDictionary *window_state;
static BOOL window_placed;
static unsigned long window_writes;

static BOOL is_game_window(NSWindow *window)
{
	return [window isKindOfClass:NSClassFromString(@"SDL3Window")];
}

static void window_save_soon(void)
{
	unsigned long write = ++window_writes;

	/* (once a moving or resizing window rests) */
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{
		if (write == window_writes)
			[window_state writeToFile:window_path() atomically:YES];
	});
}

static void window_frame_saved(NSWindow *window)
{
	if (!window_placed || !window.visible || (window.styleMask & NSWindowStyleMaskFullScreen))
		return;
	window_state[@"frame"] = NSStringFromRect(window.frame);
	window_save_soon();
}

/* whether the window is fullscreen (either kind), as SDL has it once the
window rests, kept for the next start: a window only when the player made
it one (F11, Toggle Full Screen). The Space's own notifications also come
when the game leaves it for display.mode's fullscreen, which keeps SDL's
flag, and when the window closes at quit; a NO from those made the next
start's default (display.mode empty) a window, where display.resolution
does not apply. */
static void window_fullscreen_saved(NSWindow *window)
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2), dispatch_get_main_queue(), ^{
		SDL_Window **windows = SDL_GetWindows(NULL);
		NSNumber *fullscreen = nil;
		int index;

		for (index = 0; windows && windows[index]; index++)
		{
			if (SDL_GetPointerProperty(SDL_GetWindowProperties(windows[index]), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
				NULL) == (__bridge void *)window)
			{
				fullscreen = SDL_GetWindowFlags(windows[index]) & SDL_WINDOW_FULLSCREEN ? @YES : @NO;
			}
		}
		SDL_free(windows);
		if (!fullscreen || !window.visible || [fullscreen isEqual:window_state[@"fullscreen"]])
			return;
		window_state[@"fullscreen"] = fullscreen;
		window_save_soon();
	});
}

/* the last frame, where a screen still shows it */
static void window_place(NSWindow *window)
{
	NSString *saved = window_state[@"frame"];
	NSRect frame;

	if (window_placed || (window.styleMask & NSWindowStyleMaskFullScreen))
		return;
	window_placed = YES;
	if (!saved)
		return;
	frame = NSRectFromString(saved);
	for (NSScreen *screen in [NSScreen screens])
	{
		if (NSIntersectsRect(NSInsetRect(frame, 40, 40), screen.visibleFrame))
		{
			[window setFrame:frame display:YES];
			return;
		}
	}
}

static void window_remember(void)
{
	NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
	NSOperationQueue *main = [NSOperationQueue mainQueue];
	NSNumber *fullscreen;

	window_state = [NSMutableDictionary dictionaryWithContentsOfFile:window_path()] ?: [NSMutableDictionary dictionary];
	fullscreen = window_state[@"fullscreen"];
	if (fullscreen)
		setenv("HALO_FULLSCREEN", fullscreen.boolValue ? "true" : "false", 0);
	/* (each after SDL's own handling of the change) */
	[center addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:main usingBlock:^(NSNotification *note) {
		NSWindow *window = note.object;

		if (is_game_window(window))
			dispatch_async(dispatch_get_main_queue(), ^{ window_place(window); });
	}];
	[center addObserverForName:NSWindowDidExitFullScreenNotification object:nil queue:main usingBlock:^(NSNotification *note) {
		NSWindow *window = note.object;

		if (!is_game_window(window))
			return;
		window_fullscreen_saved(window);
		dispatch_async(dispatch_get_main_queue(), ^{
			/* (a game that started fullscreen: the window now) */
			window_place(window);
			window_frame_saved(window);
		});
	}];
	[center addObserverForName:NSWindowDidEnterFullScreenNotification object:nil queue:main usingBlock:^(NSNotification *note) {
		if (is_game_window(note.object))
			window_fullscreen_saved(note.object);
	}];
	for (NSNotificationName name in @[ NSWindowDidMoveNotification, NSWindowDidResizeNotification ])
	{
		[center addObserverForName:name object:nil queue:main usingBlock:^(NSNotification *note) {
			if (is_game_window(note.object))
				window_frame_saved(note.object);
		}];
	}
	/* (the first start's window closed: the game's, next, is placed too) */
	[center addObserverForName:NSWindowWillCloseNotification object:nil queue:main usingBlock:^(NSNotification *note) {
		if (is_game_window(note.object))
			window_placed = NO;
	}];
}

/* ---------- updates (Sparkle) */

/* port/linux/host/host_desktop.c's: 1 while a game is played, 0 at the main
menu, -1 before the game's first frame (updater.c's updater_poll) */
int host_app_is_playing(void);

@interface HaloUpdater : NSObject <SPUUpdaterDelegate, SPUStandardUserDriverDelegate, NSMenuItemValidation>
@end

static SPUStandardUpdaterController *updater_controller;
static HaloUpdater *updater_delegate;
/* a new version found during a game, to be shown at the main menu */
static BOOL update_waiting;
/* the check of the start, made at the first main menu */
static BOOL update_start_checked;
/* config.toml's modification date when update.auto was last read */
static NSDate *update_config_read;

static NSString *config_path(void)
{
	return [support_folder stringByAppendingPathComponent:@"config.toml"];
}

static NSDate *config_modified(void)
{
	return [[NSFileManager defaultManager] attributesOfItemAtPath:config_path() error:NULL].fileModificationDate;
}

/* update.auto = on written to config.toml as the game writes a setting:
its line in [update] (or its commented default) replaced, else added. The
game reads the file again before it writes any setting, and keeps this */
static void config_write_update_auto(BOOL on)
{
	NSCharacterSet *blank = [NSCharacterSet whitespaceCharacterSet];
	NSString *text = [NSString stringWithContentsOfFile:config_path() encoding:NSUTF8StringEncoding error:NULL] ?: @"";
	NSMutableArray<NSString *> *lines = [[text componentsSeparatedByString:@"\n"] mutableCopy];
	NSString *line = on ? @"auto = true" : @"auto = false";
	NSUInteger section = NSNotFound, index;
	BOOL replaced = NO;

	for (index = 0; index < lines.count; index++)
	{
		NSString *trimmed = [lines[index] stringByTrimmingCharactersInSet:blank];

		if ([trimmed hasPrefix:@"["])
		{
			if (section != NSNotFound)
				break;
			if ([trimmed isEqualToString:@"[update]"])
				section = index;
			continue;
		}
		if (section == NSNotFound)
			continue;
		if ([trimmed hasPrefix:@"#"])
			trimmed = [[trimmed substringFromIndex:1] stringByTrimmingCharactersInSet:blank];
		if ([trimmed hasPrefix:@"auto"] &&
			[[[trimmed substringFromIndex:4] stringByTrimmingCharactersInSet:blank] hasPrefix:@"="])
		{
			lines[index] = line;
			replaced = YES;
			break;
		}
	}
	if (!replaced && section == NSNotFound)
	{
		if (lines.count && [lines.lastObject length] == 0)
			[lines removeLastObject];
		[lines addObjectsFromArray:@[ @"", @"[update]", line, @"" ]];
	}
	else if (!replaced)
		[lines insertObject:line atIndex:section + 1];
	if (![[lines componentsJoinedByString:@"\n"] writeToFile:config_path() atomically:YES encoding:NSUTF8StringEncoding
		error:NULL])
	{
		host_logf(HOST_LOG_WARN, "update: could not write update.auto to config.toml");
	}
}

/* Sparkle's windows over the game's when it is fullscreen (in its Space),
not in a Space of their own */
static void update_windows_over_game(void)
{
	dispatch_async(dispatch_get_main_queue(), ^{
		for (NSWindow *window in NSApp.windows)
		{
			if (is_game_window(window) || !window.visible)
				continue;
			window.collectionBehavior |= NSWindowCollectionBehaviorFullScreenAuxiliary |
				NSWindowCollectionBehaviorMoveToActiveSpace;
			[window orderFrontRegardless];
		}
	});
}

static BOOL update_test_install(void)
{
	const char *test = getenv("HALO_UPDATE_TEST");

	return test && !strcmp(test, "install");
}

@implementation HaloUpdater

/* (the appcast of a test: HALO_UPDATE_FEED, port/macos/README.md) */
- (NSString *)feedURLStringForUpdater:(SPUUpdater *)updater
{
	const char *feed = getenv("HALO_UPDATE_FEED");

	return feed && *feed ? [NSString stringWithUTF8String:feed] : nil;
}

- (void)updater:(SPUUpdater *)updater didFindValidUpdate:(SUAppcastItem *)item
{
	host_logf(HOST_LOG_INFO, "update: build %s is out", item.versionString.UTF8String);
}

- (void)updaterDidNotFindUpdate:(SPUUpdater *)updater
{
	host_logf(HOST_LOG_INFO, "update: this is the latest build");
}

- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
	host_logf(HOST_LOG_INFO, "update: %s", error.localizedDescription.UTF8String);
}

- (void)updaterWillRelaunchApplication:(SPUUpdater *)updater
{
	host_logf(HOST_LOG_INFO, "update: installed; starting the new build");
}

/* (HALO_UPDATE_TEST=install, port/macos/README.md: a downloaded update
installed at once, as Install Update does, but the app not started again,
which LaunchServices would do without the test's environment) */
- (BOOL)updater:(SPUUpdater *)updater willInstallUpdateOnQuit:(SUAppcastItem *)item
	immediateInstallationBlock:(void (^)(void))immediateInstallHandler
{
	host_logf(HOST_LOG_INFO, "update: build %s downloaded and checked", item.versionString.UTF8String);
	if (!update_test_install())
		return NO;
	immediateInstallHandler();
	return YES;
}

/* (never in a test: LaunchServices would start the new app without the
test's environment, in the player's own folders) */
- (BOOL)updaterShouldRelaunchApplication:(SPUUpdater *)updater
{
	return !getenv("HALO_UPDATE_TEST");
}

/* a version found by a check of Sparkle's own (not Check for Updates...):
shown by Sparkle at the main menu, else kept for it (update_tick) */
- (BOOL)supportsGentleScheduledUpdateReminders
{
	return YES;
}

- (BOOL)standardUserDriverShouldHandleShowingScheduledUpdate:(SUAppcastItem *)update andInImmediateFocus:(BOOL)immediateFocus
{
	return host_app_is_playing() == 0;
}

- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate forUpdate:(SUAppcastItem *)update
	state:(SPUUserUpdateState *)state
{
	if (handleShowingUpdate)
	{
		host_logf(HOST_LOG_INFO, "update: build %s shown", update.versionString.UTF8String);
		update_windows_over_game();
		return;
	}
	update_waiting = YES;
	host_logf(HOST_LOG_INFO, "update: build %s shown at the main menu, after this game",
		update.versionString.UTF8String);
}

- (void)standardUserDriverDidReceiveUserAttentionForUpdate:(SUAppcastItem *)update
{
	update_waiting = NO;
}

- (void)standardUserDriverWillFinishUpdateSession
{
	update_waiting = NO;
}

/* the app menu's Automatically Check for Updates: update.auto */
- (void)toggleAutomaticChecks:(NSMenuItem *)item
{
	BOOL on = !updater_controller.updater.automaticallyChecksForUpdates;

	updater_controller.updater.automaticallyChecksForUpdates = on;
	config_write_update_auto(on);
	update_config_read = config_modified();
	host_logf(HOST_LOG_INFO, "update: update.auto = %s", on ? "true" : "false");
}

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
	if (item.action == @selector(toggleAutomaticChecks:))
	{
		item.state = updater_controller.updater.automaticallyChecksForUpdates ? NSControlStateValueOn
			: NSControlStateValueOff;
	}
	return YES;
}

@end

/* every two seconds, on the main thread (between the game's frames, which
pump its run loop): update.auto as Settings changes it, the start's check
at the first main menu, and a version found during a game shown once the
game is over */
static void update_tick(void)
{
	SPUUpdater *updater = updater_controller.updater;
	NSDate *modified = config_modified();

	if (modified && ![modified isEqualToDate:update_config_read])
	{
		BOOL on = config_boolean_setting("update.auto", YES);

		update_config_read = modified;
		if (on != updater.automaticallyChecksForUpdates)
		{
			updater.automaticallyChecksForUpdates = on;
			host_logf(HOST_LOG_INFO, "update: update.auto = %s", on ? "true" : "false");
		}
	}
	if (host_app_is_playing() != 0)
		return;
	if (!update_start_checked)
	{
		update_start_checked = YES;
		if (updater.automaticallyChecksForUpdates && updater.canCheckForUpdates)
			[updater checkForUpdatesInBackground];
	}
	if (update_waiting)
	{
		update_waiting = NO;
		[updater_controller checkForUpdates:nil];
		update_windows_over_game();
	}
}

/* once SDL has made the app's menus (host_app_start's timer):
Sparkle, if the app has it and an appcast (a release's), else nothing */
static void update_start(void)
{
	NSBundle *bundle = [NSBundle mainBundle];
	NSBundle *sparkle = [NSBundle bundleWithPath:[bundle.privateFrameworksPath
		stringByAppendingPathComponent:@"Sparkle.framework"]];
	BOOL test = getenv("HALO_UPDATE_FEED") && *getenv("HALO_UPDATE_FEED");
	NSMenu *menu = NSApp.mainMenu.itemArray.firstObject.submenu;
	NSError *error = nil;
	Class controller;

	if (updater_controller)
		return;
	if (![bundle objectForInfoDictionaryKey:@"SUPublicEDKey"] || (!test && ![bundle objectForInfoDictionaryKey:@"SUFeedURL"]))
	{
		host_logf(HOST_LOG_INFO, "update: not looked for (a release's app looks: this build has no appcast)");
		return;
	}
	if (!sparkle || ![sparkle loadAndReturnError:&error] || !(controller = NSClassFromString(@"SPUStandardUpdaterController")))
	{
		host_logf(HOST_LOG_WARN, "update: no Sparkle (%s)", error ? error.localizedDescription.UTF8String : "");
		return;
	}
	updater_delegate = [HaloUpdater new];
	updater_controller = [[controller alloc] initWithStartingUpdater:NO updaterDelegate:updater_delegate
		userDriverDelegate:updater_delegate];
	[updater_controller startUpdater];
	updater_controller.updater.automaticallyChecksForUpdates = config_boolean_setting("update.auto", YES);
	if (update_test_install())
		updater_controller.updater.automaticallyDownloadsUpdates = YES;
	update_config_read = config_modified();
	host_logf(HOST_LOG_INFO, "update: Sparkle started (%s), update.auto = %s",
		updater_controller.updater.feedURL.absoluteString.UTF8String,
		updater_controller.updater.automaticallyChecksForUpdates ? "true" : "false");
	if (menu)
	{
		NSMenuItem *check = [[NSMenuItem alloc] initWithTitle:@"Check for Updates\u2026"
			action:@selector(checkForUpdates:) keyEquivalent:@""];
		NSMenuItem *automatic = [[NSMenuItem alloc] initWithTitle:@"Automatically Check for Updates"
			action:@selector(toggleAutomaticChecks:) keyEquivalent:@""];

		check.target = updater_controller;
		automatic.target = updater_delegate;
		[menu insertItem:[NSMenuItem separatorItem] atIndex:1];
		[menu insertItem:check atIndex:2];
		[menu insertItem:automatic atIndex:3];
	}
	/* (the tests, HALO_UPDATE_TEST: window, the update window at once, as
	Check for Updates... shows it; install, the update looked for at once,
	downloaded and installed: whatever the game is doing) */
	if (update_test_install())
		[updater_controller.updater checkForUpdatesInBackground];
	else if (getenv("HALO_UPDATE_TEST") && !strcmp(getenv("HALO_UPDATE_TEST"), "window"))
		[updater_controller checkForUpdates:nil];
}

/* ---------- start */

void host_app_start(void)
{
	@autoreleasepool
	{
		NSBundle *bundle = [NSBundle mainBundle];
		NSFileManager *files = [NSFileManager defaultManager];
		NSURL *library = [NSURL fileURLWithPath:[home_folder() stringByAppendingPathComponent:@"Library"]
			isDirectory:YES];
		NSURL *support = [[library URLByAppendingPathComponent:@"Application Support"]
			URLByAppendingPathComponent:APP_FOLDER];
		NSURL *logs = [[library URLByAppendingPathComponent:@"Logs"] URLByAppendingPathComponent:APP_FOLDER];
		NSString *brokers;
		NSString *saves;

		if (!bundle.bundleIdentifier || ![bundle.bundlePath.pathExtension isEqualToString:@"app"])
			return;
		[files createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:NULL];
		[files createDirectoryAtURL:logs withIntermediateDirectories:YES attributes:nil error:NULL];
		log_open(logs);
		support_folder = support.path;
		snprintf(settings_folder, sizeof(settings_folder), "%s/", support_folder.fileSystemRepresentation);
		host_logf(HOST_LOG_INFO, "%s %s (%s), %s", [[bundle objectForInfoDictionaryKey:@"CFBundleName"] UTF8String],
			[[bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] UTF8String],
			bundle.bundleIdentifier.UTF8String, bundle.bundlePath.fileSystemRepresentation);
		host_logf(HOST_LOG_INFO, "settings and saves: %s", settings_folder);

		/* internet play's brokers (network.brokers_file), the app's list
		beside config.toml at each start, as an update replaces the one
		beside the desktop builds' executable */
		brokers = [bundle pathForResource:@"brokers" ofType:@"txt"];
		if (brokers)
		{
			NSString *copy = [support_folder stringByAppendingPathComponent:@"brokers.txt"];

			[files removeItemAtPath:copy error:NULL];
			[files copyItemAtPath:brokers toPath:copy error:NULL];
		}
		/* ...and the stock maps' preparations' records (port/linux/src/
		prepared_shipped.h), the same way */
		{
			NSString *records = [bundle pathForResource:@"prepared" ofType:@"records"];

			if (records)
			{
				NSString *copy = [support_folder stringByAppendingPathComponent:@"prepared.records"];

				[files removeItemAtPath:copy error:NULL];
				[files copyItemAtPath:records toPath:copy error:NULL];
			}
		}
		/* ...and the textures', fonts' and menus' bytes (port/linux/src/
		embedded_assets.h), the same way */
		{
			NSString *assets = [bundle pathForResource:@"openh1" ofType:@"assets"];

			if (assets)
			{
				NSString *copy = [support_folder stringByAppendingPathComponent:@"openh1.assets"];

				[files removeItemAtPath:copy error:NULL];
				[files copyItemAtPath:assets toPath:copy error:NULL];
			}
		}

		saves = [support_folder stringByAppendingPathComponent:@"saves"];
		if (!getenv("HALO_SAVE_ROOT") && !config_path_setting("paths.saves"))
			setenv("HALO_SAVE_ROOT", saves.fileSystemRepresentation, 1);
		/* (the data root is this folder, config.toml's, unless paths.data
		says another: port/linux/src/xbox_files.c; without maps there, the
		game's first start) */
		maps_used_before();
		window_remember();
	}
}

/* host_sdl.c's, each time the game reads its events (on the main thread):
the updater started once the game reads them, and its tick every two
seconds. (Not a timer of the run loop: the game's event pump does not run
them, and NSApplicationDidFinishLaunching does not come) */
void host_app_update_tick(void)
{
	static Uint64 next;
	static BOOL started;
	Uint64 now = SDL_GetTicks();

	if (!support_folder || now < next)
		return;
	next = now + 2000;
	@autoreleasepool
	{
		if (!started)
		{
			started = YES;
			update_start();
		}
		if (updater_controller)
			update_tick();
	}
}
