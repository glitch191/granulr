// granulr - granular effect plugin
// Copyright (C) 2026 granulr contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Settings shared by every instance, stored in %APPDATA%\granulr\settings.xml.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace grn
{

enum FrameMode { frameDisplaySync = 0, frameUnlimited, frameCapped };

class GlobalSettings
{
public:
    static constexpr float defaultZoom = 1.25f;

    GlobalSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "settings";
        options.filenameSuffix = ".xml";
        options.folderName = "granulr";
        options.osxLibrarySubFolder = "Application Support";
        options.commonToAllUsers = false;
        options.ignoreCaseOfKeyNames = false;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 800;
        file = std::make_unique<juce::PropertiesFile> (options);
    }

    float getDefaultZoom() const { return juce::jlimit (1.0f, 2.0f, static_cast<float> (file->getDoubleValue ("defaultZoom", defaultZoom))); }
    void setDefaultZoom (float zoom) { file->setValue ("defaultZoom", static_cast<double> (zoom)); }

    int getFrameMode() const { return juce::jlimit (0, 2, file->getIntValue ("frameMode", frameDisplaySync)); }
    void setFrameMode (int mode) { file->setValue ("frameMode", mode); }

    int getFrameCap() const { return juce::jlimit (15, 480, file->getIntValue ("frameCap", 60)); }
    void setFrameCap (int framesPerSecond) { file->setValue ("frameCap", framesPerSecond); }

    bool getGpuRendering() const { return file->getBoolValue ("gpuRendering", true); }
    void setGpuRendering (bool enabled) { file->setValue ("gpuRendering", enabled); }

    juce::StringArray getFavourites() const
    {
        juce::StringArray names;
        names.addTokens (file->getValue ("favourites"), "|", "");
        names.removeEmptyStrings();
        return names;
    }

    void setFavourites (const juce::StringArray& names) { file->setValue ("favourites", names.joinIntoString ("|")); }

    void flush() { file->saveIfNeeded(); }

private:
    std::unique_ptr<juce::PropertiesFile> file;
};

} // namespace grn
