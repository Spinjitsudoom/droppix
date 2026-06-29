#include "macos_virtual_display.h"
#import <Foundation/Foundation.h>
#include <cstdlib>

// CGVirtualDisplay* is a private CoreGraphics API (no public headers). These
// @interface declarations only restate the existing ObjC runtime classes'
// public method/property surface so this file can call them; they don't
// reimplement anything. Surface confirmed against Chromium's
// ui/display/mac/test/virtual_display_mac_util.mm and the BetterDisplay/
// SimpleDisplay/Lumen OSS projects, which all depend on the same API.
@interface CGVirtualDisplayMode : NSObject
- (instancetype)initWithWidth:(unsigned int)width
                        height:(unsigned int)height
                   refreshRate:(double)refreshRate;
@property(readonly, nonatomic) unsigned int width;
@property(readonly, nonatomic) unsigned int height;
@property(readonly, nonatomic) double refreshRate;
@end

@interface CGVirtualDisplaySettings : NSObject
@property(strong, nonatomic) NSArray<CGVirtualDisplayMode*>* modes;
@property(nonatomic) unsigned int hiDPI;
@end

@interface CGVirtualDisplayDescriptor : NSObject
@property(nonatomic) unsigned int vendorID;
@property(nonatomic) unsigned int productID;
@property(nonatomic) unsigned int serialNum;
@property(strong, nonatomic) NSString* name;
@property(nonatomic) CGSize sizeInMillimeters;
@property(nonatomic) unsigned int maxPixelsWide;
@property(nonatomic) unsigned int maxPixelsHigh;
@property(nonatomic) CGPoint redPrimary;
@property(nonatomic) CGPoint greenPrimary;
@property(nonatomic) CGPoint bluePrimary;
@property(nonatomic) CGPoint whitePoint;
@property(strong, nonatomic) dispatch_queue_t queue;
@property(copy, nonatomic) void (^terminationHandler)(id, id);
@end

@interface CGVirtualDisplay : NSObject
- (instancetype)initWithDescriptor:(CGVirtualDisplayDescriptor*)descriptor;
@property(readonly, nonatomic) uint32_t displayID;
@property(readonly, nonatomic) NSArray<CGVirtualDisplayMode*>* modes;
- (BOOL)applySettings:(CGVirtualDisplaySettings*)settings;
@end

namespace droppix {

MacVirtualDisplay::~MacVirtualDisplay() { close(); }

bool MacVirtualDisplay::open(int width, int height, int refresh_hz) {
  close();
  if (refresh_hz > 60) refresh_hz = 60;  // hard API limit
  if (refresh_hz <= 0) refresh_hz = 60;

  CGVirtualDisplayDescriptor* desc = [[CGVirtualDisplayDescriptor alloc] init];
  desc.name = @"droppix";
  desc.vendorID = 0x3334;     // arbitrary, unregistered
  desc.productID = 0x4844;
  desc.serialNum = static_cast<unsigned int>(::rand());
  // Fixed "27-inch equivalent" physical size, matching what the SimpleDisplay
  // project found keeps pixel density under the API's rejection threshold up
  // to 4K; our resolutions are smaller so this is comfortably under it.
  desc.sizeInMillimeters = CGSizeMake(597, 336);
  desc.maxPixelsWide = static_cast<unsigned int>(width);
  desc.maxPixelsHigh = static_cast<unsigned int>(height);
  desc.redPrimary = CGPointMake(0.64f, 0.33f);
  desc.greenPrimary = CGPointMake(0.30f, 0.60f);
  desc.bluePrimary = CGPointMake(0.15f, 0.06f);
  desc.whitePoint = CGPointMake(0.3127f, 0.3290f);
  desc.queue = dispatch_get_main_queue();

  CGVirtualDisplay* display = [[CGVirtualDisplay alloc] initWithDescriptor:desc];
  if (!display) {
    descriptor_ = nullptr;
    return false;
  }

  CGVirtualDisplayMode* mode =
      [[CGVirtualDisplayMode alloc] initWithWidth:static_cast<unsigned int>(width)
                                            height:static_cast<unsigned int>(height)
                                       refreshRate:static_cast<double>(refresh_hz)];
  CGVirtualDisplaySettings* settings = [[CGVirtualDisplaySettings alloc] init];
  settings.modes = @[ mode ];
  settings.hiDPI = 0;

  if (![display applySettings:settings]) {
    return false;
  }

  display_id_ = display.displayID;
  // CFBridgingRetain hands the ObjC retain count to a plain void*; close()
  // gives it back with CFBridgingRelease so this header stays Objective-C-free.
  descriptor_ = (__bridge_retained void*)desc;
  display_ = (__bridge_retained void*)display;
  return true;
}

void MacVirtualDisplay::close() {
  if (display_) {
    CFBridgingRelease(display_);
    display_ = nullptr;
  }
  if (descriptor_) {
    CFBridgingRelease(descriptor_);
    descriptor_ = nullptr;
  }
  display_id_ = kCGNullDirectDisplay;
}

}  // namespace droppix
