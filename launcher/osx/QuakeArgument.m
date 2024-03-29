/*
Copyright (C) 2007-2008 Kristian Duske

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#import "QuakeArgument.h"


@implementation QuakeArgument

- (id)initWithArgument:(NSString *)n {
    
    return [self initWithArgument:n andValue:nil];
}

- (id)initWithArgument:(NSString *)n andValue:(NSString *)v {

    self = [super init];
    if (self == nil)
        return nil;
    
    name = [n retain];
    if (v != nil)
        value = [v retain];
    
    return self;
}

- (NSString *)name {
    
    return name;
}

- (NSString *)value {
    
    return value;
}

- (BOOL)hasValue {

    return value != nil;
}

- (NSString *)description {
    
    NSMutableString *buffer = [[NSMutableString alloc] init];
    
    [buffer appendString:name];
    if ([self hasValue]) {
        [buffer appendString:@" "];
        [buffer appendString:value];
    }
    
    return buffer;
}

- (void) dealloc
{
    [name release];
    [value release];
    
    [super dealloc];
}

@end
