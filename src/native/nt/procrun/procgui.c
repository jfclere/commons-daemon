/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2003 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 * Portions of this software are based upon public domain software
 * originally written at the National Center for Supercomputing Applications,
 * University of Illinois, Urbana-Champaign.
 */

/* ====================================================================
 * procrun
 *
 * Contributed by Mladen Turk <mturk@apache.org>
 *
 * 05 Aug 2002
 * ==================================================================== 
 */

#if defined(PROCRUN_WINAPP)

#ifndef STRICT
#define STRICT
#endif
#ifndef OEMRESOURCE
#define OEMRESOURCE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objbase.h>
#include <shlobj.h>
#include <stdlib.h>
#include <stdio.h>

#include <Shlwapi.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <time.h>
#include <stdarg.h>
#include <jni.h>
 
#include "procrun.h"

#define WM_TRAYMESSAGE         (WM_APP+1)
#define WM_TIMER_TIMEOUT       10
#define MAX_LOADSTRING         200
#define MAX_LISTCOUNT          8192
#define CONWRAP_SUCCESS        0
#define CONWRAP_ENOARGS        1
#define CONWRAP_EARG           2
#define CONWRAP_EFATAL         3
#define TIMEOUT_TIME           20000    /* startup timeout (ms) */

#ifndef PSH_NOCONTEXTHELP
#define PSH_NOCONTEXTHELP       0x02000000
#endif

#define MAX_LIST_ITEMS         4000     /* maximum items in ListView */
 
extern int g_proc_mode;
/* The main envronment for services */
extern procrun_t *g_env;

int                    ac_use_try = 0;
int                    ac_use_dlg = 0;
int                    ac_use_show = 0;
int                    ac_use_splash = 0;
int                    ac_use_props = 0;
int                    ac_use_lview = 1;

HINSTANCE              ac_instance;
static procrun_t       *ac_env = NULL;
static HICON           ac_main_icon;
static HICON           ac_try_icon;
static HICON           ac_try_stop;
static UINT            ac_taskbar_created;
HWND                   ac_main_hwnd;
static HWND            ac_list_hwnd;
static HWND            ac_splist_hwnd;
static HWND            ac_console_hwnd = NULL;
static char            *ac_stdout_lines[MAX_LISTCOUNT + 1];
char                   *ac_cmdline;
char                   *ac_cmdname;
RECT                   ac_winpos = {-1, 0, 640, 480};
/* splash window handle */
static HWND            ac_splash_hwnd = NULL;

int                    ac_lview_current = 0;
static struct {
    char *  label;
    DWORD   width;
} ac_columns[] = {
    {   "Status",   140     },
    {   "Class",    172     },
    {   "Message",  300     },
};


INT_PTR ac_show_properties(HWND owner);


