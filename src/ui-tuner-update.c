#include <math.h>
#include "ui.h"
#include "tuner.h"
#include "ui-tuner-set.h"
#include "ui-connect.h"
#include "ui-signal.h"
#include "stationlist.h"
#include "conf.h"
#include "pattern.h"
#include "scan.h"
#include "version.h"
#include "log.h"
#include "rds-utils.h"

#define PEAK_HOLD_SAMPLES 4
#define UPDATE_TIMEOUT 2000

static gboolean ui_update_af_check(GtkTreeModel*, GtkTreePath*, GtkTreeIter*, gpointer);
static gboolean update_service(gpointer);
static void service_update_rotator();

void
ui_update_freq()
{
    static gint last_freq = G_MININT;
    gchar buffer[8];

    if(conf.scan_mark_tuned)
        scan_force_redraw();

    if(tuner_get_freq() > 0)
    {
        if(last_freq != tuner_get_freq())
        {
            g_snprintf(buffer, sizeof(buffer), "%.3f", tuner_get_freq()/1000.0);
            gtk_label_set_text(GTK_LABEL(ui.l_freq), buffer);
        }

        if(conf.signal_mode == GRAPH_RESET)
            signal_clear();

        if(conf.grab_focus)
            ui_activate();

        tuner_clear_signal();
        stationlist_freq(tuner_get_freq());
        log_cleanup();
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(ui.l_freq), " ");
        signal_clear();
    }

    last_freq = tuner_get_freq();
}

void
ui_update_mode()
{
    static gint mode = -1;
    if(mode != tuner.mode)
    {
        mode = tuner.mode;
        gtk_label_set_text(GTK_LABEL(ui.l_band), (mode == MODE_FM ? "FM" : "AM"));
        gtk_widget_set_sensitive(ui.c_deemph, (tuner.mode == MODE_FM));
        g_signal_handlers_block_by_func(G_OBJECT(ui.c_bw), GINT_TO_POINTER(tuner_set_bandwidth), NULL);
        ui_bandwidth_fill(ui.c_bw, TRUE);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.c_bw), GINT_TO_POINTER(tuner_set_bandwidth), NULL);
        tuner_clear_signal();
        tuner_clear_rds();
    }
}

void
ui_update_stereo_flag()
{
    static gint flag = -1;
    static gint forced_mono = -1;

    if(forced_mono != tuner.forced_mono ||
       flag != tuner.stereo)
    {
        forced_mono = tuner.forced_mono;
        flag = tuner.stereo;

        if(flag)
        {
            gtk_label_set_text(GTK_LABEL(ui.l_st), (!forced_mono) ? "ST" : "MO");
            gtk_widget_modify_fg(GTK_WIDGET(ui.l_st), GTK_STATE_NORMAL, &ui.colors.stereo);
        }
        else
        {
            if(!conf.accessibility)
            {
                gtk_label_set_text(GTK_LABEL(ui.l_st), (!forced_mono) ? "ST" : "MO");
                gtk_widget_modify_fg(GTK_WIDGET(ui.l_st), GTK_STATE_NORMAL, &ui.colors.insensitive);
            }
            else
            {
                gtk_label_set_markup(GTK_LABEL(ui.l_st), "  ");
            }
        }
    }
}

void
ui_update_rds_flag()
{
    static gint last_flag = -1;

    if(last_flag != tuner.rds)
    {
        last_flag = tuner.rds;

        if(last_flag == 0)
        {
            if(!conf.accessibility)
            {
                gtk_label_set_text(GTK_LABEL(ui.l_rds), "RDS");
                gtk_widget_modify_fg(GTK_WIDGET(ui.l_rds), GTK_STATE_NORMAL, &ui.colors.insensitive);
            }
            else
            {
                gtk_label_set_markup(GTK_LABEL(ui.l_rds), "   ");
            }
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(ui.l_rds), "RDS");
            gtk_widget_modify_fg(GTK_WIDGET(ui.l_rds), GTK_STATE_NORMAL, &conf.color_rds);
        }
    }
}

