#include "mc/platform.hpp"
#include <GL/gl.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace mc {
using CreateContext = HGLRC (WINAPI*)(HDC,HGLRC,const int*);
using SwapInterval = BOOL (WINAPI*)(int);
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;}
std::string executableDirectory(){std::vector<wchar_t> buf(32768);DWORD n=GetModuleFileNameW(nullptr,buf.data(),DWORD(buf.size()));std::wstring s(buf.data(),n);auto p=s.find_last_of(L"/\\");return utf8(p==std::wstring::npos?L".":s.substr(0,p));}
void* Window::glProc(const char* n){PROC p=wglGetProcAddress(n);auto v=reinterpret_cast<std::intptr_t>(p);if(!p||v==1||v==2||v==3||v==-1){static HMODULE lib=LoadLibraryW(L"opengl32.dll");p=GetProcAddress(lib,n);}return reinterpret_cast<void*>(p);}
bool Window::create(int w,int h,const std::wstring& title,bool hidden,std::string& error){
    hidden_=hidden;width_=w;height_=h;
    HMODULE user=GetModuleHandleW(L"user32.dll");using Dpi=BOOL(WINAPI*)(HANDLE);auto dpi=reinterpret_cast<Dpi>(GetProcAddress(user,"SetProcessDpiAwarenessContext"));if(dpi)dpi(reinterpret_cast<HANDLE>(-4));
    placement_.length=sizeof(placement_);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_OWNDC;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=L"NativeVoxel189Window";wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClassExW(&wc);
    RECT r{0,0,w,h};AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,0);
    hwnd_=CreateWindowExW(0,wc.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,nullptr,nullptr,wc.hInstance,this);
    if(!hwnd_){error="Cannot create Win32 window";return false;}dc_=GetDC(hwnd_);
    PIXELFORMATDESCRIPTOR p{};p.nSize=sizeof(p);p.nVersion=1;p.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;p.iPixelType=PFD_TYPE_RGBA;p.cColorBits=32;p.cDepthBits=24;p.cStencilBits=8;
    int format=ChoosePixelFormat(dc_,&p);if(!format||!SetPixelFormat(dc_,format,&p)){error="Cannot select OpenGL pixel format";return false;}
    HGLRC legacy=wglCreateContext(dc_);if(!legacy||!wglMakeCurrent(dc_,legacy)){error="Cannot create OpenGL context";return false;}
    auto create=reinterpret_cast<CreateContext>(glProc("wglCreateContextAttribsARB"));
    if(create){const int attribs[]={0x2091,3,0x2092,3,0x9126,0x00000001,0};gl_=create(dc_,nullptr,attribs);}
    if(!gl_){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(legacy);error="OpenGL 3.3 core is required. Install the GPU driver (Windows software OpenGL 1.1 cannot run this renderer).";return false;}
    wglMakeCurrent(nullptr,nullptr);wglDeleteContext(legacy);wglMakeCurrent(dc_,gl_);
    RAWINPUTDEVICE raw{};raw.usUsagePage=1;raw.usUsage=2;raw.hwndTarget=hwnd_;RegisterRawInputDevices(&raw,1,sizeof(raw));
    if(!hidden){ShowWindow(hwnd_,SW_SHOW);UpdateWindow(hwnd_);}setVsync(true);return true;
}
Window::~Window(){setCapture(false);if(gl_){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl_);}if(dc_&&hwnd_)ReleaseDC(hwnd_,dc_);if(hwnd_)DestroyWindow(hwnd_);}
bool Window::pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)running_=false;TranslateMessage(&msg);DispatchMessageW(&msg);}return running_;}
void Window::swap(){SwapBuffers(dc_);}
void Window::clearTransient(){pressed_.fill(false);released_.fill(false);deltaX=deltaY=wheel=0;textInput.clear();pressedKeyOrder.clear();repeatedKeyOrder.clear();}
void Window::setTitle(const std::wstring& t){SetWindowTextW(hwnd_,t.c_str());}
void Window::setVsync(bool enabled){auto swap=reinterpret_cast<SwapInterval>(glProc("wglSwapIntervalEXT"));if(swap)swap(enabled?1:0);}
void Window::centerCursor(){mouseX=width_/2;mouseY=height_/2;deltaX=deltaY=0;if(hwnd_&&focused_&&!hidden_){POINT p{mouseX,mouseY};ClientToScreen(hwnd_,&p);SetCursorPos(p.x,p.y);}}
void Window::setCapture(bool v){if(!hwnd_)return;v=v&&focused_&&!hidden_;if(v==captured_)return;captured_=v;deltaX=deltaY=0;if(v){::SetCapture(hwnd_);RECT r;GetClientRect(hwnd_,&r);POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(hwnd_,&a);ClientToScreen(hwnd_,&b);RECT clip{a.x,a.y,b.x,b.y};ClipCursor(&clip);while(ShowCursor(FALSE)>=0){}centerCursor();}else{::ReleaseCapture();ClipCursor(nullptr);while(ShowCursor(TRUE)<0){}if(focused_)centerCursor();}}
void Window::toggleFullscreen(){
    if(!hwnd_||hidden_)return;
    const bool recapture=captured_;if(recapture)setCapture(false);
    if(!fullscreen_){
        placement_.length=sizeof(placement_);GetWindowPlacement(hwnd_,&placement_);MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&info);
        SetWindowLongPtrW(hwnd_,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowLongPtrW(hwnd_,GWL_EXSTYLE,0);
        SetWindowPos(hwnd_,HWND_TOP,info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER|SWP_NOACTIVATE);
    } else {
        SetWindowLongPtrW(hwnd_,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);SetWindowPlacement(hwnd_,&placement_);
        SetWindowPos(hwnd_,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    fullscreen_=!fullscreen_;UpdateWindow(hwnd_);if(recapture)setCapture(true);
}
LRESULT CALLBACK Window::proc(HWND h,UINT m,WPARAM w,LPARAM l){Window* self=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->hwnd_=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}return self?self->message(m,w,l):DefWindowProcW(h,m,w,l);}
LRESULT Window::message(UINT m,WPARAM w,LPARAM l){
    auto key=[&](int k,bool v){if(k<0||k>=256)return;if(v&&!keys_[k]){pressed_[k]=true;pressedKeyOrder.push_back(k);}if(!v&&keys_[k])released_[k]=true;keys_[k]=v;};
    auto keyboard=[&](int k,bool down){if(k==VK_SHIFT||k==VK_CONTROL||k==VK_MENU){int specific=k;if(k==VK_SHIFT)specific=int(MapVirtualKeyW(UINT((l>>16)&255),MAPVK_VSC_TO_VK_EX));else if(k==VK_CONTROL)specific=(l&(1ll<<24))?VK_RCONTROL:VK_LCONTROL;else specific=(l&(1ll<<24))?VK_RMENU:VK_LMENU;key(specific,down);if(k==VK_SHIFT)keys_[k]=keys_[VK_LSHIFT]||keys_[VK_RSHIFT];else if(k==VK_CONTROL)keys_[k]=keys_[VK_LCONTROL]||keys_[VK_RCONTROL];else keys_[k]=keys_[VK_LMENU]||keys_[VK_RMENU];}else key(k,down);};
    switch(m){case WM_CLOSE:running_=false;return 0;case WM_DESTROY:running_=false;return 0;
    case WM_SIZE:if(LOWORD(l)&&HIWORD(l)){width_=std::max(1,int(LOWORD(l)));height_=std::max(1,int(HIWORD(l)));}return 0;
    case WM_SETFOCUS:focused_=true;return 0;case WM_KILLFOCUS:focused_=false;setCapture(false);keys_.fill(false);return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:
        keyboard(int(w),true);
        for(unsigned n=0;n<std::min<unsigned>(unsigned(LOWORD(l)),256)&&repeatedKeyOrder.size()<1024;++n)repeatedKeyOrder.push_back(int(w));
        if(m==WM_SYSKEYDOWN&&w==VK_F4)running_=false;
        if(m==WM_KEYDOWN&&w=='V'&&!captured_&&(GetKeyState(VK_CONTROL)&0x8000)&&!(l&(1ll<<30))&&OpenClipboard(hwnd_)){
            HANDLE data=GetClipboardData(CF_UNICODETEXT);
            if(data){auto* value=static_cast<const wchar_t*>(GlobalLock(data));if(value){std::size_t limit=std::min<std::size_t>(GlobalSize(data)/sizeof(wchar_t),32768);for(std::size_t i=0;i<limit&&value[i];++i){char32_t c=value[i];if(c>=0xd800&&c<=0xdbff&&i+1<limit&&value[i+1]>=0xdc00&&value[i+1]<=0xdfff){c=0x10000+((c-0xd800)<<10)+(value[++i]-0xdc00);}if(c>=32&&c!=127&&c!=0xa7)textInput.push_back(c);}GlobalUnlock(data);}}
            CloseClipboard();
        }
        return 0;
    case WM_KEYUP:case WM_SYSKEYUP:keyboard(int(w),false);return 0;
    case WM_LBUTTONDOWN:key(VK_LBUTTON,true);return 0;case WM_LBUTTONUP:key(VK_LBUTTON,false);return 0;
    case WM_RBUTTONDOWN:key(VK_RBUTTON,true);return 0;case WM_RBUTTONUP:key(VK_RBUTTON,false);return 0;
    case WM_MBUTTONDOWN:key(VK_MBUTTON,true);return 0;case WM_MBUTTONUP:key(VK_MBUTTON,false);return 0;
    case WM_XBUTTONDOWN:key(HIWORD(w)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2,true);return TRUE;
    case WM_XBUTTONUP:key(HIWORD(w)==XBUTTON1?VK_XBUTTON1:VK_XBUTTON2,false);return TRUE;
    case WM_MOUSEMOVE:mouseX=static_cast<short>(LOWORD(l));mouseY=static_cast<short>(HIWORD(l));return 0;
    case WM_MOUSEWHEEL:wheel+=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA;return 0;
    case WM_INPUT:{if(!captured_)break;UINT size=0;GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER));if(size>4096)break;std::array<std::uint8_t,4096> data{};if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,data.data(),&size,sizeof(RAWINPUTHEADER))==UINT(-1))break;auto* raw=reinterpret_cast<RAWINPUT*>(data.data());if(raw->header.dwType==RIM_TYPEMOUSE&&!(raw->data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){deltaX+=raw->data.mouse.lLastX;deltaY+=raw->data.mouse.lLastY;}break;}
    case WM_CHAR:{char32_t c=char32_t(w);if(c>=0xD800&&c<=0xDBFF){highSurrogate_=wchar_t(c);return 0;}if(c>=0xDC00&&c<=0xDFFF&&highSurrogate_){c=0x10000+((char32_t(highSurrogate_)-0xD800)<<10)+(c-0xDC00);highSurrogate_=0;}else highSurrogate_=0;if(c>=32&&c!=127)textInput.push_back(c);return 0;}
    }return DefWindowProcW(hwnd_,m,w,l);
}
}
