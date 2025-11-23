#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface RNOverlayController : UIViewController

- (instancetype)initWithLaunchOptions:(NSDictionary *)launchOptions;
- (void)mountOverlayIfNeeded;
- (void)unmountOverlay;

@end

NS_ASSUME_NONNULL_END
