#pragma once

#include <chrono>

// Implemented by the VLC adapter; called from WinRT threads without any VLC lock held.
struct PlaybackCommands
{
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void Next() = 0;
    virtual void Previous() = 0;
    virtual void SkipForward() = 0;
    virtual void SkipBackward() = 0;
    virtual void SeekTo(std::chrono::microseconds position) = 0;

protected:
    ~PlaybackCommands() = default;
};
