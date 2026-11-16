/*
DATA_IMPORT.M

The iOS app's first start (port/ios/host_app.m calls host_app_game_data
before the game starts): while the app's Documents have no maps folder of
the Xbox game, a screen of the app's own asks for the game data, as the
Android app's first start does (XisoExtractor.java) and the desktop's
(port/linux/src/sdl_platform.c):

- the disc image (.iso or .xiso), chosen with the system's document picker
  (Files, iCloud Drive, a USB drive, another app's storage) and read where
  it lies: its maps folder is extracted into Documents/maps by
  port/linux/src/xiso.c, the desktop builds' extractor, built into the
  host (tools/ios_build.py);
- or a maps folder, chosen the same way, whose .map files are copied;
- or the maps folder copied into OpenH1's folder by hand (the Files app,
  On My iPhone, or Finder's window of the device), then "Look again".

What is found is checked as the macOS app checks it: ui.map must be the
Xbox game's (a cache file of version 5; the PC version's and Custom
Edition's are refused, with the reason). A copy is written to maps.partial
and named maps only once it is whole (xiso.c does the same).

HALO_DATA_ROOT, or paths.data in config.toml, skips the screen: the game
then finds its data as it is told.
*/

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "xiso.h"
#include "map_unpack.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

/* xiso.c's log lines, the host's */
void platform_log(const char *format, ...)
{
	char line[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	host_logf(HOST_LOG_INFO, "%s", line);
}

/* whether folder holds the Xbox game's maps; else why not */
static BOOL maps_usable(NSString *folder, NSString **problem)
{
	NSString *ui = [folder stringByAppendingPathComponent:@"ui.map"];
	NSFileHandle *file = [NSFileHandle fileHandleForReadingAtPath:ui];
	NSData *header = [file readDataOfLength:8];
	uint32_t words[2];

	[file closeFile];
	if (header.length != sizeof(words))
	{
		*problem = @"There is no ui.map in it: it is not Halo’s maps folder.";
		return NO;
	}
	memcpy(words, header.bytes, sizeof(words));
	/* a cache file's header: 'head' and its version, 5 for the Xbox's (7
	for the PC version's, 609 for Custom Edition's) */
	if (words[0] != 0x68656164)
	{
		*problem = @"Its ui.map is not one of Halo’s maps.";
		return NO;
	}
	if (words[1] != 5)
	{
		*problem = @"These are the maps of Halo for PC or Halo Custom Edition. The game needs the maps of the "
			"Xbox game, Halo: Combat Evolved. (Custom Edition maps go in the custom_maps folder beside them.)";
		return NO;
	}
	return YES;
}

@interface HaloDataImport : UIViewController <UIDocumentPickerDelegate>
- (void)answer:(NSURL *)chosen;
@property (atomic) BOOL finished;
@property (nonatomic, copy) NSString *documents;
@end

@implementation HaloDataImport
{
	UILabel *message;
	UIProgressView *progress;
	UIStackView *buttons;
	BOOL picking_folder;
	_Atomic(double) fraction;
	NSTimer *timer;
}

- (UIButton *)button:(NSString *)title action:(SEL)action
{
	UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
	UIButtonConfiguration *configuration = [UIButtonConfiguration filledButtonConfiguration];

	configuration.title = title;
	configuration.baseBackgroundColor = [UIColor colorWithRed:0x9c / 255.0 green:0xd3 / 255.0 blue:0xff / 255.0
		alpha:1.0];
	configuration.baseForegroundColor = UIColor.blackColor;
	button.configuration = configuration;
	[button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
	[buttons addArrangedSubview:button];
	return button;
}

- (void)viewDidLoad
{
	UILabel *title = [UILabel new];
	UIStackView *stack = [UIStackView new];

	[super viewDidLoad];
	self.view.backgroundColor = UIColor.blackColor;
	title.text = @"GOAT H1 needs Halo’s maps";
	title.font = [UIFont systemFontOfSize:24 weight:UIFontWeightBold];
	title.textColor = UIColor.whiteColor;
	message = [UILabel new];
	message.numberOfLines = 0;
	message.textColor = [UIColor colorWithWhite:0.85 alpha:1.0];
	message.font = [UIFont systemFontOfSize:15];
	message.text = @"Choose your disc image of Halo: Combat Evolved for the Xbox (.iso or .xiso), and GOAT H1 copies "
		"its maps. Or choose the Xbox game’s maps folder. Or copy that folder into GOAT H1’s folder in the Files "
		"app (On My iPhone, GOAT H1), then look again. No game data comes with GOAT H1.";
	progress = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
	progress.hidden = YES;
	buttons = [UIStackView new];
	buttons.axis = UILayoutConstraintAxisHorizontal;
	buttons.spacing = 12;
	buttons.distribution = UIStackViewDistributionFillEqually;
	[self button:@"Disc image" action:@selector(chooseImage)];
	[self button:@"Maps folder" action:@selector(chooseFolder)];
	[self button:@"Look again" action:@selector(lookAgain)];
	stack.axis = UILayoutConstraintAxisVertical;
	stack.spacing = 18;
	stack.translatesAutoresizingMaskIntoConstraints = NO;
	for (UIView *view in @[ title, message, progress, buttons ])
		[stack addArrangedSubview:view];
	[self.view addSubview:stack];
	[NSLayoutConstraint activateConstraints:@[
		[stack.centerYAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor],
		[stack.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor constant:32],
		[stack.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor constant:-32],
	]];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
	return UIInterfaceOrientationMaskLandscape;
}

- (void)say:(NSString *)text
{
	message.text = text;
}

- (void)busy:(BOOL)busy
{
	buttons.userInteractionEnabled = !busy;
	buttons.alpha = busy ? 0.4 : 1.0;
	progress.hidden = !busy;
	[timer invalidate];
	timer = nil;
	if (busy)
	{
		atomic_store(&fraction, 0.0);
		progress.progress = 0.0f;
		timer = [NSTimer scheduledTimerWithTimeInterval:0.1 repeats:YES block:^(NSTimer *unused) {
			(void)unused;
			self->progress.progress = (float)atomic_load(&self->fraction);
		}];
	}
}

- (void)pick:(NSArray<UTType *> *)types folder:(BOOL)folder
{
	UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc]
		initForOpeningContentTypes:types asCopy:NO];

	picking_folder = folder;
	picker.delegate = self;
	picker.allowsMultipleSelection = NO;
	[self presentViewController:picker animated:YES completion:nil];
}

