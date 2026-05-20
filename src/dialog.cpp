#include "dialog.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static char s_result[4096];

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>

void openUrl(const char * url) {
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

const char * openFileDialog(const char * title) {
    s_result[0] = '\0';
    OPENFILENAMEA ofn = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = NULL;
    ofn.lpstrFilter     = "3D Models (*.gltf;*.glb;*.obj;*.ply;*.stl)\0*.gltf;*.glb;*.obj;*.ply;*.stl\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile       = s_result;
    ofn.nMaxFile        = sizeof(s_result);
    ofn.lpstrTitle      = title;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) ? s_result : NULL;
}

#else  /* Linux / macOS */

void openUrl(const char * url) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", url);
    system(cmd);
}

const char * openFileDialog(const char * title) {
    (void)title;
    s_result[0] = '\0';

    /* Try zenity (GNOME), then kdialog (KDE), then plain stdin fallback */
    const char * cmds[] = {
        "zenity --file-selection --file-filter='3D Models (gltf glb obj ply stl) | *.gltf *.glb *.obj *.ply *.stl' 2>/dev/null",
        "kdialog --getopenfilename . '*.gltf *.glb *.obj *.ply *.stl | 3D Models' 2>/dev/null",
        NULL
    };

    for(int i = 0; cmds[i]; ++i) {
        FILE * p = popen(cmds[i], "r");
        if(!p) continue;
        if(fgets(s_result, sizeof(s_result), p)) {
            pclose(p);
            /* strip trailing newline */
            size_t n = strlen(s_result);
            if(n && s_result[n-1] == '\n') s_result[n-1] = '\0';
            if(s_result[0]) return s_result;
        } else {
            pclose(p);
        }
    }
    return NULL;
}

#endif
