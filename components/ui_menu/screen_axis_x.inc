// screen_axis_x.inc – Ekran osi X (dosuw)
// Dołączany do ui_menu.c przez #include

typedef enum { AXIS_X_MODE_JOG=0, AXIS_X_MODE_AUTO, AXIS_X_MODE_CYCLE } axis_x_mode_t;

static struct {
    axis_x_mode_t mode;
    uint8_t  jog_step_idx;
    float    auto_target_mm, auto_speed;
    float    cycle_depth_mm, cycle_z_start, cycle_z_end, cycle_z_speed;
    uint8_t  cycle_passes, param_sel;
} x_ui = { .mode=AXIS_X_MODE_JOG, .jog_step_idx=1,
            .auto_target_mm=0.0f, .auto_speed=30.0f,
            .cycle_depth_mm=0.1f, .cycle_z_start=0.0f,
            .cycle_z_end=-20.0f, .cycle_z_speed=80.0f,
            .cycle_passes=3, .param_sel=0 };

static volatile bool cycle_abort = false;
static const uint16_t X_JOG_STEPS[] = {1,4,16,64,160};

static const char    *X_JOG_LBL[]       = {"1","4","16","64","160"};
#define X_JOG_COUNT 5
static const char *x_mode_name[] = {"JOG X","AUTO X","CYKL ZX"};