- (void)chooseImage
{
	/* (.xiso has no type of its own: any file, checked as it is read) */
	[self pick:@[ UTTypeDiskImage, UTTypeData ] folder:NO];
}

- (void)chooseFolder
{
	[self pick:@[ UTTypeFolder ] folder:YES];
}

- (NSString *)mapsFolder
{
	return [self.documents stringByAppendingPathComponent:@"maps"];
}

- (void)lookAgain
{
	NSString *problem = nil;

	if (maps_usable([self mapsFolder], &problem))
		self.finished = YES;
	else
		[self say:[NSString stringWithFormat:@"GOAT H1’s folder has no maps folder of the Xbox game yet. %@",
			[[NSFileManager defaultManager] fileExistsAtPath:[self mapsFolder]] ? problem : @""]];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
	NSURL *chosen = urls.firstObject;

	(void)controller;
	if (!chosen)
		return;
	if (picking_folder)
		[self copyFolder:chosen];
	else
		[self extractImage:chosen];
}

/* (HALO_IMPORT_ANSWER's) */
- (void)answer:(NSURL *)chosen
{
	BOOL directory = NO;

	[[NSFileManager defaultManager] fileExistsAtPath:chosen.path isDirectory:&directory];
	if (directory)
		[self copyFolder:chosen];
	else
		[self extractImage:chosen];
}

- (void)extractImage:(NSURL *)image
{
	BOOL accessing = [image startAccessingSecurityScopedResource];
	NSString *documents = self.documents;

	[self say:[NSString stringWithFormat:@"Copying the maps out of %@…", image.lastPathComponent]];
	[self busy:YES];
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		char error[512] = "";
		int succeeded = xiso_extract_maps(image.path.fileSystemRepresentation, documents.fileSystemRepresentation,
			extraction_progress, (__bridge void *)self, error, sizeof(error));

		/* (and the stock maps unpacked into the cache's slots, in the save
		root's z folder, on the same bar: port/linux/src/map_unpack.c; a
		failure leaves only slots the game copies again) */
		if (succeeded)
			unpack_maps(documents.fileSystemRepresentation, (__bridge void *)self);

		NSString *reason = succeeded ? nil : [NSString stringWithUTF8String:error];

		if (accessing)
			[image stopAccessingSecurityScopedResource];
		dispatch_async(dispatch_get_main_queue(), ^{
			[self busy:NO];
			[self finish:reason];
		});
	});
}

static void extraction_progress(void *context, const char *file, unsigned long long done, unsigned long long total);

/* the maps of <data root>/maps unpacked into <save root>/z, within the
cache's budget of the disk (game.map_cache_megabytes is the game's: the
disk's room here), for the game's language, English: another's slots are
not adopted, and are copied again */
static void unpack_maps(const char *data_root, void *context)
{
	const char *save_root = getenv("HALO_SAVE_ROOT");
	char maps_folder[1024], z_folder[1024];
	struct statvfs disk;
	unsigned long long room = 0, total = 0;

	if (!save_root || !save_root[0])
		return;
	snprintf(maps_folder, sizeof(maps_folder), "%s/maps", data_root);
	mkdir(save_root, 0755);
	snprintf(z_folder, sizeof(z_folder), "%s/z", save_root);
	if (statvfs(save_root, &disk) == 0)
	{
		room = (unsigned long long)disk.f_bavail * disk.f_frsize;
		total = (unsigned long long)disk.f_blocks * disk.f_frsize;
	}
	map_unpack_folder(maps_folder, z_folder, 1, map_cache_choose_budget(0, 0, room, total, map_unpack_least_budget()),
		extraction_progress, context);
}

