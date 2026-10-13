#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string>
#include <vector>

namespace mc {
class Window {
public:
    ~Window();
    bool create(int width,int height,const std::wstring& title,bool hidden,std::string& error);
    bool pump();
    void swap();
    void setCapture(bool value);
    void centerCursor();
    void setTitle(const std::wstring& title);
    void setVsync(bool enabled);
    void clearTransient();
    void toggleFullscreen();
    void setFullscreen(bool enabled){if(enabled!=fullscreen_)toggleFullscreen();}
    bool fullscreen()const{return fullscreen_;}
    bool down(int key) const {return key>=0&&key<256&&keys_[key];}
    bool pressed(int key) const {return key>=0&&key<256&&pressed_[key];}
    bool released(int key) const {return key>=0&&key<256&&released_[key];}
    int width()const{return width_;}int height()const{return height_;}
    bool captured()const{return captured_;}bool focused()const{return focused_;}
    int mouseX=0,mouseY=0,deltaX=0,deltaY=0,wheel=0;
    std::u32string textInput;
    std::vector<int> pressedKeyOrder;
    std::vector<int> repeatedKeyOrder; // Keyboard events, including the OS repeat stream used by text fields.
    HWND handle()const{return hwnd_;}
    static void* glProc(const char* name);
private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    LRESULT message(UINT,WPARAM,LPARAM);
    HWND hwnd_=nullptr;HDC dc_=nullptr;HGLRC gl_=nullptr;
    int width_=1280,height_=720;bool running_=true,captured_=false,focused_=true,fullscreen_=false,hidden_=false;
    std::array<bool,256> keys_{},pressed_{},released_{};
    WINDOWPLACEMENT placement_{};
    wchar_t highSurrogate_=0;
};
std::string utf8(const std::wstring& value);
std::wstring wide(const std::string& value);
std::string executableDirectory();
}