void
ui_update_signal()
{
    static gfloat samples[PEAK_HOLD_SAMPLES] = {-1};
    static gint pos = 0;
    static gint last_signal_max = G_MININT;
    static gint last_signal_curr = G_MININT;
    gfloat peak = -1;
    gint signal_max;
    gint signal_curr;
    gchar *str;
    gint i;

    if(isnan(tuner.signal))
    {
        gtk_label_set_text(GTK_LABEL(ui.l_sig), " ");
        signal_clear();

        if(conf.signal_display == SIGNAL_GRAPH)
            gtk_widget_queue_draw(ui.graph);
        else if(conf.signal_display == SIGNAL_BAR)
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_signal), 0.0);

        last_signal_curr = G_MININT;
        return;
    }

    signal_push(tuner.signal, tuner.stereo, tuner.rds, tuner_get_freq());
    scan_update_value(tuner_get_freq(), tuner.signal);
    pattern_push(tuner.signal);
    stationlist_rcvlevel(lround(tuner.signal));

    /* Add new sample to the buffer */
    samples[pos] = tuner.signal;

    for(i=0; i<PEAK_HOLD_SAMPLES; i++)
       if(samples[i] > peak)
           peak = samples[i];

    signal_max = lround(signal_level(tuner.signal_max));
    signal_curr = lround(signal_level(peak));

    if(signal_max < signal_curr)
    {
        /* Clear all buffered samples */
        for(i=0; i<PEAK_HOLD_SAMPLES; i++)
            samples[i] = -1;
        pos = 0;
        samples[pos] = tuner.signal;
        signal_curr = lround(signal_level(tuner.signal));
    }

    if(pos == 0 &&
       (last_signal_max != signal_max || last_signal_curr != signal_curr))
     {
        last_signal_max = signal_max;
        last_signal_curr = signal_curr;
        if(tuner.mode == MODE_FM)
        {
            switch(conf.signal_unit)
            {
            case UNIT_DBM:
                str = g_markup_printf_escaped("<span color=\"#777777\">%4d%s</span>%4ddBm",
                                              signal_max,
                                              ((fabs(conf.signal_offset) < 0.01) ? "↑" : "↥"),
                                              signal_curr);
                break;

            case UNIT_DBUV:
                str = g_markup_printf_escaped("<span color=\"#777777\">%3d%s</span>%3d dBµV",
                                              signal_max,
                                              ((fabs(conf.signal_offset) < 0.01) ? "↑" : "↥"),
                                              signal_curr);
                break;

            case UNIT_DBF:
            default:
                str = g_markup_printf_escaped("<span color=\"#777777\">%3d%s</span>%3d dBf",
                                              signal_max,
                                              ((fabs(conf.signal_offset) < 0.1) ? "↑" : "↥"),
                                              signal_curr);
                break;
            }
        }
        else
        {
            str = g_markup_printf_escaped("<span color=\"#777777\">%3d%s</span> %3d",
                                          signal_max,
                                          ((fabs(conf.signal_offset) < 0.1) ? "↑" : "↥"),
                                          signal_curr);
        }
        gtk_label_set_markup(GTK_LABEL(ui.l_sig), str);
        g_free(str);
    }
    pos = (pos+1)%PEAK_HOLD_SAMPLES;


    if(conf.signal_display == SIGNAL_GRAPH)
        gtk_widget_queue_draw(ui.graph);
    else if(conf.signal_display == SIGNAL_BAR)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_signal),
                                      ((tuner.signal >= 80)? 1.0 : tuner.signal/80.0));
}