static void extraction_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	HaloDataImport *import = (__bridge HaloDataImport *)context;

	(void)file;
	[import setFraction:total ? (double)done / (double)total : 0.0];
}

- (void)setFraction:(double)value
{
	atomic_store(&fraction, value);
}

- (void)copyFolder:(NSURL *)chosen
{
	BOOL accessing = [chosen startAccessingSecurityScopedResource];
	NSString *source = chosen.path, *problem = nil;
	NSFileManager *files = [NSFileManager defaultManager];

	/* (the folder that holds a maps folder, too) */
	if (![files fileExistsAtPath:[source stringByAppendingPathComponent:@"ui.map"]] &&
		[files fileExistsAtPath:[[source stringByAppendingPathComponent:@"maps"] stringByAppendingPathComponent:@"ui.map"]])
	{
		source = [source stringByAppendingPathComponent:@"maps"];
	}
	if (!maps_usable(source, &problem))
	{
		if (accessing)
			[chosen stopAccessingSecurityScopedResource];
		[self say:problem];
		return;
	}
	[self say:[NSString stringWithFormat:@"Copying the maps from %@…", chosen.lastPathComponent]];
	[self busy:YES];
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		NSString *partial = [self.documents stringByAppendingPathComponent:@"maps.partial"];
		NSArray<NSString *> *names = [files contentsOfDirectoryAtPath:source error:NULL];
		unsigned long long total = 0, done = 0;
		NSError *error = nil;
		BOOL succeeded = YES;

		[files removeItemAtPath:partial error:NULL];
		[files createDirectoryAtPath:partial withIntermediateDirectories:YES attributes:nil error:NULL];
		for (NSString *name in names)
			total += [[files attributesOfItemAtPath:[source stringByAppendingPathComponent:name] error:NULL] fileSize];
		for (NSString *name in names)
		{
			NSString *from = [source stringByAppendingPathComponent:name];
			BOOL directory = NO;

			if (![files fileExistsAtPath:from isDirectory:&directory] || directory)
				continue;
			if (![files copyItemAtPath:from toPath:[partial stringByAppendingPathComponent:name] error:&error])
			{
				succeeded = NO;
				break;
			}
			done += [[files attributesOfItemAtPath:from error:NULL] fileSize];
			[self setFraction:total ? (double)done / (double)total : 0.0];
		}
		if (succeeded)
		{
			[files removeItemAtPath:[self mapsFolder] error:NULL];
			succeeded = [files moveItemAtPath:partial toPath:[self mapsFolder] error:&error];
		}
		if (accessing)
			[chosen stopAccessingSecurityScopedResource];
		dispatch_async(dispatch_get_main_queue(), ^{
			[self busy:NO];
			[self finish:succeeded ? nil : [NSString stringWithFormat:@"The maps could not be copied: %@",
				error.localizedDescription]];
		});
	});
}

- (void)finish:(NSString *)error
{
	NSString *problem = nil;

	if (error)
	{
		[self say:error];
		return;
	}
	if (!maps_usable([self mapsFolder], &problem))
	{
		[self say:problem];
		return;
	}
	host_logf(HOST_LOG_INFO, "game data: the maps are in %s", [self mapsFolder].fileSystemRepresentation);
	self.finished = YES;
}

@end

/* (the main thread, before the game starts: port/ios/host_app.m) asks for
the game data while documents has no Xbox maps folder; returns once it has */
void host_app_game_data(const char *documents)
{
	NSString *folder = [NSString stringWithUTF8String:documents];
	NSString *problem = nil;
	UIWindowScene *scene = nil;
	UIWindow *window;
	HaloDataImport *import;

	if (maps_usable([folder stringByAppendingPathComponent:@"maps"], &problem))
		return;
	host_logf(HOST_LOG_INFO, "game data: none yet (%s): asking for it", problem.UTF8String);
	/* (the app's scene, once UIKit has connected it) */
	while (!scene)
	{
		for (UIScene *candidate in UIApplication.sharedApplication.connectedScenes)
		{
			if ([candidate isKindOfClass:[UIWindowScene class]])
				scene = (UIWindowScene *)candidate;
		}
		if (!scene)
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
	}
	import = [HaloDataImport new];
	import.documents = folder;
	window = [[UIWindow alloc] initWithWindowScene:scene];
	window.windowLevel = UIWindowLevelNormal + 2.0;
	window.rootViewController = import;
	[window makeKeyAndVisible];
	/* HALO_IMPORT_ANSWER=<disc image or maps folder> (tests): the screen
	answered with it a second after it shows, as if it had been picked */
	if (getenv("HALO_IMPORT_ANSWER") && *getenv("HALO_IMPORT_ANSWER"))
	{
		NSURL *answer = [NSURL fileURLWithPath:[NSString stringWithUTF8String:getenv("HALO_IMPORT_ANSWER")]];

		dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
			[import answer:answer];
		});
	}
	while (!import.finished)
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
	window.hidden = YES;
}
