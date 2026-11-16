// ==WindhawkMod==
// @id              thrivingtop-favourite-apps-desktop
// @name            Favourite Apps Desktop Launcher
// @description     Keep favourite apps on the desktop and quickly launch your most-used shortcuts
// @version         1.6.0
// @author          thrivingtop
// @include          windhawk.exe
// @github           https://github.com/thrivingtop
// @compilerOptions -lshell32 -lole32 -luuid -lgdi32 -luser32 -ldwmapi -lcomctl32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Favourite Apps Desktop Launcher

A compact desktop panel for favourite apps. Press **Ctrl+Alt+Space** to show or
hide it. Click an app to open it; the panel remembers which shortcuts you launch
and can sort them by frequency. Hover over the compact header to reveal the
apps, or click **PIN** to keep the list open. Drag the header to move the panel.
Choose a list or two-column grid, adjust its opacity, and set whether it stays
above other windows in Settings. Windows 11 uses an acrylic backdrop when
available; the opacity setting also works as a fallback. Right-click the panel
for quick layout, pinning, topmost, hide, and Windhawk actions. Choose
**Deactivate this mod** there to open Windhawk and switch it off from Installed
Mods; Windhawk does not allow a mod to disable itself directly.

## Add your apps

Open the mod's **Settings** and edit **Apps**. Add one app per line in this
format:

`Display name|path or command`

Click the **+** button to search apps and shortcuts in your Start Menu, then
choose **Add selected**. You can also drag an `.exe`, `.lnk`, or folder from the
desktop or File Explorer onto the panel. Added items are saved locally.
Drag an app onto another app to change its position. Turn off **Sort by most
used** in Settings to keep your manual order.
Change **App icon size** in Settings to resize the app tiles and icons.

For example:

```text
Browser|C:\Program Files\Google\Chrome\Application\chrome.exe
Notepad|notepad.exe
Downloads|shell:Downloads
```

Paths can include environment variables such as `%USERPROFILE%`. The panel
starts with a few common Windows shortcuts; replace them with your own. The
**Sort by most used** setting orders shortcuts by how often you launch them
from this panel. Counts are stored locally in your user profile.

This is a Windhawk tool mod and runs separately from Explorer. It does not
monitor app activity outside of launches made through this panel.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- apps: |-
    Notepad|notepad.exe
    Calculator|calc.exe
    File Explorer|explorer.exe
  $name: Apps
  $description: One app per line as Name|path, or drag app shortcuts onto the panel.
- sortMostUsed: false
  $name: Sort by most used
- panelWidth: 250
  $name: Panel width
  $description: Panel width in pixels (180 to 420).
- layout: list
  $name: App arrangement
  $options:
  - list: List
  - grid: Two-column grid
- opacity: 92
  $name: Panel opacity
  $description: Set from 65 (more transparent) to 100 (opaque).
- showOnHover: true
  $name: Expand on hover
- stayOnTop: true
  $name: Keep above other windows
- accent: blue
  $name: Accent color
  $options:
  - blue: Ocean blue
  - violet: Violet
  - mint: Mint
  - amber: Amber
- appSize: 28
  $name: App icon size
  $description: Icon size in pixels (20 to 52). Increase panel width for grid layouts with large icons.
*/
// ==/WindhawkModSettings==

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <sstream>

struct AppEntry { std::wstring name, target; unsigned count = 0; };
static std::vector<AppEntry> g_apps;
static std::vector<AppEntry> g_addedApps;
static std::vector<AppEntry> g_availableApps;
static std::vector<size_t> g_searchMatches;
static HWND g_window;
static HWND g_addWindow;
static HWND g_searchEdit;
static HWND g_searchList;
static HANDLE g_thread;
static HANDLE g_stop;
static int g_width = 250;
static int g_opacity = 92;
static int g_appSize = 28;
static COLORREF g_accentColor = RGB(91,156,245);
static bool g_sort = true;
static bool g_showOnHover = true;
static bool g_stayOnTop = true;
static bool g_expanded = false;
static bool g_pinnedOpen = false;
static bool g_mouseOutsideSinceSet = false;
static ULONGLONG g_mouseOutsideSince = 0;
static POINT g_dragStart{};
static int g_dragSource = -1;
static bool g_dragReorderActive = false;
static int g_contentHeight = 60;
static int g_layoutColumns = 1;
static std::wstring g_countPath;
static std::wstring g_addedAppsPath;
static std::wstring g_orderPath;
static const UINT WM_REBUILD = WM_APP + 10;
static const UINT WM_SHOW_CONTEXT = WM_APP + 11;
static constexpr int ID_FIRST_APP = 1000;
static constexpr int ID_PIN_OPEN = 900;
static constexpr int ID_ADD_APP = 901;
static constexpr int ID_SEARCH_EDIT = 10;
static constexpr int ID_SEARCH_LIST = 11;
static constexpr int ID_SEARCH_ADD = 12;
static constexpr int ID_SEARCH_CLOSE = 13;
static constexpr UINT_PTR ID_HOVER_TIMER = 1;
static constexpr int HEADER_HEIGHT = 48;
enum ContextCommand { IDM_KEEP_OPEN=2001, IDM_TOPMOST, IDM_LIST, IDM_GRID,
                      IDM_SETTINGS, IDM_DEACTIVATE, IDM_HIDE };