void
ui_update_cci()
{
    static gint samples[PEAK_HOLD_SAMPLES] = {-1};
    static gint last_peak = G_MININT;
    static gint pos = 0;
    gint peak = -1;
    gint last_pos = pos;
    gchar buff[10];
    gint i;

    /* Add new sample to the buffer */
    pos = (pos+1)%PEAK_HOLD_SAMPLES;
    samples[pos] = tuner.cci;

    /* Change indicator value */
    if(samples[last_pos] != samples[pos])
    {
        if(samples[pos] == -1)
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_cci), 0.0);
        else
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_cci), samples[pos]/100.0);
    }

    /* No data? Clear all buffered samples */
    if(samples[pos] == -1)
        for(i=0; i<PEAK_HOLD_SAMPLES; i++)
            samples[i] = -1;

    /* Update peak value every PEAK_HOLD_SAMPLES */
    if(last_pos == 0 || samples[pos] == -1)
    {
        for(i=0; i<PEAK_HOLD_SAMPLES; i++)
           if(samples[i] > peak)
               peak = samples[i];

        if(last_peak != peak)
        {
            last_peak = peak;
            if(samples[pos] < 0)
                g_snprintf(buff, sizeof(buff), "CCI: ?");
            else
                g_snprintf(buff, sizeof(buff), "CCI: %d%%", peak);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui.p_cci), buff);
        }
    }
}

void
ui_update_aci()
{
    static gint samples[PEAK_HOLD_SAMPLES] = {-1};
    static gint last_peak = G_MININT;
    static gint pos = 0;
    gint peak = -1;
    gint last_pos = pos;
    gchar buff[10];
    gint i;

    /* Add new sample to the buffer */
    pos = (pos+1)%PEAK_HOLD_SAMPLES;
    samples[pos] = tuner.aci;

    /* Change indicator value */
    if(samples[last_pos] != samples[pos])
    {
        if(samples[pos] == -1)
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_aci), 0.0);
        else
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.p_aci), samples[pos]/100.0);
    }

    /* No data? Clear all buffered samples */
    if(samples[pos] == -1)
        for(i=0; i<PEAK_HOLD_SAMPLES; i++)
            samples[i] = -1;

    /* Update peak value every PEAK_HOLD_SAMPLES */
    if(last_pos == 0 || samples[pos] == -1)
    {
        for(i=0; i<PEAK_HOLD_SAMPLES; i++)
           if(samples[i] > peak)
               peak = samples[i];

        if(last_peak != peak)
        {
            last_peak = peak;
            if(samples[pos] < 0)
                g_snprintf(buff, sizeof(buff), "ACI: ?");
            else
                g_snprintf(buff, sizeof(buff), "ACI: %d%%", peak);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ui.p_aci), buff);
        }
    }
}

void
ui_update_pi()
{
    static gint last_pi = G_MININT;
    static gint last_err_level = G_MININT;
    gchar buffer[50];

    if(last_pi == tuner.rds_pi &&
       last_err_level == tuner.rds_pi_err_level)
        return;
    last_pi = tuner.rds_pi;
    last_err_level = tuner.rds_pi_err_level;

    if(tuner.rds_pi >= 0)
    {
        if(tuner.rds_pi_err_level >= 3)
            g_snprintf(buffer, sizeof(buffer),
                       "%04X<span color=\"#777777\">\342\201\207</span>",
                       tuner.rds_pi);
        else if(tuner.rds_pi_err_level == 2)
            g_snprintf(buffer, sizeof(buffer),
                       "%04X<span color=\"#777777\">?</span>",
                       tuner.rds_pi);
        else if(tuner.rds_pi_err_level == 1)
            g_snprintf(buffer, sizeof(buffer),
                       "%04X<span color=\"#AAAAAA\">?</span>",
                       tuner.rds_pi);
        else
            g_snprintf(buffer, sizeof(buffer),
                       "%04X",
                       tuner.rds_pi);

        gtk_label_set_markup(GTK_LABEL(ui.l_pi), buffer);
        stationlist_pi(tuner.rds_pi);
        log_pi(tuner.rds_pi, tuner.rds_pi_err_level);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(ui.l_pi), " ");
    }
}

void
ui_update_tp()
{
    static gint last_tp = G_MININT;

    if(last_tp == tuner.rds_tp)
        return;
    last_tp = tuner.rds_tp;

    switch(tuner.rds_tp)
    {
    case 0:
        if(!conf.accessibility)
        {
            gtk_label_set_text(GTK_LABEL(ui.l_tp), "TP");
            gtk_widget_modify_fg(GTK_WIDGET(ui.l_tp), GTK_STATE_NORMAL, &ui.colors.insensitive);
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(ui.l_tp), "  ");
        }
        break;
    case 1:
        gtk_label_set_text(GTK_LABEL(ui.l_tp), "TP");
        gtk_widget_modify_fg(GTK_WIDGET(ui.l_tp), GTK_STATE_NORMAL, &ui.colors.foreground);
        break;
    default:
        gtk_label_set_text(GTK_LABEL(ui.l_tp), "  ");
        break;
    }
}