void ac_show_try_icon(HWND hwnd, DWORD message, const char *tip, int stop)
{
    
    NOTIFYICONDATA nid;
    if (!ac_use_try)
        return;

    ZeroMemory(&nid,sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 0xFF;
    nid.uFlags = NIF_ICON | NIF_MESSAGE;
    if (tip)
        nid.uFlags |= NIF_TIP;

    nid.uCallbackMessage = WM_TRAYMESSAGE;
    
    if (message != NIM_DELETE)
        nid.hIcon = stop ? ac_try_stop : ac_try_icon;
    else
        nid.hIcon = NULL;
    if (tip)
        strcpy(nid.szTip, tip);

    Shell_NotifyIcon(message, &nid);
}

static char *ac_lv_stat = NULL;
static char ac_lv_clbuf[1024] = {0};
static char *ac_lv_class = NULL;
static char *ac_lv_clmsg = NULL;
static int  ac_lv_iicon = 0;

#define STR_NOT_NULL(s) { if((s) == NULL) (s) = ""; }

static void ac_parse_list_string(const char *str)
{
    int row = 0x7FFFFFFF;
    LV_ITEM lvi;
    int off = 0;

    if (isdigit(*str)) {
        char *p;
                
        strncpy(ac_lv_clbuf, str, 1023);
        ac_lv_stat = p = &ac_lv_clbuf[0];

        while (*p && !isspace(*p))
            ++p;
        ++p;
        while (*p && !isspace(*p))
            ++p;
        *(p++) = 0;
        while (*p && isspace(*p))
            ++p;
        ac_lv_class = p;

        while (*p && !isspace(*p))
            ++p;
        *(p++) = 0;
        ac_lv_clmsg = p;
        
    }
    else {
        if (STRN_COMPARE(str, "INFO:")) {
            ac_lv_iicon = 0;
            off = STRN_SIZE("INFO:") + 1;
        }
        else if (STRN_COMPARE(str, "WARNING:")) {
            ac_lv_iicon = 1;
            off = STRN_SIZE("WARNING:") + 1;
        }
        else if (STRN_COMPARE(str, "ERROR:")) {
            ac_lv_iicon = 2;
            off = STRN_SIZE("ERROR:") + 1;
        }
        else if (STRN_COMPARE(str, "SEVERE:")) {
            ac_lv_iicon = 2;
            off = STRN_SIZE("SEVERE:") + 1;
        }
        ac_lv_clmsg = (char *)str + off;

        /* skip leading spaces */
        while (*ac_lv_clmsg && isspace(*ac_lv_clmsg))
            ++ac_lv_clmsg;
    }

    STR_NOT_NULL(ac_lv_class);
    STR_NOT_NULL(ac_lv_clmsg);
    STR_NOT_NULL(ac_lv_stat);

    memset(&lvi, 0, sizeof(LV_ITEM));
    lvi.mask        = LVIF_IMAGE | LVIF_TEXT;
    lvi.iItem       = ac_lview_current;
    lvi.iImage      = ac_lv_iicon;
    lvi.pszText     = ac_lv_stat;
    lvi.cchTextMax  = strlen(ac_lv_stat) + 1;
    row = ListView_InsertItem(ac_list_hwnd, &lvi);
    if (row == -1)
        return;
    ListView_SetItemText(ac_list_hwnd, row, 1, ac_lv_class); 
    ListView_SetItemText(ac_list_hwnd, row, 2, ac_lv_clmsg);
    ListView_EnsureVisible(ac_list_hwnd,
                               ListView_GetItemCount(ac_list_hwnd) - 1,
                               FALSE); 

    ac_lview_current++;
}

void ac_add_list_string(const char *str, int len)
{
    static int nqueue = 0;
    static int nlen = 0, olen = 0;
    static int litems = 0;
    int i;

    if (str) {

        /* set the status to 'green' when received Jk running 
         * and close the spash window if present
         */
        if (STRN_COMPARE(str, "INFO: Jk running")) {
            ac_show_try_icon(ac_main_hwnd, NIM_MODIFY, ac_cmdname, 0);
            /* kill the splash window if present */
            if (ac_splash_hwnd)
                EndDialog(ac_splash_hwnd, TRUE);
            ac_splash_hwnd = NULL;
        }
        else if (ac_splash_hwnd) {
            SendMessage(ac_splist_hwnd, LB_INSERTSTRING, 0, (LPARAM)str);
        }
        if (nqueue > MAX_LISTCOUNT - 1) {
            free(ac_stdout_lines[0]);
            /* TODO: improve performance */
            for (i = 1; i < MAX_LISTCOUNT; i++)
                ac_stdout_lines[i - 1] = ac_stdout_lines[i];
            --nqueue;
        }
        ac_stdout_lines[nqueue++] = strdup(str);
        nlen = max(nlen, len);
    }
    if (!ac_list_hwnd || !nqueue)
        return;

    if (ac_use_lview) {
        for (i = 0; i < nqueue; i++) {
            ac_parse_list_string(ac_stdout_lines[i]);
            if (litems++ > MAX_LIST_ITEMS)
                ListView_DeleteItem(ac_list_hwnd, 0);
        }
    }
    else {
        for (i = 0; i < nqueue; i++)
            ListBox_AddString(ac_list_hwnd, ac_stdout_lines[i]);
        
        if (olen < nlen) {
            olen = nlen;
            SendMessage(ac_list_hwnd, LB_SETHORIZONTALEXTENT,
                        (WPARAM) 10 * olen, (LPARAM) 0);
        }
    }
    for (i = 0; i < nqueue; i++) {
        free(ac_stdout_lines[i]);
        ac_stdout_lines[i] = NULL;
    }
    nqueue = 0;
}

static void ac_append_menu_item(HMENU menu, UINT menu_id, char *name, int isdef, int enabled)
{
    MENUITEMINFO mii;
    
    ZeroMemory(&mii, sizeof(MENUITEMINFO));
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    if (strlen(name)) {
        mii.fType = MFT_STRING;
        mii.wID = menu_id;
        if (isdef)
            mii.fState = MFS_DEFAULT;
        if (!enabled)
            mii.fState |= MFS_DISABLED;
        mii.dwTypeData = name;
    }
    else
        mii.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, menu_id, FALSE, &mii);
}

