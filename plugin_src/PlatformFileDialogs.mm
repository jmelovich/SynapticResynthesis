/**
 * @file PlatformFileDialogs.mm
 * @brief macOS implementation of native file dialogs using Cocoa
 */

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#include "PlatformFileDialogs.h"

namespace platform
{
  bool ShowMacSavePanel_ObjC(std::string& outPath, const std::string& defaultFileName, const std::vector<std::string>& extensions)
  {
    __block bool result = false;
    __block std::string resultPath;
    
    // File dialogs must run on the main thread
    if ([NSThread isMainThread])
    {
      @autoreleasepool
      {
        NSSavePanel* savePanel = [NSSavePanel savePanel];
        
        // Set default filename
        if (!defaultFileName.empty())
        {
          [savePanel setNameFieldStringValue:[NSString stringWithUTF8String:defaultFileName.c_str()]];
        }
        
        // Set allowed file types (extensions)
        if (!extensions.empty())
        {
          NSMutableArray* allowedTypes = [NSMutableArray arrayWithCapacity:extensions.size()];
          for (const auto& ext : extensions)
          {
            [allowedTypes addObject:[NSString stringWithUTF8String:ext.c_str()]];
          }
          [savePanel setAllowedFileTypes:allowedTypes];
        }
        
        // Show dialog
        NSModalResponse response = [savePanel runModal];
        
        if (response == NSModalResponseOK)
        {
          NSURL* url = [savePanel URL];
          if (url)
          {
            const char* pathCStr = [[url path] UTF8String];
            if (pathCStr)
            {
              resultPath = pathCStr;
              result = true;
            }
          }
        }
      }
    }
    else
    {
      // If not on main thread, dispatch synchronously to main thread
      dispatch_sync(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
          NSSavePanel* savePanel = [NSSavePanel savePanel];
          
          // Set default filename
          if (!defaultFileName.empty())
          {
            [savePanel setNameFieldStringValue:[NSString stringWithUTF8String:defaultFileName.c_str()]];
          }
          
          // Set allowed file types (extensions)
          if (!extensions.empty())
          {
            NSMutableArray* allowedTypes = [NSMutableArray arrayWithCapacity:extensions.size()];
            for (const auto& ext : extensions)
            {
              [allowedTypes addObject:[NSString stringWithUTF8String:ext.c_str()]];
            }
            [savePanel setAllowedFileTypes:allowedTypes];
          }
          
          // Show dialog
          NSModalResponse response = [savePanel runModal];
          
          if (response == NSModalResponseOK)
          {
            NSURL* url = [savePanel URL];
            if (url)
            {
              const char* pathCStr = [[url path] UTF8String];
              if (pathCStr)
              {
                resultPath = pathCStr;
                result = true;
              }
            }
          }
        }
      });
    }
    
    if (result)
    {
      outPath = resultPath;
    }
    
    return result;
  }

  bool ShowMacOpenPanel_ObjC(std::string& outPath, const std::vector<std::string>& extensions)
  {
    __block bool result = false;
    __block std::string resultPath;
    
    // File dialogs must run on the main thread
    if ([NSThread isMainThread])
    {
      @autoreleasepool
      {
        NSOpenPanel* openPanel = [NSOpenPanel openPanel];
        
        [openPanel setCanChooseFiles:YES];
        [openPanel setCanChooseDirectories:NO];
        [openPanel setAllowsMultipleSelection:NO];
        
        // Set allowed file types (extensions)
        if (!extensions.empty())
        {
          NSMutableArray* allowedTypes = [NSMutableArray arrayWithCapacity:extensions.size()];
          for (const auto& ext : extensions)
          {
            [allowedTypes addObject:[NSString stringWithUTF8String:ext.c_str()]];
          }
          [openPanel setAllowedFileTypes:allowedTypes];
        }
        
        // Show dialog
        NSModalResponse response = [openPanel runModal];
        
        if (response == NSModalResponseOK)
        {
          NSArray* urls = [openPanel URLs];
          if ([urls count] > 0)
          {
            NSURL* url = [urls objectAtIndex:0];
            const char* pathCStr = [[url path] UTF8String];
            if (pathCStr)
            {
              resultPath = pathCStr;
              result = true;
            }
          }
        }
      }
    }
    else
    {
      // If not on main thread, dispatch synchronously to main thread
      dispatch_sync(dispatch_get_main_queue(), ^{
        @autoreleasepool
        {
          NSOpenPanel* openPanel = [NSOpenPanel openPanel];
          
          [openPanel setCanChooseFiles:YES];
          [openPanel setCanChooseDirectories:NO];
          [openPanel setAllowsMultipleSelection:NO];
          
          // Set allowed file types (extensions)
          if (!extensions.empty())
          {
            NSMutableArray* allowedTypes = [NSMutableArray arrayWithCapacity:extensions.size()];
            for (const auto& ext : extensions)
            {
              [allowedTypes addObject:[NSString stringWithUTF8String:ext.c_str()]];
            }
            [openPanel setAllowedFileTypes:allowedTypes];
          }
          
          // Show dialog
          NSModalResponse response = [openPanel runModal];
          
          if (response == NSModalResponseOK)
          {
            NSArray* urls = [openPanel URLs];
            if ([urls count] > 0)
            {
              NSURL* url = [urls objectAtIndex:0];
              const char* pathCStr = [[url path] UTF8String];
              if (pathCStr)
              {
                resultPath = pathCStr;
                result = true;
              }
            }
          }
        }
      });
    }
    
    if (result)
    {
      outPath = resultPath;
    }
    
    return result;
  }
}

#endif // __APPLE__