void
ui_update_ta()
{
    static gint last_ta = G_MININT;

    if(last_ta == tuner.rds_ta)
        return;
    last_ta = tuner.rds_ta;

    switch(tuner.rds_ta)
    {
    case 0:
        if(!conf.accessibility)
        {
            gtk_label_set_text(GTK_LABEL(ui.l_ta), "TA");
            gtk_widget_modify_fg(GTK_WIDGET(ui.l_ta), GTK_STATE_NORMAL, &ui.colors.insensitive);
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(ui.l_ta), "  ");
        }
        break;
    case 1:
        gtk_label_set_text(GTK_LABEL(ui.l_ta), "TA");
        gtk_widget_modify_fg(GTK_WIDGET(ui.l_ta), GTK_STATE_NORMAL, &ui.colors.foreground);
        break;
    default:
        gtk_label_set_text(GTK_LABEL(ui.l_ta), "  ");
        break;
    }
}

void
ui_update_ms()
{
    static gint last_ms = G_MININT;

    if(last_ms == tuner.rds_ms)
        return;
    last_ms = tuner.rds_ms;

    switch(tuner.rds_ms)
    {
    case 0:
        if(!conf.accessibility)
            gtk_label_set_markup(GTK_LABEL(ui.l_ms), "<span color=\"" UI_COLOR_INSENSITIVE "\">M</span>S");
        else
            gtk_label_set_text(GTK_LABEL(ui.l_ms), "Speech");
        break;
    case 1:
        if(!conf.accessibility)
            gtk_label_set_markup(GTK_LABEL(ui.l_ms), "M<span color=\"" UI_COLOR_INSENSITIVE "\">S</span>");
        else
            gtk_label_set_text(GTK_LABEL(ui.l_ms), "Music ");
        break;
    default:
        if(!conf.accessibility)
            gtk_label_set_text(GTK_LABEL(ui.l_ms), "  ");
        else
            gtk_label_set_text(GTK_LABEL(ui.l_ms), "      ");
        break;
    }
}

void
ui_update_pty()
{
    static gint last_pty = G_MININT;
    const gchar *pty_text;

    if(last_pty == tuner.rds_pty)
        return;

    last_pty = tuner.rds_pty;

    if(last_pty >= 0 && last_pty < 32)
    {
        pty_text = rds_utils_pty_to_string(conf.rds_pty_set, last_pty);
        gtk_label_set_text(GTK_LABEL(ui.l_pty), pty_text);
        stationlist_pty(last_pty);
        log_pty(pty_text);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(ui.l_pty), " ");
    }
}

void
ui_update_ecc()
{
    static const gchar* const ecc_list[][16] = {
        {"??", "DE", "DZ", "AD", "IL", "IT", "BE", "RU", "PS", "AL", "AT", "HU", "MT", "DE", "??", "EG" },
        {"??", "GR", "CY", "SM", "CH", "JO", "FI", "LU", "BG", "DK", "GI", "IQ", "GB", "LY", "RO", "FR" },
        {"??", "MA", "CZ", "PL", "VA", "SK", "SY", "TN", "??", "LI", "IS", "MC", "LT", "YU", "ES", "NO" },
        {"??", "??", "IE", "TR", "MK", "??", "??", "??", "NL", "LV", "LB", "??", "HR", "??", "SE", "BY" },
        {"??", "MD", "EE", "??", "??", "??", "UA", "??", "PT", "SI", "??", "??", "??", "??", "??", "BA" },
    };

    /* ECC is currently displayed only in StationList and logs */
    if(tuner.rds_pi >= 0 &&
       (tuner.rds_ecc >= 0xE0 && tuner.rds_ecc <= 0xE4))
    {
        stationlist_ecc(tuner.rds_ecc);
        log_ecc(ecc_list[tuner.rds_ecc & 7][(guint16)tuner.rds_pi >> 12], tuner.rds_ecc);
    }
}

