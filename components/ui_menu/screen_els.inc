// screen_els.inc – Ekran ELS (gwintowanie)
// Dołączany do ui_menu.c przez #include

static struct {
    uint8_t  preset_idx;
    float    z_start_mm, z_end_mm;
    uint8_t  passes, param_sel;
    bool     confirm_mode;
} els_ui = { .preset_idx=5, .z_start_mm=0.0f, .z_end_mm=-20.0f,
             .passes=1, .param_sel=0, .confirm_mode=false };

static void draw_els(void)
{
    els_status_t st; els_get_status(&st);
    uint16_t hdr = els_is_running() ? RGB565(160,0,80) : RGB565(80,0,120);
    draw_header("ELS GWINTOWANIE", hdr);
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H+2, COLOR_BLACK);
    char buf[24]; int16_t y=CONTENT_Y+2;

    if (els_is_running()) {
        const char *ss[]={"IDLE","CZEKA","BIEG","POWRT","BLAD"};
        uint16_t sc[]={COL_LABEL,COL_WARN,COL_OK,COL_VAL,COLOR_RED};
        snprintf(buf,sizeof(buf),"Stan: %s", ss[st.state]);
        display_draw_string(4,y,buf,sc[st.state],COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"Przejscie: %d/%d",st.pass_current,st.pass_total);
        display_draw_string(4,y,buf,COL_LABEL,COLOR_BLACK,1); y+=ROW_H;
        int pi=(int)st.z_pos_mm, pd=(int)(fabsf(st.z_pos_mm-(float)pi)*100.0f);
        snprintf(buf,sizeof(buf),"Z: %4d.%02d mm",pi,pd);
        display_draw_string(4,y,buf,COL_VAL,COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"RPM: %4d  %s",st.spindle_rpm,st.sync_ok?"SYNC OK":"SYNC...");
        display_draw_string(4,y,buf,st.sync_ok?COL_OK:COL_WARN,COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"Kroki: %ld",(long)st.steps_sent);
        display_draw_string(4,y,buf,COLOR_LIGHT_GREY,COLOR_BLACK,1); y+=ROW_H;
        display_draw_hline(4,y,SCR_W-8,COLOR_LIGHT_GREY); y+=4;
        display_draw_string(4,y,"BTN1=STOP ELS",COL_WARN,COLOR_BLACK,1);

    } else if (els_ui.confirm_mode) {
        display_fill_rect(0,y,SCR_W,12,RGB565(80,0,0));
        display_draw_string(4,y+2,"POTWIERDZ START",COLOR_WHITE,RGB565(80,0,0),1); y+=ROW_H;
        const thread_preset_t *p=&ELS_THREAD_PRESETS[els_ui.preset_idx];
        float pitch=(p->type==THREAD_IMPERIAL)?els_tpi_to_mm(p->pitch):p->pitch;
        snprintf(buf,sizeof(buf),"Gwint: %s",p->label);
        display_draw_string(4,y,buf,COLOR_YELLOW,COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"Skok: %.3f mm",pitch);
        display_draw_string(4,y,buf,COL_VAL,COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"Z: %.1f->%.1f mm",els_ui.z_start_mm,els_ui.z_end_mm);
        display_draw_string(4,y,buf,COL_VAL,COLOR_BLACK,1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"Przejsc: %d",els_ui.passes);
        display_draw_string(4,y,buf,COL_VAL,COLOR_BLACK,1); y+=ROW_H;
        display_fill_rect(0,y,SCR_W,13,RGB565(0,60,0));
        display_draw_string(4,y+2,"SW=START  BTN2=ANULUJ",COLOR_WHITE,RGB565(0,60,0),1);

    } else {
        const thread_preset_t *p=&ELS_THREAD_PRESETS[els_ui.preset_idx];
        float pitch=(p->type==THREAD_IMPERIAL)?els_tpi_to_mm(p->pitch):p->pitch;
        draw_row(y,"Gwint:",p->label,COLOR_YELLOW,els_ui.param_sel==0); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%.3fmm",pitch);
        draw_row(y,"  skok:",buf,COL_LABEL,false); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%+.1f",els_ui.z_start_mm);
        draw_row(y,"Z start:",buf,COL_VAL,els_ui.param_sel==1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%+.1f",els_ui.z_end_mm);
        draw_row(y,"Z koniec:",buf,COL_VAL,els_ui.param_sel==2); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%d",els_ui.passes);
        draw_row(y,"Przejscia:",buf,COL_VAL,els_ui.param_sel==3); y+=ROW_H;
        display_draw_hline(4,y,SCR_W-8,COLOR_LIGHT_GREY); y+=4;
        display_draw_string(4,y,"SW=param SW>>>=START",COLOR_LIGHT_GREY,COLOR_BLACK,1);
    }
    draw_footer();
    display_flush();
}

