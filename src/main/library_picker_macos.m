/*
 * TheXTech - A platform game engine ported from old source code for VB6
 *
 * Copyright (c) 2009-2011 Andrew Spinks, original VB6 code
 * Copyright (c) 2020-2025 Vitaly Novichkov <admin@wohlnet.ru>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// smbx2compat: Finder's open panel for the library's Import button

#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <string.h>

#include "main/library_picker.h"

int LibraryPicker_available(void)
{
    return 1;
}

char *LibraryPicker_pickImportSource(void)
{
    char *result = NULL;

    @autoreleasepool
    {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setTitle:@"Import episodes or SMBX2's data"];
        [panel setMessage:@"Pick a zip or a folder: an episode, SMBX2's data, or a download with both."];
        [panel setPrompt:@"Import"];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        [panel setAllowedFileTypes:@[@"zip"]];

        NSWindow *gameWindow = [NSApp keyWindow];

        if([panel runModal] == NSModalResponseOK)
        {
            NSURL *url = [[panel URLs] firstObject];
            const char *path = url ? [[url path] fileSystemRepresentation] : NULL;
            if(path)
                result = strdup(path);
        }

        // give the keyboard back to the game
        if(gameWindow)
            [gameWindow makeKeyAndOrderFront:nil];
    }

    return result;
}
