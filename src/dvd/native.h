#pragma once

#include <windows.h>

namespace dvd {

// Slot order and by-value RECT arguments are the DVD executable's x86 ABI.
class Native {
public:
    virtual ~Native() = default;
    virtual int init() = 0;
    virtual void shutdown() = 0;
    virtual void configure() = 0;
    virtual void showWindow(int show) = 0;
    virtual void setSourceRect(RECT rect) = 0;
    virtual void setDestRect(RECT rect) = 0;
    virtual void setClientRect(RECT rect) = 0;
    virtual void closeMovie(int release) = 0;
    virtual int openMovie(void* parent, char* path) = 0;
    virtual void playMovie(void* parent) = 0;
    virtual void pauseMovie() = 0;
    virtual void stopMovie() = 0;
    virtual void seekMovie(int selector, int frame) = 0;
    virtual void stepMovie(int frames) = 0;
    virtual int usesWaveDevice() = 0;
    virtual int usesOverlay() = 0;
    virtual void handleNotify(void* parent, unsigned notification) = 0;
    virtual int handleMessages(void* parent, unsigned message, unsigned wparam, long lparam) = 0;
    virtual long getDriverID() = 0;
    virtual int isPlaying() = 0;
    virtual int userBreak() = 0;
    virtual void* getMovieWnd() = 0;
    virtual void setPlayFrom(long frame) = 0;
    virtual void setPlayTo(long frame) = 0;
    virtual char* getErrorString() = 0;
};

}