void
ui_update_ps(gboolean new_data)
{
    static gint last_ps_avail = G_MININT;
    guchar c[8];
    gint i;
    gchar *m;

    if(last_ps_avail == (gint)tuner.rds_ps_avail &&
       tuner.rds_ps_avail != TRUE)
        return;
    last_ps_avail = tuner.rds_ps_avail;

    if(!tuner.rds_ps_avail)
    {
        gtk_label_set_text(GTK_LABEL(ui.l_ps), " ");
        return;
    }

    for(i=0; i<8; i++)
        c[i] = (tuner.rds_ps_err[i] ? 110+(tuner.rds_ps_err[i] * 12) : 0);

    m = g_markup_printf_escaped("<span color=\"" UI_COLOR_INSENSITIVE "\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"#%02X%02X%02X\">%c</span>"
                                "<span color=\"" UI_COLOR_INSENSITIVE "\">%c</span>",
                                conf.rds_ps_progressive?'(':'[',
                                c[0], c[0], c[0], tuner.rds_ps[0],
                                c[1], c[1], c[1], tuner.rds_ps[1],
                                c[2], c[2], c[2], tuner.rds_ps[2],
                                c[3], c[3], c[3], tuner.rds_ps[3],
                                c[4], c[4], c[4], tuner.rds_ps[4],
                                c[5], c[5], c[5], tuner.rds_ps[5],
                                c[6], c[6], c[6], tuner.rds_ps[6],
                                c[7], c[7], c[7], tuner.rds_ps[7],
                                conf.rds_ps_progressive?')':']');
    gtk_label_set_markup(GTK_LABEL(ui.l_ps), m);
    g_free(m);

    if(new_data)
    {
        stationlist_ps(tuner.rds_ps);
        log_ps(tuner.rds_ps, tuner.rds_ps_err);
    }
}

void
ui_update_rt(gboolean flag)
{
    static gint last_rt_avail[2] = {G_MININT, G_MININT};
    gchar *m;

    if(!tuner.rds_rt_avail[flag] && !last_rt_avail[flag])
        return;

    last_rt_avail[flag] = tuner.rds_rt_avail[flag];

    if(!tuner.rds_rt_avail[flag])
    {
        gtk_label_set_text(GTK_LABEL(ui.l_rt[flag]), " ");
        return;
    }

    m = g_markup_printf_escaped("<span color=\"" UI_COLOR_INSENSITIVE "\">[</span>"
                                "%s"
                                "<span color=\"" UI_COLOR_INSENSITIVE "\">]</span>",
                                tuner.rds_rt[flag]);

    gtk_label_set_markup(GTK_LABEL(ui.l_rt[flag]), m);
    g_free(m);
    stationlist_rt(flag, tuner.rds_rt[flag]);
    log_rt(flag, tuner.rds_rt[flag]);
}

void
ui_update_af(gint af)
{
    GtkListStore *model = ui.af_model;
    GtkTreeIter iter;

    // if new frequency is found on the AF list, ui_update_af_check() will set it to 0
    gtk_tree_model_foreach(GTK_TREE_MODEL(model), (GtkTreeModelForeachFunc)ui_update_af_check, &af);
    if(af)
    {
        if(!conf.horizontal_af)
        {
            GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(ui.af_treeview_scroll));
            ui.autoscroll = (gtk_adjustment_get_value(adj) == gtk_adjustment_get_lower(adj));
        }

        gtk_list_store_append(model, &iter);
        gchar *af_new_freq = g_strdup_printf("%.1f", ((87500+af*100)/1000.0));
        gtk_list_store_set(model, &iter, 0, af, 1, af_new_freq, -1);
        stationlist_af(af);
        log_af(af_new_freq);
        g_free(af_new_freq);
    }
}