static void ac_show_try_menu(HWND hwnd)
{
    HMENU menu;
    POINT pt;
    char tmp[MAX_LOADSTRING];

    if (!ac_use_try)
        return;
    menu = CreatePopupMenu();
    if (menu) {
        if (ac_use_dlg) {
            ac_append_menu_item(menu, IDM_CONSOLE, "Open Console Monitor", 1, 1);
            ac_append_menu_item(menu, 0, "", 0, 1);
        }
        ac_append_menu_item(menu, IDM_ABOUT, "About", 0, 1);
        ac_append_menu_item(menu, IDM_OPTIONS, "Properties", 0, 1);
        strcpy(tmp, "Shutdown: ");
        strcat(tmp, g_env->m->service.name);
        ac_append_menu_item(menu, IDM_EXIT,  tmp, 0, 1);

        if (!SetForegroundWindow(hwnd))
            SetForegroundWindow(NULL);
        GetCursorPos(&pt);
        TrackPopupMenu(menu, TPM_LEFTALIGN|TPM_RIGHTBUTTON, 
                       pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(menu);
    }
}

static int ac_copy_to_clipboard()
{
    HGLOBAL hglbcopy = NULL; 
    DWORD sel, i;
    char buf[MAX_PATH + 2], *str; 
    int *si = NULL;
    
    if (!OpenClipboard(NULL)) 
        return -1; 
    EmptyClipboard(); 

    if (ac_use_lview) {
        sel = ListView_GetSelectedCount(ac_list_hwnd);
        if (sel != LB_ERR && sel > 0) {
            int curr;
            str = malloc((MAX_PATH+4)*sel);
            str[0] = '\0';
            curr = ListView_GetNextItem(ac_list_hwnd, -1, LVNI_SELECTED);
            for (i = 0; i < sel && curr >= 0; i++) {
                int j;
                for(j=0; j < 3; j++) {
                    ListView_GetItemText(ac_list_hwnd, curr, j, buf, MAX_PATH);
                    strcat(buf, j < 2 ? "\t" : "\r\n");
                    strcat(str, buf);
                }
                curr = ListView_GetNextItem(ac_list_hwnd, curr, LVNI_SELECTED);
            }
            sel = strlen(str);
            if(sel > 0) {
                hglbcopy = GlobalAlloc(GMEM_MOVEABLE, sel+1);
                strcpy(GlobalLock(hglbcopy), str);
            }
            free(str);

        }
    }
    else {
        sel = SendMessage(ac_list_hwnd, LB_GETSELCOUNT, (WPARAM)0, (LPARAM)0);
        if (sel != LB_ERR && sel > 0) {
            si = (int *)malloc(sel * sizeof(int));
            str = malloc((MAX_PATH + 2) * sel); 
            str[0] = '\0';
            SendMessage(ac_list_hwnd, LB_GETSELITEMS, (WPARAM)sel, (LPARAM)si);
            for (i = 0; i < sel; i++) {
                SendMessage(ac_list_hwnd, LB_GETTEXT, (WPARAM)si[i], (LPARAM)buf);
                strcat(buf, "\r\n");
                strcat(str, buf);
            }
            sel = strlen(str);
            if(sel > 0) {
                hglbcopy = GlobalAlloc(GMEM_MOVEABLE, sel+1);
                strcpy(GlobalLock(hglbcopy), str);
            }
            free(str);
            free(si);
        }
    }

    if(hglbcopy != NULL) {
        GlobalUnlock(hglbcopy);  
        SetClipboardData(CF_TEXT, hglbcopy); 
    }
    CloseClipboard();
    return 0;
}

static void ac_center_window(HWND hwnd)
{
   RECT    rc, rw;
   int     cw, ch;
   int     x, y;
   if (!ac_use_try)
       return;

   /* Get the Height and Width of the child window */
   GetWindowRect(hwnd, &rc);
   cw = rc.right - rc.left;
   ch = rc.bottom - rc.top;

   /* Get the limits of the 'workarea' */
   if (!SystemParametersInfo(
            SPI_GETWORKAREA,
            sizeof(RECT),
            &rw, 0)) {
      rw.left = rw.top = 0;
      rw.right = GetSystemMetrics(SM_CXSCREEN);
      rw.bottom = GetSystemMetrics(SM_CYSCREEN);
   }

   /* Calculate new X and Y position*/
   x = (rw.right - cw)/2;
   y = (rw.bottom - ch)/2;
   SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
}

static void ac_calc_center()
{
   RECT    rWorkArea;
   BOOL    bResult;


   /* Get the limits of the 'workarea' */
   bResult = SystemParametersInfo(
      SPI_GETWORKAREA,
      sizeof(RECT),
      &rWorkArea,
      0);
   if (!bResult) {
      rWorkArea.left = rWorkArea.top = 0;
      rWorkArea.right = GetSystemMetrics(SM_CXSCREEN);
      rWorkArea.bottom = GetSystemMetrics(SM_CYSCREEN);
   }

   /* Calculate new X and Y position*/
   ac_winpos.left = (rWorkArea.right - ac_winpos.right) / 2;
   ac_winpos.top  = (rWorkArea.bottom - ac_winpos.bottom) / 2;
}

LRESULT CALLBACK ac_about_dlg_proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hrich;
    HRSRC rsrc;
    HGLOBAL glob;
    char *txt;

    switch (message) {
    case WM_INITDIALOG:
        ac_center_window(hDlg);
        hrich = GetDlgItem(hDlg, IDC_RICHEDIT21);
        rsrc = FindResource(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_RTFLIC), "RTF");
        glob = LoadResource(GetModuleHandle(NULL), rsrc);
        txt = (char *)LockResource(glob);
        SendMessage(hrich, WM_SETTEXT, 0, (LPARAM)txt);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LRESULT CALLBACK ac_console_dlg_proc(HWND hdlg, UINT message, WPARAM wparam, LPARAM lparam)
{

    RECT r, m;
    static      HWND  status_bar; 

    switch (message) {
        case WM_INITDIALOG:
           ac_console_hwnd = hdlg;
           SetWindowText(hdlg, ac_cmdname);
           ac_list_hwnd = GetDlgItem(hdlg, IDL_STDOUT);
           if (ac_use_lview)
               ShowWindow(ac_list_hwnd, SW_HIDE);
           status_bar = CreateStatusWindow(0x0800 /* SBT_TOOLTIPS */
                                          | WS_CHILD | WS_VISIBLE,
                                            ac_cmdline, hdlg, IDC_STATBAR);
  
           if (!ac_use_try) { 
               LONG w = GetWindowLong(hdlg, GWL_STYLE);
               w &= ~WS_MINIMIZEBOX;
               SetWindowLong(hdlg, GWL_STYLE, w);
           }
           
           if (ac_winpos.left < 0)
               ac_calc_center();

           SetWindowPos(hdlg, HWND_TOP, ac_winpos.left, ac_winpos.top,
                        ac_winpos.right, ac_winpos.bottom, SWP_SHOWWINDOW);
                        
           GetWindowRect(status_bar, &r);
           GetClientRect(hdlg, &m);
           if (!ac_use_lview)
               MoveWindow(ac_list_hwnd, 0, 0, m.right - m.left, m.bottom - abs((r.top - r.bottom)), TRUE);
           else {
               LV_COLUMN lvc;
               int i;
               HIMAGELIST  imlist;
               HICON hicon; 
               imlist = ImageList_Create(16, 16, ILC_COLORDDB | ILC_MASK, 3, 0);
               hicon = LoadImage(ac_instance, MAKEINTRESOURCE(IDI_ICOI),
                                 IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
               ImageList_AddIcon(imlist, hicon);
               hicon = LoadImage(ac_instance, MAKEINTRESOURCE(IDI_ICOW),
                                 IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
               ImageList_AddIcon(imlist, hicon);
               hicon = LoadImage(ac_instance, MAKEINTRESOURCE(IDI_ICOS),
                                 IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
               ImageList_AddIcon(imlist, hicon);

               ac_list_hwnd = CreateWindowEx(0L, WC_LISTVIEW, "", 
                                             WS_VISIBLE | WS_CHILD |
                                             LVS_REPORT | WS_EX_CLIENTEDGE,
                                             0, 0, m.right - m.left,
                                             m.bottom - abs((r.top - r.bottom)),
                                             hdlg, NULL, ac_instance, NULL);
               lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
               lvc.fmt  = LVCFMT_LEFT;

               ListView_SetImageList(ac_list_hwnd,imlist, LVSIL_SMALL);

               for (i = 0; i < sizeof(ac_columns) / sizeof(ac_columns[0]); ++i)  {
                    lvc.iSubItem    = i;
                    lvc.cx          = ac_columns[i].width;
                    lvc.pszText     = ac_columns[i].label;
                    ListView_InsertColumn(ac_list_hwnd, i, &lvc );
               }
#ifdef LVS_EX_FULLROWSELECT
               ListView_SetExtendedListViewStyleEx(ac_list_hwnd, 0,
                                                   LVS_EX_FULLROWSELECT |
                                                   LVS_EX_INFOTIP);
#endif
           }
           ac_add_list_string(NULL, 0);
           break;
        case WM_SIZE:
            switch (LOWORD(wparam)) { 
                case SIZE_MINIMIZED:
                    GetWindowRect(hdlg, &ac_winpos);
                    if (!ac_use_try) {
                        ShowWindow(hdlg, SW_RESTORE);
                        return FALSE;
                    }
                    ShowWindow(hdlg, SW_HIDE);
                    return TRUE; 
                break;
                default:
                    GetWindowRect(status_bar, &r);
                    MoveWindow(status_bar, 0, HIWORD(lparam) - (r.top - r.bottom),
                                              LOWORD(lparam), (r.top - r.bottom), TRUE);
                    GetClientRect(hdlg, &m);
                    MoveWindow(ac_list_hwnd, 0, 0, LOWORD(lparam), HIWORD(lparam) - abs((r.top - r.bottom)), TRUE);
                    break;
            }
        break;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDM_MENU_EXIT:
                    EndDialog(hdlg, TRUE);
                    ac_list_hwnd = NULL;
                    ac_console_hwnd = NULL;
                    ac_show_try_icon(ac_main_hwnd, NIM_MODIFY, ac_cmdname, 1);
                    SetEvent(g_env->m->events[0]);
                break;
                case IDM_MENU_EDIT:
                    ac_copy_to_clipboard();
                break;
                case IDM_MENU_ABOUT:
                    DialogBox(ac_instance, MAKEINTRESOURCE(IDD_ABOUTBOX),
                              hdlg, (DLGPROC)ac_about_dlg_proc);
                break;
            }
            break;
        case WM_QUIT:
        case WM_CLOSE:
            GetWindowRect(hdlg, &ac_winpos);
            if (!ac_use_try) {
                EndDialog(hdlg, TRUE);
                ac_list_hwnd = NULL;
                ac_console_hwnd = NULL;
                SetEvent(g_env->m->events[0]);
                PostQuitMessage(CONWRAP_SUCCESS);
            }
            else
                ShowWindow(ac_console_hwnd, SW_HIDE);
            return TRUE;
        default:
            return FALSE;
    }

    return FALSE;
}

LRESULT CALLBACK ac_splash_dlg_proc(HWND hdlg, UINT message, WPARAM wparam, LPARAM lparam)
{

    switch (message) {
        case WM_INITDIALOG:
           ac_splash_hwnd = hdlg;
           ac_center_window(hdlg);
           ac_splist_hwnd = GetDlgItem(hdlg, IDL_INFO); 
           break;
    }

    return FALSE;
}

int ac_browse_for_dialog(HWND hwnd, char *str, size_t len, int files)
{
    int rv = 0;

    BROWSEINFO bi;
    ITEMIDLIST *il , *ir;
    LPMALLOC pMalloc;
    
    memset(&bi, 0, sizeof(BROWSEINFO));
    SHGetSpecialFolderLocation(hwnd, CSIDL_DRIVES, &il);
    if (files)
        bi.lpszTitle  = "Tomcat Service Manager :\nSelect Folder!";
    else
        bi.lpszTitle  = "Tomcat Service Manager :\nSelect File!";
    bi.pszDisplayName = str;
    bi.hwndOwner =      hwnd;
    bi.ulFlags =        BIF_EDITBOX;
    if (files)
        bi.ulFlags |=   BIF_BROWSEINCLUDEFILES;

    bi.lpfn =           NULL;
    bi.lParam =         0;
    bi.iImage =         0;
    bi.pidlRoot =       il;
    
    if ((ir = SHBrowseForFolder(&bi)) != NULL) {
        SHGetPathFromIDList(ir, str);

        rv = 1;
    }
    if (SHGetMalloc(&pMalloc)) {
        pMalloc->lpVtbl->Free(pMalloc, il);
        pMalloc->lpVtbl->Release(pMalloc);
    }
    return rv;
    
}

void CALLBACK PropSheetCallback(HWND hwndPropSheet, UINT uMsg, LPARAM lParam)
{
    switch(uMsg) {
        case PSCB_PRECREATE:       
           {
                LPDLGTEMPLATE  lpTemplate = (LPDLGTEMPLATE)lParam;
                if (!(lpTemplate->style & WS_SYSMENU))
                    lpTemplate->style |= WS_SYSMENU;

            }
        break;
        case PSCB_INITIALIZED:
        break;

    }
}

LRESULT CALLBACK dlg_service_proc(HWND hdlg,
                                  UINT uMessage,
                                  WPARAM wParam,
                                  LPARAM lParam)
{

    LPNMHDR     lpnmhdr;
    int argc = 2;
    char *argv[10];
    char txt[4096];
    process_t p;

    switch (uMessage) {
        case WM_COMMAND:
            PropSheet_Changed(GetParent(hdlg), hdlg);
            switch (LOWORD(wParam))  {
                case RC_BTN_BIP:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 1))
                        SetDlgItemText(hdlg, RC_TXT_IP,
                                       txt);
                    return TRUE;
                break;
                case RC_BTN_BWP:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 0))
                        SetDlgItemText(hdlg, RC_TXT_WP,
                                       txt);
                    return TRUE;
                break;
            }
        break;
        case WM_INITDIALOG:
            ac_center_window(GetParent(hdlg));
            SetDlgItemText(hdlg, RC_TXT_SN, g_env->m->service.display ? 
                           g_env->m->service.display : g_env->m->service.name);
            SetDlgItemText(hdlg, RC_TXT_SD, g_env->m->service.description);
            SetDlgItemText(hdlg, RC_TXT_IP, g_env->m->service.image);
            SetDlgItemText(hdlg, RC_TXT_WP, g_env->m->service.path);
            SetDlgItemText(hdlg, RC_TXT_UN, g_env->m->service.account);
            SetDlgItemText(hdlg, RC_TXT_UP, g_env->m->service.password);
            if (g_env->m->service.startup == SERVICE_AUTO_START)
                CheckDlgButton(hdlg, RC_CHK_AUTO, BST_CHECKED);

        break;

        case WM_NOTIFY:
            lpnmhdr = (NMHDR FAR *)lParam;

            switch (lpnmhdr->code) {
                case PSN_APPLY:   /* sent when OK or Apply button pressed */
                memcpy(&p, g_env->m, sizeof(process_t));
                p.pool = pool_create();
                p.service.name = g_env->m->service.name;
                argc = 2;
                if (GetDlgItemText(hdlg, RC_TXT_SD, txt, 4095) > 0) {
                    argv[argc++] = "--" PROCRUN_PARAMS_DESCRIPTION;
                    argv[argc++] = &txt[0];
                    argv[argc] = NULL;
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_IP, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_IMAGE;
                    argv[argc++] = &txt[0];
                    argv[argc] = NULL;
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_WP, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_WORKPATH;
                    argv[argc++] = &txt[0];
                    argv[argc] = NULL;
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_UN, txt, 64) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_ACCOUNT;
                    argv[argc++] = &txt[0];
                    argv[argc] = NULL;
                    if (GetDlgItemText(hdlg, RC_TXT_UP, &txt[128], 64) > 0) {
                        argv[argc++] = "--" PROCRUN_PARAMS_PASSWORD;
                        argv[argc++] = &txt[128];
                        procrun_update_service(&p, argc, argv);
                    }
                }
                else if (g_env->m->service.account) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_ACCOUNT;
                    argv[argc++] = "-";
                    argv[argc++] = "--" PROCRUN_PARAMS_PASSWORD;
                    argv[argc++] = "-";
                    argv[argc] = NULL;
                    procrun_update_service(&p, argc, argv);
                }
                /* TODO: check if the param is changed */
                argc = 2;
                argv[argc++] = "--" PROCRUN_PARAMS_STARTUP;
                if (IsDlgButtonChecked(hdlg, RC_CHK_AUTO))
                    argv[argc++] = "auto";
                else
                    argv[argc++] = "manual";

                argv[argc] = NULL;
                procrun_update_service(&p, argc, argv);
                pool_destroy(p.pool);
                break;
                case PSN_RESET:   /* sent when Cancel button pressed */
                break;         
                default:
                break;
            }
        break;
        default:
        break;
    }

    return FALSE;
}