static std::wstring Trim(std::wstring s) {
    size_t a=s.find_first_not_of(L" \t\r\n"), b=s.find_last_not_of(L" \t\r\n");
    return a==std::wstring::npos ? L"" : s.substr(a,b-a+1);
}
static void Rebuild();
static void OpenAddWindow();
static bool HasTarget(const std::vector<AppEntry>& apps,const std::wstring& target) {
    return std::any_of(apps.begin(),apps.end(),[&](const AppEntry& app) {
        return _wcsicmp(app.target.c_str(),target.c_str())==0;
    });
}
static void SaveAppOrder() {
    if(g_orderPath.empty()) return;
    std::wofstream out(g_orderPath.c_str(),std::ios::trunc);
    for(const auto& app:g_apps) out<<app.target<<L"\n";
}
static void LoadAppOrder() {
    PWSTR base=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&base))) return;
    g_orderPath=base;
    CoTaskMemFree(base);
    g_orderPath+=L"\\FavouriteAppsDesktop.order";
    std::wifstream in(g_orderPath.c_str());
    std::vector<AppEntry> ordered;
    std::vector<bool> used(g_apps.size(),false);
    std::wstring target;
    while(std::getline(in,target)) {
        for(size_t i=0;i<g_apps.size();i++) {
            if(!used[i]&&_wcsicmp(g_apps[i].target.c_str(),target.c_str())==0) {
                ordered.push_back(g_apps[i]);
                used[i]=true;
                break;
            }
        }
    }
    for(size_t i=0;i<g_apps.size();i++) if(!used[i]) ordered.push_back(g_apps[i]);
    g_apps.swap(ordered);
}
static void MoveAppTo(int source,int destination) {
    if(source<0||destination<0||source>=(int)g_apps.size()||destination>=(int)g_apps.size()||source==destination) return;
    if(source<destination)
        std::rotate(g_apps.begin()+source,g_apps.begin()+source+1,g_apps.begin()+destination+1);
    else
        std::rotate(g_apps.begin()+destination,g_apps.begin()+source,g_apps.begin()+source+1);
    SaveAppOrder();
    Rebuild();
}
static std::wstring GetDisplayName(const std::wstring& path) {
    SHFILEINFOW info{};
    if(SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_DISPLAYNAME))
        return info.szDisplayName;
    size_t slash=path.find_last_of(L"\\/");
    std::wstring name=slash==std::wstring::npos?path:path.substr(slash+1);
    size_t dot=name.find_last_of(L'.');
    if(dot!=std::wstring::npos && dot>0) name.resize(dot);
    return name;
}
static void LoadAddedApps() {
    PWSTR base=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&base))) return;
    g_addedAppsPath=base;
    CoTaskMemFree(base);
    g_addedAppsPath+=L"\\FavouriteAppsDesktop.apps";
    if(g_orderPath.empty()) g_orderPath=g_addedAppsPath.substr(0,g_addedAppsPath.find_last_of(L"\\/"))+L"\\FavouriteAppsDesktop.order";
    std::wifstream in(g_addedAppsPath.c_str());
    std::wstring line;
    while(std::getline(in,line)) {
        size_t separator=line.find(L'|');
        if(separator==std::wstring::npos) continue;
        AppEntry app{Trim(line.substr(0,separator)),Trim(line.substr(separator+1)),0};
        if(app.name.empty()||app.target.empty()||HasTarget(g_apps,app.target)) continue;
        g_addedApps.push_back(app);
        g_apps.push_back(app);
    }
}
static void SaveAddedApps() {
    if(g_addedAppsPath.empty()) return;
    std::wofstream out(g_addedAppsPath.c_str(),std::ios::trunc);
    for(const auto& app:g_addedApps) out<<app.name<<L"|"<<app.target<<L"\n";
}
static std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
    return value;
}
static void EnumerateStartMenuFolder(const std::wstring& folder,int depth=0) {
    if(depth>10) return;
    WIN32_FIND_DATAW data{};
    std::wstring pattern=folder+L"\\*";
    HANDLE find=FindFirstFileW(pattern.c_str(),&data);
    if(find==INVALID_HANDLE_VALUE) return;
    do {
        if(wcscmp(data.cFileName,L".")==0||wcscmp(data.cFileName,L"..")==0) continue;
        std::wstring path=folder+L"\\"+data.cFileName;
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
            if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) EnumerateStartMenuFolder(path,depth+1);
            continue;
        }
        size_t dot=path.find_last_of(L'.');
        if(dot==std::wstring::npos) continue;
        std::wstring ext=Lowercase(path.substr(dot));
        if(ext!=L".lnk"&&ext!=L".url"&&ext!=L".exe") continue;
        std::wstring name=data.cFileName;
        dot=name.find_last_of(L'.'); if(dot!=std::wstring::npos) name.resize(dot);
        if(name.empty()||HasTarget(g_availableApps,path)) continue;
        g_availableApps.push_back({name,path,0});
    } while(FindNextFileW(find,&data));
    FindClose(find);
}
static void LoadAvailableApps() {
    g_availableApps.clear();
    for(REFKNOWNFOLDERID folderId : {FOLDERID_Programs,FOLDERID_CommonPrograms}) {
        PWSTR path=nullptr;
        if(SUCCEEDED(SHGetKnownFolderPath(folderId,0,nullptr,&path))) {
            EnumerateStartMenuFolder(path);
            CoTaskMemFree(path);
        }
    }
    std::stable_sort(g_availableApps.begin(),g_availableApps.end(),[](const auto& a,const auto& b){
        return _wcsicmp(a.name.c_str(),b.name.c_str())<0;
    });
}
static void RefreshSearchResults() {
    if(!g_searchList||!g_searchEdit) return;
    WCHAR queryBuffer[256]{};
    GetWindowTextW(g_searchEdit,queryBuffer,ARRAYSIZE(queryBuffer));
    std::wstring query=Lowercase(queryBuffer);
    g_searchMatches.clear();
    SendMessageW(g_searchList,LB_RESETCONTENT,0,0);
    for(size_t i=0;i<g_availableApps.size();i++) {
        const auto& app=g_availableApps[i];
        if(HasTarget(g_apps,app.target)) continue;
        if(!query.empty()&&Lowercase(app.name).find(query)==std::wstring::npos&&
           Lowercase(app.target).find(query)==std::wstring::npos) continue;
        g_searchMatches.push_back(i);
        SendMessageW(g_searchList,LB_ADDSTRING,0,(LPARAM)app.name.c_str());
        if(g_searchMatches.size()>=200) break;
    }
    if(!g_searchMatches.empty()) SendMessageW(g_searchList,LB_SETCURSEL,0,0);
}
static void AddSelectedSearchResult() {
    if(!g_searchList) return;
    LRESULT selected=SendMessageW(g_searchList,LB_GETCURSEL,0,0);
    if(selected==LB_ERR||(size_t)selected>=g_searchMatches.size()) return;
    AppEntry app=g_availableApps[g_searchMatches[(size_t)selected]];
    if(HasTarget(g_apps,app.target)) return;
    g_addedApps.push_back(app);
    g_apps.push_back(app);
    if(g_sort) std::stable_sort(g_apps.begin(),g_apps.end(),[](const auto& a,const auto& b){return a.count>b.count;});
    SaveAddedApps(); SaveAppOrder(); Rebuild(); RefreshSearchResults();
}
static LRESULT CALLBACK AddWindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {
        g_addWindow=hwnd;
        g_searchEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
            18,58,354,32,hwnd,(HMENU)(INT_PTR)ID_SEARCH_EDIT,GetModuleHandleW(nullptr),nullptr);
        g_searchList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
            18,102,354,252,hwnd,(HMENU)(INT_PTR)ID_SEARCH_LIST,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_searchEdit,EM_SETCUEBANNER,TRUE,(LPARAM)L"Search apps and Start Menu shortcuts");
        CreateWindowExW(0,L"BUTTON",L"Add selected",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            168,370,112,32,hwnd,(HMENU)(INT_PTR)ID_SEARCH_ADD,GetModuleHandleW(nullptr),nullptr);
        CreateWindowExW(0,L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            288,370,84,32,hwnd,(HMENU)(INT_PTR)ID_SEARCH_CLOSE,GetModuleHandleW(nullptr),nullptr);
        RefreshSearchResults();
        SetFocus(g_searchEdit);
        return 0;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(wp)==ID_SEARCH_EDIT&&HIWORD(wp)==EN_CHANGE) { RefreshSearchResults(); return 0; }
        if(LOWORD(wp)==ID_SEARCH_ADD || (LOWORD(wp)==ID_SEARCH_LIST&&HIWORD(wp)==LBN_DBLCLK)) {
            AddSelectedSearchResult(); return 0;
        }
        if(LOWORD(wp)==ID_SEARCH_CLOSE) { DestroyWindow(hwnd); return 0; }
    }
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r);
        HBRUSH bg=CreateSolidBrush(RGB(20,25,36)); FillRect(dc,&r,bg); DeleteObject(bg);
        RECT accent={14,0,r.right-14,3}; HBRUSH line=CreateSolidBrush(g_accentColor); FillRect(dc,&accent,line); DeleteObject(line);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(245,248,252));
        HFONT font=CreateFontW(-17,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        HFONT old=(HFONT)SelectObject(dc,font); RECT title={18,16,r.right-18,45};
        DrawTextW(dc,L"ADD AN APP",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_LEFT);
        SelectObject(dc,old); DeleteObject(font); EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { g_addWindow=nullptr; g_searchEdit=nullptr; g_searchList=nullptr; return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void OpenAddWindow() {
    if(g_addWindow) { SetForegroundWindow(g_addWindow); SetFocus(g_searchEdit); return; }
    RECT panel{}; GetWindowRect(g_window,&panel);
    int x=panel.right+12, y=panel.top;
    if(x+400>GetSystemMetrics(SM_CXSCREEN)) x=std::max(8,(int)panel.left-400);
    if(y+430>GetSystemMetrics(SM_CYSCREEN)) y=std::max(8,GetSystemMetrics(SM_CYSCREEN)-438);
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,L"WhFavouriteAppsAddWindow",
        L"Add an app",WS_POPUP,x,y,390,420,g_window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(window) {
        SetLayeredWindowAttributes(window,0,248,LWA_ALPHA);
        DWORD backdrop=3; DwmSetWindowAttribute(window,(DWMWINDOWATTRIBUTE)38,&backdrop,sizeof(backdrop));
        ShowWindow(window,SW_SHOWNORMAL);
    }
}
static void AddDroppedFiles(HDROP drop) {
    UINT count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);
    for(UINT i=0;i<count;i++) {
        UINT length=DragQueryFileW(drop,i,nullptr,0);
        if(!length) continue;
        std::wstring path(length+1,L'\0');
        if(!DragQueryFileW(drop,i,path.data(),length+1)) continue;
        path.resize(length);
        if(HasTarget(g_apps,path)) continue;
        AppEntry app{GetDisplayName(path),path,0};
        if(app.name.empty()) continue;
        g_addedApps.push_back(app);
        g_apps.push_back(app);
    }
    DragFinish(drop);
    if(g_sort) std::stable_sort(g_apps.begin(),g_apps.end(),[](const auto& a,const auto& b){return a.count>b.count;});
    SaveAddedApps();
    SaveAppOrder();
    Rebuild();
}
static void LoadCounts() {
    PWSTR base=nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&base))) {
        g_countPath=base; CoTaskMemFree(base);
        g_countPath+=L"\\FavouriteAppsDesktop.counts";
        std::wifstream in(g_countPath.c_str());
        std::wstring line;
        while (std::getline(in,line)) {
            auto p=line.find(L'|'); if(p==std::wstring::npos) continue;
            std::wstring target=line.substr(0,p);
            unsigned n=0; try { n=(unsigned)std::stoul(line.substr(p+1)); } catch(...) {}
            for(auto& app:g_apps) if(_wcsicmp(app.target.c_str(),target.c_str())==0) app.count=n;
        }
    }
}
static void SaveCounts() {
    if(g_countPath.empty()) return;
    std::wofstream out(g_countPath.c_str(),std::ios::trunc);
    for(const auto& a:g_apps) out<<a.target<<L"|"<<a.count<<L"\n";
}
static void LoadSettings() {
    g_apps.clear();
    g_addedApps.clear();
    PCWSTR raw=Wh_GetStringSetting(L"apps");
    std::wstring text=raw?raw:L""; if(raw) Wh_FreeStringSetting(raw);
    std::wistringstream lines(text); std::wstring line;
    while(std::getline(lines,line)) {
        auto p=line.find(L'|'); if(p==std::wstring::npos) continue;
        AppEntry a{Trim(line.substr(0,p)),Trim(line.substr(p+1)),0};
        if(!a.name.empty()&&!a.target.empty()) g_apps.push_back(a);
    }
    g_sort=Wh_GetIntSetting(L"sortMostUsed")!=0;
    g_width=std::clamp(Wh_GetIntSetting(L"panelWidth"),180,420);
    g_opacity=std::clamp(Wh_GetIntSetting(L"opacity"),65,100);
    g_appSize=std::clamp(Wh_GetIntSetting(L"appSize"),20,52);
    g_showOnHover=Wh_GetIntSetting(L"showOnHover")!=0;
    g_stayOnTop=Wh_GetIntSetting(L"stayOnTop")!=0;
    PCWSTR accent=Wh_GetStringSetting(L"accent");
    g_accentColor=RGB(91,156,245);
    if(accent&&_wcsicmp(accent,L"violet")==0) g_accentColor=RGB(173,119,255);
    else if(accent&&_wcsicmp(accent,L"mint")==0) g_accentColor=RGB(69,211,177);
    else if(accent&&_wcsicmp(accent,L"amber")==0) g_accentColor=RGB(255,177,74);
    if(accent) Wh_FreeStringSetting(accent);
    PCWSTR layout=Wh_GetStringSetting(L"layout");
    g_layoutColumns=(layout&&_wcsicmp(layout,L"grid")==0)?2:1;
    if(layout) Wh_FreeStringSetting(layout);
    LoadAddedApps();
    LoadAppOrder();
    LoadCounts();
    if(g_sort) std::stable_sort(g_apps.begin(),g_apps.end(),[](const auto& a,const auto& b){return a.count>b.count;});
}
static LRESULT CALLBACK ChildControlProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
static void UpdateWindowRegion();
static void Rebuild() {
    if(!g_window) return;
    HWND child=GetWindow(g_window,GW_CHILD);
    while(child){HWND next=GetWindow(child,GW_HWNDNEXT);DestroyWindow(child);child=next;}
    CreateWindowExW(0,L"BUTTON",g_pinnedOpen?L"UNPIN":L"PIN",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                    g_width-66,10,54,26,g_window,(HMENU)(INT_PTR)ID_PIN_OPEN,GetModuleHandleW(nullptr),nullptr);
    SetWindowSubclass(GetDlgItem(g_window,ID_PIN_OPEN),ChildControlProc,1,(DWORD_PTR)-1);
    HWND addButton=CreateWindowExW(0,L"BUTTON",L"+",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                    g_width-106,10,30,26,g_window,(HMENU)(INT_PTR)ID_ADD_APP,GetModuleHandleW(nullptr),nullptr);
    if(addButton) SetWindowSubclass(addButton,ChildControlProc,1,(DWORD_PTR)-1);
    int columns=g_layoutColumns;
    int buttonWidth=(g_width-24-(columns-1)*8)/columns;
    int buttonHeight=g_appSize+14;
    int rowPitch=buttonHeight+8;
    for(size_t i=0;i<g_apps.size()&&i<60;i++) {
        std::wstring label=g_apps[i].name;
        int x=12+(int)(i%columns)*(buttonWidth+8);
        int y=HEADER_HEIGHT+((int)i/columns)*rowPitch;
        HWND button=CreateWindowExW(0,L"BUTTON",label.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
                        x,y,buttonWidth,buttonHeight,g_window,(HMENU)(INT_PTR)(ID_FIRST_APP+(int)i),GetModuleHandleW(nullptr),nullptr);
        if(button) SetWindowSubclass(button,ChildControlProc,1,(DWORD_PTR)i);
    }
    if(g_apps.empty()) CreateWindowExW(0,L"STATIC",L"Drop apps here or add them in Windhawk Settings",WS_CHILD|WS_VISIBLE,
         14,HEADER_HEIGHT,g_width-28,28,g_window,nullptr,GetModuleHandleW(nullptr),nullptr);
    int rows=(int)((std::min<size_t>(g_apps.size(),60)+columns-1)/columns);
    g_contentHeight=std::max(HEADER_HEIGHT+12,HEADER_HEIGHT+rows*rowPitch+8);
    int windowHeight=g_expanded?g_contentHeight:HEADER_HEIGHT;
    SetWindowPos(g_window,nullptr,0,0,g_width,windowHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    UpdateWindowRegion();
    SetLayeredWindowAttributes(g_window,0,(BYTE)(g_opacity*255/100),LWA_ALPHA);
    InvalidateRect(g_window,nullptr,TRUE);
}
static void SetExpanded(bool expanded) {
    if(!g_window || g_expanded==expanded) return;
    g_expanded=expanded;
    int height=g_expanded?g_contentHeight:HEADER_HEIGHT;
    SetWindowPos(g_window,nullptr,0,0,g_width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    UpdateWindowRegion();
    InvalidateRect(g_window,nullptr,TRUE);
}
static void UpdateWindowRegion() {
    if(!g_window) return;
    RECT r; GetWindowRect(g_window,&r);
    int width=r.right-r.left,height=r.bottom-r.top;
    HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,22,22);
    if(region && !SetWindowRgn(g_window,region,TRUE)) DeleteObject(region);
}
static void ApplyTopmost() {
    if(!g_window) return;
    SetWindowPos(g_window,g_stayOnTop?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
static LRESULT CALLBACK ChildControlProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,
                                         UINT_PTR subclassId,DWORD_PTR refData) {
    if(msg==WM_CONTEXTMENU) {
        PostMessageW(GetParent(hwnd),WM_SHOW_CONTEXT,0,0);
        return 0;
    }
    if(refData!=(DWORD_PTR)-1 && refData<g_apps.size()) {
        if(msg==WM_LBUTTONDOWN) {
            g_dragSource=(int)refData;
            g_dragReorderActive=false;
            GetCursorPos(&g_dragStart);
            SetCapture(hwnd);
            return DefSubclassProc(hwnd,msg,wp,lp);
        }
        if(msg==WM_MOUSEMOVE && g_dragSource==(int)refData && GetCapture()==hwnd && (wp&MK_LBUTTON)) {
            POINT p; GetCursorPos(&p);
            if(!g_dragReorderActive &&
               (std::abs(p.x-g_dragStart.x)>=GetSystemMetrics(SM_CXDRAG) ||
                std::abs(p.y-g_dragStart.y)>=GetSystemMetrics(SM_CYDRAG)))
                g_dragReorderActive=true;
            if(g_dragReorderActive) {
                SetCursor(LoadCursorW(nullptr,IDC_SIZEALL));
                return 0;
            }
        }
        if(msg==WM_LBUTTONUP && g_dragSource==(int)refData && GetCapture()==hwnd) {
            int source=g_dragSource;
            bool reorder=g_dragReorderActive;
            if(!reorder) {
                g_dragSource=-1;
                return DefSubclassProc(hwnd,msg,wp,lp);
            }
            POINT p; GetCursorPos(&p);
            HWND target=WindowFromPoint(p);
            int destination=-1;
            if(target && IsChild(GetParent(hwnd),target)) {
                int id=GetDlgCtrlID(target);
                if(id>=ID_FIRST_APP && id<ID_FIRST_APP+(int)g_apps.size())
                    destination=id-ID_FIRST_APP;
            }
            if(destination<0) {
                ScreenToClient(GetParent(hwnd),&p);
                int columns=g_layoutColumns;
                int buttonWidth=(g_width-24-(columns-1)*8)/columns;
                if(p.y>=HEADER_HEIGHT && p.x>=12 && p.x<g_width-12) {
                    int column=std::clamp((int)(p.x-12)/(buttonWidth+8),0,columns-1);
                    int row=(p.y-HEADER_HEIGHT)/(g_appSize+22);
                    int index=row*columns+column;
                    if(index<(int)g_apps.size()) destination=index;
                }
            }
            ReleaseCapture();
            g_dragSource=-1;
            g_dragReorderActive=false;
            MoveAppTo(source,destination);
            return 0;
        }
        if(msg==WM_CAPTURECHANGED && g_dragSource==(int)refData) {
            g_dragSource=-1;
            g_dragReorderActive=false;
        }
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
static void OpenWindhawk() {
    WCHAR path[MAX_PATH];
    DWORD length=GetModuleFileNameW(nullptr,path,ARRAYSIZE(path));
    if(!length||length>=ARRAYSIZE(path)) return;
    std::wstring appPath(path,length);
    size_t slash=appPath.find_last_of(L"\\/");
    if(slash==std::wstring::npos) return;
    appPath.resize(slash+1);
    appPath+=L"windhawk.exe";
    ShellExecuteW(nullptr,L"open",appPath.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
static void ShowContextMenu(HWND hwnd) {
    HMENU menu=CreatePopupMenu();
    HMENU layout=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(g_pinnedOpen?MF_CHECKED:0),IDM_KEEP_OPEN,L"Keep panel open");
    AppendMenuW(menu,MF_STRING|(g_stayOnTop?MF_CHECKED:0),IDM_TOPMOST,L"Stay on top");
    AppendMenuW(layout,MF_STRING|(g_layoutColumns==1?MF_CHECKED:0),IDM_LIST,L"List");
    AppendMenuW(layout,MF_STRING|(g_layoutColumns==2?MF_CHECKED:0),IDM_GRID,L"Two-column grid");
    AppendMenuW(menu,MF_POPUP,(UINT_PTR)layout,L"App arrangement");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,IDM_SETTINGS,L"Open Windhawk");
    AppendMenuW(menu,MF_STRING,IDM_DEACTIVATE,L"Deactivate this mod...");
    AppendMenuW(menu,MF_STRING,IDM_HIDE,L"Hide panel");
    POINT p; GetCursorPos(&p);
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);
    DestroyMenu(menu);
    switch(command) {
        case IDM_KEEP_OPEN:
            g_pinnedOpen=!g_pinnedOpen;
            SetExpanded(g_pinnedOpen);
            Rebuild();
            break;
        case IDM_TOPMOST:
            g_stayOnTop=!g_stayOnTop;
            ApplyTopmost();
            break;
        case IDM_LIST:
            g_layoutColumns=1;
            Rebuild();
            break;
        case IDM_GRID:
            g_layoutColumns=2;
            Rebuild();
            break;
        case IDM_SETTINGS:
            OpenWindhawk();
            break;
        case IDM_DEACTIVATE:
            MessageBoxW(hwnd,L"Windhawk does not let a mod deactivate itself. Windhawk will open; find Favourite Apps Desktop Launcher in Installed Mods and switch it off.",L"Deactivate Favourite Apps",MB_OK|MB_ICONINFORMATION);
            OpenWindhawk();
            break;
        case IDM_HIDE:
            ShowWindow(hwnd,SW_HIDE);
            break;
    }
}
static LRESULT CALLBACK PanelProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CREATE) {
        g_window=hwnd;
        DragAcceptFiles(hwnd,TRUE);
        WNDCLASSW addClass{};
        addClass.lpfnWndProc=AddWindowProc;
        addClass.hInstance=GetModuleHandleW(nullptr);
        addClass.lpszClassName=L"WhFavouriteAppsAddWindow";
        addClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&addClass);
        Rebuild();
        SetTimer(hwnd,ID_HOVER_TIMER,100,nullptr);
        return 0;
    }
    if(msg==WM_REBUILD) { Rebuild(); ApplyTopmost(); return 0; }
    if(msg==WM_SHOW_CONTEXT || msg==WM_CONTEXTMENU) { ShowContextMenu(hwnd); return 0; }
    if(msg==WM_DROPFILES) { AddDroppedFiles((HDROP)wp); return 0; }
    if(msg==WM_NCHITTEST) {
        LRESULT hit=DefWindowProcW(hwnd,msg,wp,lp);
        if(hit==HTCLIENT) {
            POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&p);
            if(p.y<HEADER_HEIGHT && p.x<g_width-112) return HTCAPTION;
        }
        return hit;
    }
    if(msg==WM_TIMER && wp==ID_HOVER_TIMER) {
        if(!g_showOnHover || !IsWindowVisible(hwnd)) return 0;
        if(g_dragSource>=0) { SetExpanded(true); return 0; }
        POINT p; GetCursorPos(&p);
        HWND under=WindowFromPoint(p);
        bool inside=(under==hwnd || IsChild(hwnd,under));
        if(inside) {
            g_mouseOutsideSinceSet=false;
            SetExpanded(true);
        } else if(g_expanded&&!g_pinnedOpen) {
            ULONGLONG now=GetTickCount64();
            if(!g_mouseOutsideSinceSet) { g_mouseOutsideSinceSet=true; g_mouseOutsideSince=now; }
            else if(now-g_mouseOutsideSince>450) SetExpanded(false);
        }
        return 0;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(wp)==ID_ADD_APP) { OpenAddWindow(); return 0; }
        if(LOWORD(wp)==ID_PIN_OPEN) {
            g_pinnedOpen=!g_pinnedOpen;
            SetExpanded(g_pinnedOpen);
            Rebuild();
            return 0;
        }
        if(LOWORD(wp)<ID_FIRST_APP) return DefWindowProcW(hwnd,msg,wp,lp);
        size_t i=LOWORD(wp)-ID_FIRST_APP;
        if(i<g_apps.size()) {
            std::wstring target=g_apps[i].target;
            if(target.find(L'%')!=std::wstring::npos) {
                wchar_t expanded[32768]; DWORD n=ExpandEnvironmentStringsW(target.c_str(),expanded,32768);
                if(n&&n<32768) target=expanded;
            }
            HINSTANCE result=ShellExecuteW(hwnd,L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            if((INT_PTR)result>32) { g_apps[i].count++; SaveCounts(); if(g_sort) {std::stable_sort(g_apps.begin(),g_apps.end(),[](const auto&a,const auto&b){return a.count>b.count;}); Rebuild();} }
        }
        return 0;
    }
    if(msg==WM_DRAWITEM) {
        auto d=(DRAWITEMSTRUCT*)lp;
        bool hot=(d->itemState&ODS_HOTLIGHT)!=0;
        bool focused=(d->itemState&ODS_FOCUS)!=0;
        HRGN shape=CreateRoundRectRgn(d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,12,12);
        COLORREF fill=hot?RGB(47,58,77):RGB(29,35,48);
        if(d->CtlID==ID_PIN_OPEN && g_pinnedOpen) fill=g_accentColor;
        HBRUSH brush=CreateSolidBrush(fill);
        FillRgn(d->hDC,shape,brush);
        DeleteObject(brush);
        if(focused) {
            HBRUSH focusBrush=CreateSolidBrush(g_accentColor);
            FrameRgn(d->hDC,shape,focusBrush,2,2);
            DeleteObject(focusBrush);
        }
        DeleteObject(shape);
        SetBkMode(d->hDC,TRANSPARENT);
        HFONT font=CreateFontW(-15,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                               DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        HFONT oldFont=(HFONT)SelectObject(d->hDC,font);
        if(d->CtlID==ID_PIN_OPEN) {
            SetTextColor(d->hDC,g_pinnedOpen?RGB(14,24,38):RGB(190,204,225));
            DrawTextW(d->hDC,g_pinnedOpen?L"PINNED":L"PIN",-1,&d->rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
        } else if(d->CtlID==ID_ADD_APP) {
            SetTextColor(d->hDC,hot?RGB(255,255,255):g_accentColor);
            HFONT plusFont=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
            HFONT previous=(HFONT)SelectObject(d->hDC,plusFont);
            DrawTextW(d->hDC,L"+",-1,&d->rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
            SelectObject(d->hDC,previous); DeleteObject(plusFont);
        } else {
            size_t index=d->CtlID-ID_FIRST_APP;
            if(index<g_apps.size()) {
                SHFILEINFOW fileInfo{};
                std::wstring iconTarget=g_apps[index].target;
                if(iconTarget.find(L'%')!=std::wstring::npos) {
                    WCHAR expanded[MAX_PATH*4];
                    DWORD n=ExpandEnvironmentStringsW(iconTarget.c_str(),expanded,ARRAYSIZE(expanded));
                    if(n&&n<ARRAYSIZE(expanded)) iconTarget=expanded;
                }
                if(SHGetFileInfoW(iconTarget.c_str(),0,&fileInfo,sizeof(fileInfo),SHGFI_ICON|SHGFI_SMALLICON)) {
                    int iconY=d->rcItem.top+(d->rcItem.bottom-d->rcItem.top-g_appSize)/2;
                    DrawIconEx(d->hDC,d->rcItem.left+10,iconY,fileInfo.hIcon,g_appSize,g_appSize,0,nullptr,DI_NORMAL);
                    DestroyIcon(fileInfo.hIcon);
                }
                RECT nameRect=d->rcItem; nameRect.left+=g_appSize+18; nameRect.right-=42;
                SetTextColor(d->hDC,RGB(242,245,251));
                DrawTextW(d->hDC,g_apps[index].name.c_str(),-1,&nameRect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
                if(g_apps[index].count) {
                    RECT countRect=d->rcItem; countRect.left=countRect.right-42; countRect.right-=10;
                    SetTextColor(d->hDC,RGB(143,158,181));
                    std::wstring count=std::to_wstring(g_apps[index].count);
                    DrawTextW(d->hDC,count.c_str(),-1,&countRect,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
                }
            }
        }
        SelectObject(d->hDC,oldFont); DeleteObject(font);
        return TRUE;
    }
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r);
        HBRUSH bg=CreateSolidBrush(RGB(20,25,36)); FillRect(dc,&r,bg); DeleteObject(bg);
        RECT accent={14,0,r.right-14,3};
        HBRUSH accentBrush=CreateSolidBrush(g_accentColor); FillRect(dc,&accent,accentBrush); DeleteObject(accentBrush);
        HPEN border=CreatePen(PS_SOLID,1,RGB(69,81,102));
        HGDIOBJ oldPen=SelectObject(dc,border), oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,0,0,r.right-1,r.bottom-1,22,22);
        SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(border);
        SetBkMode(dc,TRANSPARENT);
        HFONT titleFont=CreateFontW(-16,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        HFONT oldFont=(HFONT)SelectObject(dc,titleFont);
        SetTextColor(dc,RGB(246,248,252));
        RECT title={16,7,r.right-112,28};
        DrawTextW(dc,g_expanded?L"FAVOURITES":L"APP DOCK",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_LEFT);
        SelectObject(dc,oldFont); DeleteObject(titleFont);
        EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_DESTROY){KillTimer(hwnd,ID_HOVER_TIMER);g_window=nullptr;return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static DWORD WINAPI UiThread(void*) {
    LoadAvailableApps();
    WNDCLASSW wc{}; wc.lpfnWndProc=PanelProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"WhFavouriteAppsPanel";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); RegisterClassW(&wc);
    int x=24,y=100;
    g_window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,L"WhFavouriteAppsPanel",L"Favourite Apps",
        WS_POPUP,x,y,g_width,260,nullptr,nullptr,wc.hInstance,nullptr);
    if(!g_window) return 1;
    ApplyTopmost();
    DWORD backdrop=3; // DWM transient backdrop (acrylic) on supported Windows versions.
    DwmSetWindowAttribute(g_window,(DWMWINDOWATTRIBUTE)38,&backdrop,sizeof(backdrop));
    DWORD cornerPreference=2; // Rounded corners on Windows 11.
    DwmSetWindowAttribute(g_window,(DWMWINDOWATTRIBUTE)33,&cornerPreference,sizeof(cornerPreference));
    ShowWindow(g_window,SW_SHOWNOACTIVATE); UpdateWindow(g_window);
    RegisterHotKey(g_window,1,MOD_CONTROL|MOD_ALT,VK_SPACE);
    MSG msg;
    while(WaitForSingleObject(g_stop,0)!=WAIT_OBJECT_0) {
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_HOTKEY) {
                if(IsWindowVisible(g_window)) ShowWindow(g_window,SW_HIDE);
                else {ShowWindow(g_window,SW_SHOWNOACTIVATE);ApplyTopmost();}
            }
            else {TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        WaitForSingleObject(g_stop,25);
    }
    UnregisterHotKey(g_window,1); DestroyWindow(g_window); return 0;
}
BOOL WhTool_ModInit() {
    LoadSettings(); g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr); if(!g_stop) return FALSE;
    g_thread=CreateThread(nullptr,0,UiThread,nullptr,0,nullptr); return g_thread!=nullptr;
}
void WhTool_ModSettingsChanged() { LoadSettings(); if(g_window) PostMessageW(g_window,WM_REBUILD,0,0); }
void WhTool_ModUninit() { if(g_stop) SetEvent(g_stop); if(g_thread){WaitForSingleObject(g_thread,3000);CloseHandle(g_thread);} if(g_stop) CloseHandle(g_stop); SaveCounts(); SaveAddedApps(); }

// Legacy-compatible tool-mod launcher. Windhawk 2.0 also recognizes this target.
bool g_isToolModProcessLauncher; HANDLE g_toolModProcessMutex;
void WINAPI EntryPoint_Hook(){ExitThread(0);}
BOOL Wh_ModInit(){
 DWORD sessionId; if(ProcessIdToSessionId(GetCurrentProcessId(),&sessionId)&&sessionId==0)return FALSE;
 bool excluded=false,isTool=false,isThis=false; int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv)return FALSE;
 for(int i=1;i<argc;i++) if(!_wcsicmp(argv[i],L"-service")||!_wcsicmp(argv[i],L"-service-start")||!_wcsicmp(argv[i],L"-service-stop")){excluded=true;break;}
 for(int i=1;i<argc-1;i++) if(!_wcsicmp(argv[i],L"-tool-mod")){isTool=true;if(!_wcsicmp(argv[i+1],WH_MOD_ID))isThis=true;break;}
 LocalFree(argv); if(excluded)return FALSE;
 if(isThis){g_toolModProcessMutex=CreateMutexW(nullptr,TRUE,L"windhawk-tool-mod_" WH_MOD_ID);if(!g_toolModProcessMutex||GetLastError()==ERROR_ALREADY_EXISTS)ExitProcess(1);if(!WhTool_ModInit())ExitProcess(1);
  auto base=(IMAGE_DOS_HEADER*)GetModuleHandleW(nullptr); auto nt=(IMAGE_NT_HEADERS*)((BYTE*)base+base->e_lfanew); Wh_SetFunctionHook((BYTE*)base+nt->OptionalHeader.AddressOfEntryPoint,(void*)EntryPoint_Hook,nullptr);return TRUE;}
 if(isTool)return FALSE; g_isToolModProcessLauncher=true; return TRUE;
}
void Wh_ModAfterInit(){if(!g_isToolModProcessLauncher)return; WCHAR path[MAX_PATH];if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return;
 WCHAR cmd[MAX_PATH+128];swprintf_s(cmd,L"\"%s\" -tool-mod \"" WH_MOD_ID L"\"",path);
 HMODULE k=GetModuleHandleW(L"kernelbase.dll");if(!k)k=GetModuleHandleW(L"kernel32.dll");if(!k)return;
 using Fn=BOOL(WINAPI*)(HANDLE,LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION,PHANDLE);
 auto create=(Fn)GetProcAddress(k,"CreateProcessInternalW");if(!create)return; STARTUPINFOW si{};si.cb=sizeof(si); PROCESS_INFORMATION pi{};
 if(create(nullptr,path,cmd,nullptr,nullptr,FALSE,NORMAL_PRIORITY_CLASS,nullptr,nullptr,&si,&pi,nullptr)){CloseHandle(pi.hProcess);CloseHandle(pi.hThread);}}
void Wh_ModSettingsChanged(){if(!g_isToolModProcessLauncher)WhTool_ModSettingsChanged();}
void Wh_ModUninit(){if(!g_isToolModProcessLauncher){WhTool_ModUninit();ExitProcess(0);}}