static void cycle_zx_task(void *arg)
{
    cycle_abort = false;                        // ← reset flagi przy starcie
    for (uint8_t p = 1; p <= x_ui.cycle_passes; p++) {
        if (cycle_abort) break;                 // ← sprawdź przed każdym ruchem

        float xnow = axis_get_position_mm(g_axis_x);
        axis_move_to_mm(g_axis_x, xnow - x_ui.cycle_depth_mm, x_ui.auto_speed);
        while (axis_get_state(g_axis_x) == AXIS_STATE_RUN) {
            if (cycle_abort) { axis_stop(g_axis_x); break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (cycle_abort || axis_get_state(g_axis_x) == AXIS_STATE_ERROR) break;

        float spmm = axis_get_steps_per_mm(g_axis_z);
        axis_move_t zmv = { .steps = (int32_t)((x_ui.cycle_z_end - x_ui.cycle_z_start) * spmm),
                            .speed_mm_min = x_ui.cycle_z_speed };
        stepper_move(&zmv);
        while (stepper_get_state() == STEPPER_STATE_RUN) {
            if (cycle_abort) { stepper_stop(); break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (cycle_abort || stepper_get_state() == STEPPER_STATE_ERROR) break;

        axis_move_t zr = { .steps = -(int32_t)((x_ui.cycle_z_end - x_ui.cycle_z_start) * spmm),
                           .speed_mm_min = 200.0f };
        stepper_move(&zr);
        while (stepper_get_state() == STEPPER_STATE_RUN) {
            if (cycle_abort) { stepper_stop(); break; }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (cycle_abort) break;

        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ui_menu_notify(cycle_abort ? "Cykl ZX przerwany" : "Cykl ZX zakonczony",
                   cycle_abort ? COL_WARN : COL_OK, 2000);
    vTaskDelete(NULL);
}

static void draw_axis_x(void)
{
    draw_header("OS X – DOSUW", rgb565(0,100,80));
    display_fill_rect(0, HEADER_H, SCR_W, CONTENT_H+2, COLOR_BLACK);
    char buf[24]; int16_t y=CONTENT_Y+2;

    // Pozycja X i Z
    if (g_axis_x) {
        axis_status_t xs; axis_get_status(g_axis_x, &xs);
        int xi=(int)xs.position_mm, xd=(int)(fabsf(xs.position_mm-(float)xi)*100.0f);
        display_draw_string(4,y,"X:",COL_LABEL,COLOR_BLACK,1);
        snprintf(buf,sizeof(buf),"%4d.%02d mm",xi,xd);
        display_draw_string(UI_PAD_X + 14, y, buf, COL_VAL, COLOR_BLACK, 1);
        const char *xs_s[]={"IDLE","RUN","ACC","DEC","ERR"};
        display_draw_string(COL_RIGHT_1, y, xs_s[xs.state], (xs.state==AXIS_STATE_IDLE)?COL_LABEL:COL_WARN, COLOR_BLACK, 1);
        y+=ROW_H;
    }
    float zmm=stepper_get_position_mm();
    int zi=(int)zmm, zd=(int)(fabsf(zmm-(float)zi)*100.0f);
    snprintf(buf,sizeof(buf),"Z: %4d.%02d mm",zi,zd);
    display_draw_string(4,y,buf,COLOR_CYAN,COLOR_BLACK,1); y+=ROW_H;
    display_draw_hline(4,y,SCR_W-8,COLOR_LIGHT_GREY); y+=5;

    // Tryb
    snprintf(buf,sizeof(buf),"TRYB: %s",x_mode_name[x_ui.mode]);
    display_draw_string(4,y,buf,COLOR_YELLOW,COLOR_BLACK,1); y+=ROW_H;
    display_draw_hline(4,y,SCR_W-8,COLOR_LIGHT_GREY); y+=5;

    if (x_ui.mode==AXIS_X_MODE_JOG) {
        snprintf(buf,sizeof(buf),"Krok: %s kr",X_JOG_LBL[x_ui.jog_step_idx]);
        display_draw_string(4,y,buf,COL_LABEL,COLOR_BLACK,1); y+=ROW_H;
        display_draw_string(4,y,"ENC=jog SW=krok BTN2=tryb",COLOR_LIGHT_GREY,COLOR_BLACK,1);
    } else if (x_ui.mode==AXIS_X_MODE_AUTO) {
        snprintf(buf,sizeof(buf),"%+.2f",x_ui.auto_target_mm);
        draw_row(y,"Cel X:",buf,COLOR_YELLOW,x_ui.param_sel==0); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%.0f mm/m",x_ui.auto_speed);
        draw_row(y,"Pred:",buf,COL_VAL,x_ui.param_sel==1); y+=ROW_H;
        display_draw_string(4,y,"BTN3=GO SW=param",COLOR_LIGHT_GREY,COLOR_BLACK,1);
    } else {
        snprintf(buf,sizeof(buf),"%.2fmm",x_ui.cycle_depth_mm);
        draw_row(y,"Glab/przej:",buf,COLOR_YELLOW,x_ui.param_sel==0); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%.0f->%.0f",x_ui.cycle_z_start,x_ui.cycle_z_end);
        draw_row(y,"Z:",buf,COL_VAL,x_ui.param_sel==1); y+=ROW_H;
        snprintf(buf,sizeof(buf),"%d przejsc",x_ui.cycle_passes);
        draw_row(y,buf,"",COL_LABEL,x_ui.param_sel==2); y+=ROW_H;
        display_draw_string(4,y,"BTN3>>>=START",COLOR_LIGHT_GREY,COLOR_BLACK,1);
    }
    draw_footer();
    display_flush();
}

static void handle_axis_x(encoder_event_t evt)
{
    if (evt==ENCODER_EVT_BTN1_LONG) {
        cycle_abort = true; 
        if (g_axis_x) axis_stop(g_axis_x);
        stepper_stop(); spindle_emergency_stop();
        ui_menu_notify("!!! E-STOP !!!", COLOR_RED, 3000); return;
    }
    switch (evt) {
        case ENCODER_EVT_CW:
            if (x_ui.mode==AXIS_X_MODE_JOG) { if(g_axis_x) axis_jog(g_axis_x,AXIS_DIR_POS,X_JOG_STEPS[x_ui.jog_step_idx],40); }
            else if (x_ui.mode==AXIS_X_MODE_AUTO) { if(x_ui.param_sel==0) x_ui.auto_target_mm+=0.05f; else x_ui.auto_speed+=5.0f; }
            else { if(x_ui.param_sel==0) x_ui.cycle_depth_mm+=0.05f; else if(x_ui.param_sel==1) x_ui.cycle_z_end-=1.0f; else if(x_ui.cycle_passes<20) x_ui.cycle_passes++; }
            break;
        case ENCODER_EVT_CCW:
            if (x_ui.mode==AXIS_X_MODE_JOG) { if(g_axis_x) axis_jog(g_axis_x,AXIS_DIR_NEG,X_JOG_STEPS[x_ui.jog_step_idx],40); }
            else if (x_ui.mode==AXIS_X_MODE_AUTO) { if(x_ui.param_sel==0) x_ui.auto_target_mm-=0.05f; else if(x_ui.auto_speed>5.0f) x_ui.auto_speed-=5.0f; }
            else { if(x_ui.param_sel==0&&x_ui.cycle_depth_mm>0.05f) x_ui.cycle_depth_mm-=0.05f; else if(x_ui.param_sel==1) x_ui.cycle_z_end+=1.0f; else if(x_ui.cycle_passes>1) x_ui.cycle_passes--; }
            break;
        case ENCODER_EVT_SW_PRESS:
            if (x_ui.mode==AXIS_X_MODE_JOG) { x_ui.jog_step_idx=(x_ui.jog_step_idx+1)%X_JOG_COUNT; ui_menu_notify("Krok zmieniony",COL_OK,600); }
            else x_ui.param_sel=(x_ui.param_sel+1)%3;
            break;
        case ENCODER_EVT_SW_LONG:
            if (g_axis_x) { axis_reset_position(g_axis_x); ui_menu_notify("X = 0",COL_OK,800); } break;
        case ENCODER_EVT_BTN3_PRESS:
            if (x_ui.mode==AXIS_X_MODE_AUTO&&g_axis_x&&axis_get_state(g_axis_x)==AXIS_STATE_IDLE)
                { axis_move_to_mm(g_axis_x,x_ui.auto_target_mm,x_ui.auto_speed); ui_menu_notify("AUTO X...",COL_OK,500); }
            break;
        case ENCODER_EVT_BTN3_LONG:
            if (x_ui.mode==AXIS_X_MODE_CYCLE&&g_axis_x&&
                axis_get_state(g_axis_x)==AXIS_STATE_IDLE&&stepper_get_state()==STEPPER_STATE_IDLE)
                { axis_get_position_mm(g_axis_z); xTaskCreate(cycle_zx_task,"cycle_zx",3072,NULL,5,NULL); ui_menu_notify("Cykl ZX START",COL_OK,800); }
            break;
        case ENCODER_EVT_BTN1_PRESS:
            cycle_abort = true;
            if (g_axis_x)
            axis_stop(g_axis_x);
            stepper_stop();
            ui_menu_notify("STOP",COL_WARN,800);
            break;
        case ENCODER_EVT_BTN2_PRESS:
            x_ui.mode=(axis_x_mode_t)((x_ui.mode+1)%3); x_ui.param_sel=0; break;
        case ENCODER_EVT_BTN2_LONG:
            if (g_axis_x)
            axis_stop(g_axis_x);
            ui_menu_goto(SCREEN_MENU);
            break;
        default: break;
    }
}