LRESULT CALLBACK dlg_java_proc(HWND hdlg,
                               UINT uMessage,
                               WPARAM wParam,
                               LPARAM lParam)
{

    LPNMHDR     lpnmhdr;
    int argc = 2;
    char *argv[10];
    char txt[4096];
    process_t p;
    char *s, *d;

    switch (uMessage) {
        case WM_COMMAND:
            PropSheet_Changed(GetParent(hdlg), hdlg);
            switch (LOWORD(wParam))  {
                case RC_BTN_JVM:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 1))
                        SetDlgItemText(hdlg, RC_TXT_JVM,
                                       txt);
                    return TRUE;
                break;
            }

        break;
        case WM_INITDIALOG:
            SetDlgItemText(hdlg, RC_TXT_JVM, g_env->m->java.display);
            sprintf(txt, "%s;%s;%s", g_env->m->java.start_class,
                                     g_env->m->java.start_method,
                                     g_env->m->java.start_param);
            SetDlgItemText(hdlg, RC_TXT_SC, txt);
            sprintf(txt, "%s;%s;%s", g_env->m->java.stop_class,
                                     g_env->m->java.stop_method,
                                     g_env->m->java.stop_param);
            SetDlgItemText(hdlg, RC_TXT_EC, txt);
            if (g_env->m->java.display &&
                !strcmp(g_env->m->java.display, "auto"))
                CheckDlgButton(hdlg, RC_CHK_JVM, BST_CHECKED);
            
            memset(txt, 0, 4096);
            d = &txt[0];
            for (s = g_env->m->java.opts; s && *s; s++) {
                sprintf(d, "%s\r\n", s);
                d += strlen(d);
                while (*s)
                    s++;
            }
            SetDlgItemText(hdlg, RC_TXT_JO, txt);
        break;
        case WM_NOTIFY:
            lpnmhdr = (NMHDR FAR *)lParam;

            switch (lpnmhdr->code) {
                case PSN_APPLY:   /* sent when OK or Apply button pressed */
                memcpy(&p, g_env->m, sizeof(process_t));
                argc = 2;
                p.pool = pool_create();
                if (IsDlgButtonChecked(hdlg, RC_CHK_JVM)) {
                    argv[argc++] = "--" PROCRUN_PARAMS_JVM;
                    argv[argc++] = "auto";
                }
                else if (GetDlgItemText(hdlg, RC_TXT_JVM, txt, 4095) > 0) {
                    argv[argc++] = "--" PROCRUN_PARAMS_JVM;
                    argv[argc++] = &txt[0];
                }
                if (argc > 2)
                    procrun_update_service(&p, argc, argv);

                if (GetDlgItemText(hdlg, RC_TXT_SC, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_STARTCLASS;
                    argv[argc++] = &txt[0];
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_EC, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_STOPCLASS;
                    argv[argc++] = &txt[0];
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_JO, txt, 4095) > 0) {
                    char *c = &txt[0];
                    char b[4096] = {0};
                    int i = 0;
                    argc = 2;
                    while (*c) {
                        if (*c == '\n')
                            b[i++] = '#';
                        else if (*c != '\r')
                            b[i++] = *c;
                        ++c;
                    }
                    b[i] = '\0';
                    argv[argc++] = "--" PROCRUN_PARAMS_JVM_OPTS;
                    argv[argc++] = &b[0];
                    procrun_update_service(&p, argc, argv);
                }
                pool_destroy(p.pool);
                break;
                case PSN_RESET:   /* sent when Cancel button pressed */
                break;         
                default:
                break;
            }
        break;
        default:
        break;
    }

    return FALSE;
}