static void handle_els(encoder_event_t evt)
{
    if (evt==ENCODER_EVT_BTN1_LONG) {
        els_stop(); spindle_emergency_stop(); stepper_stop();
        els_ui.confirm_mode=false;
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000); return;
    }
    if (els_is_running()) {
        if (evt==ENCODER_EVT_BTN1_PRESS) { els_stop(); ui_menu_notify("ELS STOP",COL_WARN,1000); }
        return;
    }
    if (els_ui.confirm_mode) {
        if (evt==ENCODER_EVT_SW_PRESS) {
            els_ui.confirm_mode=false;
            const thread_preset_t *p=&ELS_THREAD_PRESETS[els_ui.preset_idx];
            float pitch=(p->type==THREAD_IMPERIAL)?els_tpi_to_mm(p->pitch):p->pitch;
            if (!spindle_is_at_speed()) { ui_menu_notify("Uruchom wrzeciono!",COL_WARN,2000); return; }
            els_config_t cfg={.pitch_mm=pitch,
                .feed_dir=(els_ui.z_end_mm<els_ui.z_start_mm)?STEPPER_DIR_CCW:STEPPER_DIR_CW,
                .z_start_mm=els_ui.z_start_mm,.z_end_mm=els_ui.z_end_mm,.passes=els_ui.passes};
                if (stepper_get_state() != STEPPER_STATE_IDLE) {
                ui_menu_notify("Os Z jeszcze biegnie!", COL_WARN, 2000);
                return;
                }
            if (els_start(&cfg)) ui_menu_notify("ELS START!",COL_OK,800);
            else ui_menu_notify("Blad startu ELS",COLOR_RED,2000);
        } else if (evt==ENCODER_EVT_BTN2_PRESS||evt==ENCODER_EVT_SW_LONG) {
            els_ui.confirm_mode=false; ui_menu_notify("Anulowano",COL_LABEL,800);
        }
        return;
    }
    switch (evt) {
        case ENCODER_EVT_CW:
            switch(els_ui.param_sel){
                case 0: if(els_ui.preset_idx<ELS_THREAD_PRESETS_COUNT-1) els_ui.preset_idx++; break;
                case 1: els_ui.z_start_mm+=0.5f; break;
                case 2: els_ui.z_end_mm+=0.5f; break;
                case 3: if(els_ui.passes<20) els_ui.passes++; break;
            } break;
        case ENCODER_EVT_CCW:
            switch(els_ui.param_sel){
                case 0: if(els_ui.preset_idx>0) els_ui.preset_idx--; break;
                case 1: els_ui.z_start_mm-=0.5f; break;
                case 2: els_ui.z_end_mm-=0.5f; break;
                case 3: if(els_ui.passes>1) els_ui.passes--; break;
            } break;
        case ENCODER_EVT_SW_PRESS:
            els_ui.param_sel=(els_ui.param_sel+1)%4; break;
        case ENCODER_EVT_SW_LONG:
            if (!spindle_is_at_speed()) ui_menu_notify("Uruchom wrzeciono!",COL_WARN,2000);
            else {
                els_ui.confirm_mode=true;
             }
             break;
        case ENCODER_EVT_BTN3_PRESS:
            els_ui.z_start_mm=stepper_get_position_mm();
            ui_menu_notify("Z_START = tu",COL_OK,800); break;
        case ENCODER_EVT_BTN3_LONG:
            els_ui.z_end_mm=stepper_get_position_mm();
            ui_menu_notify("Z_END = tu",COL_OK,800); break;
        case ENCODER_EVT_BTN2_LONG: ui_menu_goto(SCREEN_MENU); break;
        default: break;
    }
}
