#import "ARMSXModule.h"
#import "AppDelegate.h"
#import <React/RCTUtils.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface ARMSXModule ()
@property(nonatomic, copy) RCTPromiseResolveBlock pendingResolve;
@property(nonatomic, copy) RCTPromiseRejectBlock pendingReject;
@end

@implementation ARMSXModule

RCT_EXPORT_MODULE();

RCT_EXPORT_METHOD(loadEmu:(NSArray<NSString *> *)args
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self forceLandscapeOrientation];

        AppDelegate *delegate = (AppDelegate *)[UIApplication sharedApplication].delegate;
        if (!delegate) {
            reject(@"no_delegate", @"AppDelegate unavailable", nil);
            return;
        }

        [delegate startSDLWithArgs:args ?: @[]];
        resolve(@(YES));
    });
}

RCT_EXPORT_METHOD(forceLandscape:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self forceLandscapeOrientation];
        resolve(@(YES));
    });
}

RCT_EXPORT_METHOD(pickPath:(NSString *)kind
                  resolver:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.pendingResolve) {
            self.pendingReject(@"picker_busy", @"A picker is already active", nil);
            self.pendingResolve = nil;
            self.pendingReject = nil;
        }

        self.pendingResolve = resolve;
        self.pendingReject = reject;

        NSArray *types;
        if (@available(iOS 14.0, *)) {
            types = @[UTTypeData];
        } else {
            types = @[@"public.data"];
        }

        UIDocumentPickerViewController *picker;
        if (@available(iOS 14.0, *)) {
            picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:types asCopy:NO];
        } else {
            picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:types inMode:UIDocumentPickerModeOpen];
        }

        picker.allowsMultipleSelection = NO;
        picker.delegate = self;
        picker.modalPresentationStyle = UIModalPresentationFormSheet;

        UIViewController *root = RCTPresentedViewController();
        if (root) {
            [root presentViewController:picker animated:YES completion:nil];
        } else {
            self.pendingReject(@"no_view_controller", @"Unable to present picker", nil);
            self.pendingResolve = nil;
            self.pendingReject = nil;
        }
    });
}

RCT_EXPORT_METHOD(ensureGameFolder:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    NSError *error = nil;
    NSURL *url = [self gamesDirectoryURLWithError:&error];
    if (error || !url) {
        reject(@"folder_error", @"Unable to create games folder", error);
        return;
    }
    resolve([url path]);
}

RCT_EXPORT_METHOD(listGames:(RCTPromiseResolveBlock)resolve
                  rejecter:(RCTPromiseRejectBlock)reject) {
    NSError *error = nil;
    NSURL *dir = [self gamesDirectoryURLWithError:&error];
    if (error || !dir) {
        reject(@"folder_error", @"Unable to access games folder", error);
        return;
    }

    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray *contents = [fm contentsOfDirectoryAtURL:dir
                          includingPropertiesForKeys:@[NSURLIsDirectoryKey]
                                             options:0
                                               error:&error];

    if (error) {
        reject(@"list_error", @"Unable to read games folder", error);
        return;
    }

    NSMutableArray *games = [NSMutableArray array];
    NSSet *allowedExts = [NSSet setWithArray:@[@"bin", @"cue", @"iso", @"img", @"pbp"]];

    for (NSURL *item in contents) {
        NSNumber *isDir = nil;
        [item getResourceValue:&isDir forKey:NSURLIsDirectoryKey error:nil];
        if ([isDir boolValue]) continue;

        NSString *ext = [[item pathExtension] lowercaseString];
        if (![allowedExts containsObject:ext]) continue;

        [games addObject:@{
            @"name": [item lastPathComponent],
            @"path": [item path]
        }];
    }

    resolve(games);
}

- (NSURL *)gamesDirectoryURLWithError:(NSError **)errorPtr {
    NSFileManager *fm = [NSFileManager defaultManager];
    NSURL *docs = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *gamesDir = [docs URLByAppendingPathComponent:@"ARMSX/Games" isDirectory:YES];
    if (![fm fileExistsAtPath:[gamesDir path]]) {
        [fm createDirectoryAtURL:gamesDir withIntermediateDirectories:YES attributes:nil error:errorPtr];
    }
    return gamesDir;
}

#pragma mark - UIDocumentPickerDelegate

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (self.pendingResolve) {
        self.pendingResolve(nil);
    }
    self.pendingResolve = nil;
    self.pendingReject = nil;
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *picked = [urls firstObject];
    if (picked && self.pendingResolve) {
        self.pendingResolve([picked path]);
    } else if (self.pendingReject) {
        self.pendingReject(@"no_selection", @"No file selected", nil);
    }
    self.pendingResolve = nil;
    self.pendingReject = nil;
}

- (void)forceLandscapeOrientation {
    NSNumber *value = @(UIInterfaceOrientationLandscapeRight);
    [[UIDevice currentDevice] setValue:value forKey:@"orientation"];
    [UIViewController attemptRotationToDeviceOrientation];
}

@end