LRESULT CALLBACK dlg_stream_proc(HWND hdlg,
                                 UINT uMessage,
                                 WPARAM wParam,
                                 LPARAM lParam)
{

    LPNMHDR     lpnmhdr;
    int argc = 2;
    char *argv[10];
    char txt[4096];
    process_t p;

    switch (uMessage) {
        case WM_COMMAND:
            PropSheet_Changed(GetParent(hdlg), hdlg);
            switch (LOWORD(wParam))  {
                case RC_BTN_STDI:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 1))
                        SetDlgItemText(hdlg, RC_TXT_STDI,
                                       txt);
                    return TRUE;
                break;
                case RC_BTN_STDO:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 1))
                        SetDlgItemText(hdlg, RC_TXT_STDO,
                                       txt);
                    return TRUE;
                break;
                case RC_BTN_STDE:
                    if (ac_browse_for_dialog(hdlg, txt, 1024, 1))
                        SetDlgItemText(hdlg, RC_TXT_STDE,
                                       txt);
                    return TRUE;
                break;
            }

        break;
        case WM_INITDIALOG:
            SetDlgItemText(hdlg, RC_TXT_STDI, g_env->m->service.inname);
            SetDlgItemText(hdlg, RC_TXT_STDO, g_env->m->service.outname);
            SetDlgItemText(hdlg, RC_TXT_STDE, g_env->m->service.errname);

        break;

        case WM_NOTIFY:
            lpnmhdr = (NMHDR FAR *)lParam;

            switch (lpnmhdr->code) {
                case PSN_APPLY:   /* sent when OK or Apply button pressed */
                memcpy(&p, g_env->m, sizeof(process_t));
                p.pool = pool_create();
                if (GetDlgItemText(hdlg, RC_TXT_STDI, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_STDINFILE;
                    argv[argc++] = &txt[0];
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_STDO, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_STDOUTFILE;
                    argv[argc++] = &txt[0];
                    procrun_update_service(&p, argc, argv);
                }
                if (GetDlgItemText(hdlg, RC_TXT_STDE, txt, 4095) > 0) {
                    argc = 2;
                    argv[argc++] = "--" PROCRUN_PARAMS_STDERRFILE;
                    argv[argc++] = &txt[0];
                    procrun_update_service(&p, argc, argv);
                }
                pool_destroy(p.pool);

                break;
                case PSN_RESET:   /* sent when Cancel button pressed */
                break;         
                default:
                break;
            }
        break;
        default:
        break;
    }

    return FALSE;
}


