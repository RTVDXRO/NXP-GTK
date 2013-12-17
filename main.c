/*
 *  XDR-GTK v0.2
 *  Copyright (C) 2012-2013  Konrad Kosmatka
 *  http://redakcja.radiopolska.pl/konrad/

 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <gtk/gtk.h>
#include "gui.h"
#include "settings.h"
#include "graph.h"
#ifdef G_OS_WIN32
#define _WIN32_WINNT 0x0500
#include "Windows.h"
#endif

gint main(gint argc, gchar* argv[])
{
    #ifdef G_OS_WIN32
    gint font = AddFontResourceEx(FONT_FILE, FR_PRIVATE, NULL);
    #endif
    gtk_disable_setlocale();
    gtk_init(&argc, &argv);
    settings_read();
    gui_init();
    gtk_main();
    g_free(rssi);
    #ifdef G_OS_WIN32
    if(font)
    {
        RemoveFontResourceEx(FONT_FILE, FR_PRIVATE, NULL);
    }
    #endif
    return 0;
}