static gboolean
ui_update_af_check(GtkTreeModel *model,
                   GtkTreePath  *path,
                   GtkTreeIter  *iter,
                   gpointer      user_data)
{
    gint *new_freq = (gint*)user_data;
    gint freq;
    
    gtk_tree_model_get(model, iter, 0, &freq, -1);
    if(freq == *new_freq)
    {
        *new_freq = 0;
        return TRUE; // frequency is already on the list, stop searching.
    }
    return FALSE;
}

void
ui_update_filter()
{
    stationlist_bw(tuner_filter_bw(tuner.filter));
}

void
ui_update_rotator()
{
    const GdkColor *color = (tuner.rotator_waiting ? &ui.colors.action2 : &ui.colors.action);
    gtk_widget_modify_bg(ui.b_cw,  GTK_STATE_ACTIVE,   (tuner.rotator == 1 ? color : NULL));
    gtk_widget_modify_bg(ui.b_cw,  GTK_STATE_PRELIGHT, (tuner.rotator == 1 ? color : NULL));
    gtk_widget_modify_bg(ui.b_ccw, GTK_STATE_ACTIVE,   (tuner.rotator == 2 ? color : NULL));
    gtk_widget_modify_bg(ui.b_ccw, GTK_STATE_PRELIGHT, (tuner.rotator == 2 ? color : NULL));
}

void
ui_update_scan(tuner_scan_t* scan)
{
    scan_update(scan);
}

void
ui_update_disconnected()
{
    gtk_widget_set_sensitive(ui.b_connect, TRUE);
    connect_button(FALSE);
    gtk_window_set_title(GTK_WINDOW(ui.window), APP_NAME);
}

void
ui_update_pilot(gint pilot)
{
    if(!tuner.last_set_pilot)
        return;
    tuner.last_set_pilot = 0;

    if(pilot)
    {
        ui_dialog(ui.window,
                  GTK_MESSAGE_INFO,
                  "Stereo pilot subcarrier",
                  "Estimated injection level:\n<b>%.1f kHz (%0.1f%%)</b>",
                  pilot/10.0, pilot/10.0/75.0*100);
    }
    else
    {
        ui_dialog(ui.window,
                  GTK_MESSAGE_WARNING,
                  "Stereo pilot subcarrier",
                  "The stereo subcarrier is not present or the injection level is too low.");
    }
}

void
ui_action()
{
    switch(conf.event_action)
    {
        case ACTION_NONE:
            break;
        case ACTION_ACTIVATE:
            ui_activate();
            break;
        case ACTION_SCREENSHOT:
            ui_screenshot();
            break;
    }
}

void
ui_unauthorized()
{
    connection_socket_auth_fail();
}

void
ui_clear_af()
{
    gtk_list_store_clear(ui.af_model);
}

void
ui_update_service()
{
    tuner.last_set_filter = 0;
    tuner.last_set_deemph = 0;
    tuner.last_set_agc = 0;
    tuner.last_set_ant = 0;
    tuner.last_set_volume = 0;
    tuner.last_set_squelch = 0;
    tuner.last_set_gain = 0;
    tuner.last_set_daa = 0;
    tuner.last_set_rotator = 0;
    tuner.last_set_pilot = 0;
    g_timeout_add(100, (GSourceFunc)update_service, NULL);
}