INT_PTR ac_show_properties(HWND owner)
{
    PROPSHEETPAGE psp[3];
    PROPSHEETHEADER psh;
    char title[256];

    strcpy(title, ac_cmdline);
    strcat(title, " Service properties");
    psp[0].dwSize        = sizeof(PROPSHEETPAGE);
    psp[0].dwFlags       = PSP_USETITLE;
    psp[0].hInstance     = ac_instance;
    psp[0].pszTemplate   = MAKEINTRESOURCE(RC_DLG_SRVOPT);
    psp[0].pszIcon       = NULL;
    psp[0].pfnDlgProc    = (DLGPROC)dlg_service_proc;
    psp[0].pszTitle      = TEXT("Service");
    psp[0].lParam        = 0;

    psp[1].dwSize        = sizeof(PROPSHEETPAGE);
    psp[1].dwFlags       = PSP_USETITLE;
    psp[1].hInstance     = ac_instance;
    psp[1].pszTemplate   = MAKEINTRESOURCE(RC_DLG_JVMOPT);
    psp[1].pszIcon       = NULL;
    psp[1].pfnDlgProc    = (DLGPROC)dlg_java_proc;
    psp[1].pszTitle      = TEXT("Java VM");
    psp[1].lParam        = 0;

    psp[2].dwSize        = sizeof(PROPSHEETPAGE);
    psp[2].dwFlags       = PSP_USETITLE;
    psp[2].hInstance     = ac_instance;
    psp[2].pszTemplate   = MAKEINTRESOURCE(RC_DLG_STDOPT);
    psp[2].pszIcon       = NULL;
    psp[2].pfnDlgProc    = (DLGPROC)dlg_stream_proc;
    psp[2].pszTitle      = TEXT("Standard Streams");
    psp[2].lParam        = 0;

    psh.dwSize           = sizeof(PROPSHEETHEADER);
    psh.dwFlags          = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_USECALLBACK | PSH_NOCONTEXTHELP;
    psh.hwndParent       = owner;
    psh.hInstance        = ac_instance;
    psh.pszIcon          = MAKEINTRESOURCE(IDI_ICOCONWRAP);
#if (_WIN32_IE >= 0x0500)
    psh.pszbmHeader      = MAKEINTRESOURCE(IDB_BMPJAKARTA);
#endif
    psh.pszCaption       = title;
    psh.nPages           = sizeof(psp) / sizeof(PROPSHEETPAGE);
    psh.ppsp             = (LPCPROPSHEETPAGE) &psp;
    psh.pfnCallback      = (PFNPROPSHEETCALLBACK)PropSheetCallback;
    psh.nStartPage       = 0;
    return PropertySheet(&psh);
}



