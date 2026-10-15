/*
 ReflectINI - INI writer and reader with struct reflection for Arduino / C++17
 Copyright (c) 2026 Michael
 Commercial licensing available under separate written agreement.
 See LICENSE and COMMERCIAL.md.
*/
#pragma once

// Umbrella header: includes the whole library.
// ReflectINI uses the REFLECT / MEMBER / fn() / fx() macros of ReflectCore.h (shared with ReflectJSON),
// so one struct can be read and written as JSON and as INI.
#include "ReflectINI_tools.h"
#include "ReflectINI_writer.h"
#include "ReflectINI_reader.h"

#define REFLECTINI_VERSION "1.0.0"