static gboolean
update_service(gpointer user_data)
{
    gint64 current_time = g_get_real_time() / 1000;
    gint value;

    if(!tuner.thread)
        return TRUE;

    /* Reset RDS data after timeout */
    if(conf.rds_reset && tuner.rds_reset_timer &&
       (g_get_real_time() - tuner.rds_reset_timer) > (conf.rds_reset_timeout*1000000L))
    {
        tuner_clear_rds();
    }

    /* AGC */
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(ui.c_agc)) != tuner.agc &&
       (current_time - tuner.last_set_agc > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.c_agc), GINT_TO_POINTER(tuner_set_agc), NULL);
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui.c_agc), tuner.agc);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.c_agc), GINT_TO_POINTER(tuner_set_agc), NULL);
    }

    /* Deemphasis */
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(ui.c_deemph)) != tuner.deemphasis &&
       (current_time - tuner.last_set_deemph > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.c_deemph), GINT_TO_POINTER(tuner_set_deemphasis), NULL);
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui.c_deemph), tuner.deemphasis);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.c_deemph), GINT_TO_POINTER(tuner_set_deemphasis), NULL);
    }

    /* Antenna */
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(ui.c_ant)) != tuner.antenna &&
       (current_time - tuner.last_set_ant > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.c_ant), GINT_TO_POINTER(tuner_set_antenna), NULL);
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui.c_ant), tuner.antenna);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.c_ant), GINT_TO_POINTER(tuner_set_antenna), NULL);
    }

    /* Filters */
    value = tuner_filter_index(tuner.filter);
    if(gtk_combo_box_get_active(GTK_COMBO_BOX(ui.c_bw)) != value &&
       (current_time - tuner.last_set_filter > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.c_bw), GINT_TO_POINTER(tuner_set_bandwidth), NULL);
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui.c_bw), value);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.c_bw), GINT_TO_POINTER(tuner_set_bandwidth), NULL);
    }

    /* Volume */
    if(lround(gtk_scale_button_get_value(GTK_SCALE_BUTTON(ui.volume))) != tuner.volume &&
       (current_time - tuner.last_set_volume > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.volume), GINT_TO_POINTER(tuner_set_volume), NULL);
        gtk_scale_button_set_value(GTK_SCALE_BUTTON(ui.volume), tuner.volume);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.volume), GINT_TO_POINTER(tuner_set_volume), NULL);
    }

    /* Squelch */
    if(lround(gtk_scale_button_get_value(GTK_SCALE_BUTTON(ui.squelch))) != tuner.squelch &&
       (current_time - tuner.last_set_squelch > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.squelch), GINT_TO_POINTER(tuner_set_squelch), NULL);
        gtk_scale_button_set_value(GTK_SCALE_BUTTON(ui.squelch), tuner.squelch);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.squelch), GINT_TO_POINTER(tuner_set_squelch), NULL);
    }

    /* Gain */
    if(((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.x_rf)) != tuner.rfgain) ||
       gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.x_if)) != tuner.ifgain) &&
       (current_time - tuner.last_set_gain > UPDATE_TIMEOUT))
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.x_rf), tuner.rfgain);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.x_if), tuner.ifgain);
    }

    /* DAA */
    if(lround(gtk_adjustment_get_value(GTK_ADJUSTMENT(ui.adj_align))) != tuner.daa &&
       (current_time - tuner.last_set_daa > UPDATE_TIMEOUT))
    {
        g_signal_handlers_block_by_func(G_OBJECT(ui.adj_align), GINT_TO_POINTER(tuner_set_alignment), NULL);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(ui.adj_align), tuner.daa);
        g_signal_handlers_unblock_by_func(G_OBJECT(ui.adj_align), GINT_TO_POINTER(tuner_set_alignment), NULL);
    }

    /* Rotator */
    if(current_time - tuner.last_set_rotator > UPDATE_TIMEOUT)
        service_update_rotator();

    return TRUE;
}

static void
service_update_rotator()
{
    if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.b_cw)) &&
       !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.b_ccw)) &&
       tuner.rotator == 0)
        return;

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.b_cw)) &&
       tuner.rotator == 1)
        return;

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui.b_ccw)) &&
       tuner.rotator == 2)
        return;

    g_signal_handlers_block_by_func(G_OBJECT(ui.b_cw), GINT_TO_POINTER(tuner_set_rotator), GINT_TO_POINTER(1));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.b_cw),  (tuner.rotator == 1));
    g_signal_handlers_unblock_by_func(G_OBJECT(ui.b_cw), GINT_TO_POINTER(tuner_set_rotator), GINT_TO_POINTER(1));

    g_signal_handlers_block_by_func(G_OBJECT(ui.b_ccw), GINT_TO_POINTER(tuner_set_rotator), GINT_TO_POINTER(2));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.b_ccw), (tuner.rotator == 2));
    g_signal_handlers_unblock_by_func(G_OBJECT(ui.b_ccw), GINT_TO_POINTER(tuner_set_rotator), GINT_TO_POINTER(2));
}