LRESULT CALLBACK ac_main_wnd_proc(HWND hwnd, UINT message,
                          WPARAM wparam, LPARAM lparam)
{
    if (message == ac_taskbar_created) {
        /* restore the tray icon on shell restart */
        ac_show_try_icon(hwnd, NIM_ADD, ac_cmdname, 0);
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
    switch (message) {
        case WM_CREATE:
            ac_main_hwnd = hwnd;
            if (ac_use_props) {
                PostMessage(hwnd, WM_COMMAND, IDM_OPTIONS, 0);
                return FALSE;
            }

            if (ac_use_try)
                ac_show_try_icon(hwnd, NIM_ADD, ac_cmdname, 1);
            /* add the 20 s timer for startup to avoid zombie spash 
             * if something goes wrong.
             */
            SetTimer(hwnd, WM_TIMER_TIMEOUT, TIMEOUT_TIME, NULL);
            if (ac_use_splash) {
                DialogBox(ac_instance, MAKEINTRESOURCE(IDD_DLGSPLASH),
                          hwnd, (DLGPROC)ac_splash_dlg_proc);
            }

            if (ac_use_show) {
                DialogBox(ac_instance, MAKEINTRESOURCE(IDD_DLGCONSOLE),
                          hwnd, (DLGPROC)ac_console_dlg_proc);
            }
            break;
        case WM_TIMER:
            switch (wparam) {
                case WM_TIMER_TIMEOUT:
                    if (ac_use_try)
                        ac_show_try_icon(hwnd, NIM_MODIFY, ac_cmdname, 0);
                    if (ac_use_splash && ac_splash_hwnd)
                        EndDialog(ac_splash_hwnd, TRUE);
                break;
            }
            break;
        case WM_DESTROY:
        case WM_QUIT:
            if (ac_use_try);
                ac_show_try_icon(hwnd, NIM_DELETE, NULL, 0);
            SetEvent(g_env->m->events[0]);
            break;
        case WM_TRAYMESSAGE:
            switch(lparam) {
                case WM_LBUTTONDBLCLK:
                   if (ac_console_hwnd) {
                        ShowWindow(ac_console_hwnd, SW_SHOW);
                        ShowWindow(ac_console_hwnd, SW_RESTORE);
                   }
                   else
                       DialogBox(ac_instance, MAKEINTRESOURCE(IDD_DLGCONSOLE),
                                 hwnd, (DLGPROC)ac_console_dlg_proc);                   
                break;
                case WM_RBUTTONUP:
                    ac_show_try_menu(hwnd);
                break;    
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
               case IDM_EXIT:
                   ac_show_try_icon(hwnd, NIM_MODIFY, ac_cmdname, 1);
                   SetEvent(g_env->m->events[0]);
                   return TRUE;
                   break;
               case IDM_CONSOLE:
                   if (ac_console_hwnd) {
                        ShowWindow(ac_console_hwnd, SW_SHOW);
                        ShowWindow(ac_console_hwnd, SW_RESTORE);
                   }
                   else
                       DialogBox(ac_instance, MAKEINTRESOURCE(IDD_DLGCONSOLE),
                                 hwnd, (DLGPROC)ac_console_dlg_proc);                   
                   break;
               case IDM_ABOUT:
                       DialogBox(ac_instance, MAKEINTRESOURCE(IDD_ABOUTBOX),
                                 hwnd, (DLGPROC)ac_about_dlg_proc);                   
                   break;
               case IDM_OPTIONS:
                    ac_show_properties(NULL);
                    if (ac_use_props)                               
                        PostMessage(hwnd, WM_QUIT, 0, 0);

                   break;
            }
        default:
            return DefWindowProc(hwnd, message, wparam, lparam);
    }

    return FALSE;
}

/* Create main invisible window */
static HWND ac_create_main_window(HINSTANCE instance, const char *wclass, const char *title)
{
    HWND       hwnd = NULL;
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX); 

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = ac_main_wnd_proc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = instance;
    wcex.hIcon          = ac_main_icon = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_ICOCONWRAP),
                                                IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = 0;
    wcex.lpszClassName  = wclass;
    wcex.hIconSm        = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_ICOCONWRAP),
                                           IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    ac_try_icon = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_ICOCONTRY),
                                           IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    ac_try_stop = (HICON)LoadImage(instance, MAKEINTRESOURCE(IDI_ICOCONTRYSTOP),
                                           IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (RegisterClassEx(&wcex))
        hwnd = CreateWindow(wclass, title,
                            0, 0, 0, 0, 0,
                            NULL, NULL, instance, NULL);

 
    return hwnd;

}

DWORD WINAPI gui_thread(LPVOID param)
{
    DWORD rv = 0;
    MSG   msg;
    /* single instance mutex */
    HANDLE mutex;

    procrun_t *env = ac_env = (procrun_t *)param;
    char cname[MAX_LOADSTRING];
    char cmutex[MAX_PATH];

    if (!param || !env->m->service.name)
        return -1;
    strcpy(cname, env->m->service.name);
    strcat(cname, "_CLASS");
    strcpy(cmutex, env->m->service.name);
    strcat(cmutex, "_MUTEX");
    if (env->m->service.description)
        ac_cmdline = env->m->service.description;
    else
        ac_cmdline = env->m->service.name;
    if (env->m->service.display)
        ac_cmdname = env->m->service.display;
    else
        ac_cmdname = env->m->service.name;
    
    mutex = CreateMutex(NULL, FALSE, cmutex);
    if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) {
        char msg[2048];
        sprintf(msg, "Starting: %s\nApplication is already running",
                env->m->service.name);
        MessageBox(NULL, msg, "Second instance", 
                   MB_OK | MB_ICONEXCLAMATION | MB_SYSTEMMODAL);
        if (mutex)
            CloseHandle(mutex);
        SetEvent(env->m->events[0]);
        return 0;
    }

    /* TODO: make that customizable */
    if (ac_use_try)
        ac_use_splash = 1;
    ac_main_hwnd = ac_create_main_window(ac_instance, cname, 
                                         env->m->service.name);

    InitCommonControls();
    if (ac_main_hwnd) {
        if (ac_use_try)
            ac_taskbar_created = RegisterWindowMessage("TaskbarCreated");
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }    
    }
    if (mutex)
        CloseHandle(mutex);
    ac_main_hwnd = NULL;
    SetEvent(env->m->events[0]);
    return rv;
}

#endif /* PROCRUN_WINAPP */
