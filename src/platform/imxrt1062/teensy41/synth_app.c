#if SK_SYNTH
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "solar_os_synth_app.h"
#include "solar_os_synth_voice.h"
#include "solar_os_shell_io.h"
#include "solar_os_keys.h"
#include "audio_output.h"

#define OWNER "app:synth"
typedef struct {
    solar_os_shell_io_t fallback;
    solar_os_synth_voice_config_t config;
    unsigned octave, volume, parameter;
    bool hold;
    uint32_t notes[13], deadlines[13];
} app_state_t;
static void *state;
#define app (*(app_state_t *)state)
static const char piano[] = "awsedftgyhujk";
static const unsigned frequencies[] = {262,277,294,311,330,349,370,392,415,440,466,494,523};
static const char *parameters[] = {"attack", "decay", "sustain", "release", "cutoff", "resonance", "osc2 mix"};
static solar_os_shell_io_t *io(solar_os_context_t *ctx) {
    solar_os_shell_io_t *out = solar_os_context_shell_io(ctx);
    if (!out || solar_os_shell_io_kind(out) == SOLAR_OS_SHELL_IO_KIND_NONE) {
        solar_os_shell_io_init_terminal(&app.fallback, solar_os_context_terminal(ctx));
        solar_os_context_set_shell_io(ctx, &app.fallback); out = &app.fallback;
    }
    return out;
}
static void report(solar_os_context_t *ctx, esp_err_t err) {
    if (err != ESP_OK) solar_os_shell_io_printf(io(ctx), "synth: %s\n", esp_err_to_name(err));
}
static void silence(void) {
    solar_os_synth_voice_all_notes_off(OWNER);
    memset(app.notes, 0, sizeof(app.notes));
    memset(app.deadlines, 0, sizeof(app.deadlines));
}
static void show(solar_os_context_t *ctx) {
    solar_os_synth_voice_status_t voice;
    solar_os_synth_status_t service;
    solar_os_synth_voice_get_status(&voice);
    solar_os_synth_get_status(&service);
    solar_os_shell_io_printf(io(ctx),
        "synth: wave=%s octave=%u volume=%u hold=%s parameter=%s\n",
        solar_os_synth_waveform_name(app.config.waveform), app.octave, app.volume,
        app.hold ? "on" : "off", parameters[app.parameter]);
    solar_os_shell_io_printf(io(ctx),
        "ADSR=%lu/%lu/%u/%lu cutoff=%lu resonance=%u osc2=%u\n",
        (unsigned long)app.config.attack_ms, (unsigned long)app.config.decay_ms,
        app.config.sustain_percent, (unsigned long)app.config.release_ms,
        (unsigned long)app.config.filter.cutoff_hz, app.config.filter.resonance_percent,
        app.config.oscillator2.mix_percent);
    solar_os_shell_io_printf(io(ctx),
        "voices=%u running=%u rate=%lu blocks=%lu peak=%lu hash=%08lx\n",
        (unsigned)voice.active_voices, service.running,
        (unsigned long)service.sample_rate, (unsigned long)service.rendered_blocks,
        (unsigned long)voice.pcm_peak, (unsigned long)voice.pcm_hash);
    solar_os_shell_io_printf(io(ctx),
        "max_us=%lu misses=%lu underruns=%lu errors=%lu\n",
        (unsigned long)service.max_render_us, (unsigned long)service.render_deadline_misses,
        (unsigned long)sk_audio_output_underruns(), (unsigned long)service.write_errors);
}
static esp_err_t start(solar_os_context_t *ctx) {
    if (solar_os_context_argc(ctx) > 1 &&
        strcmp(solar_os_context_argv(ctx, 1), "--headless")) return ESP_ERR_INVALID_ARG;
    app.octave = 4; app.volume = 20;
    solar_os_synth_voice_status_t initial;
    solar_os_synth_voice_get_status(&initial);
    app.config = initial.config;
    app.config.waveform = SOLAR_OS_SYNTH_WAVE_SINE;
    solar_os_shell_io_writeln(io(ctx),
        "Synth: a w s e d f t g y h u j k = C through C\n"
        "Notes pulse for 220 ms; H toggles hold (press a note again to release).\n"
        "1 square 2 triangle 3 saw 4 sine 5 noise | z/x octave | -/+ volume\n"
        "[ / ] select parameter, , / . adjust | Space silence | ? status | q/Ctrl-] exit");
    report(ctx, solar_os_synth_voice_configure(OWNER, &app.config));
    show(ctx);
    return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
    silence();
    esp_err_t err = solar_os_synth_voice_stop(OWNER);
    // Also reap a worker whose startup failed before the voice was claimed.
    if (err == ESP_OK) err = solar_os_synth_stop(OWNER);
    report(ctx, err);
}
static unsigned adjust(unsigned value, int delta, unsigned lo, unsigned hi) {
    int next = (int)value + delta;
    return next < (int)lo ? lo : next > (int)hi ? hi : (unsigned)next;
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *ev) {
    if (ev->type == SOLAR_OS_EVENT_TICK) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        for (unsigned i=0;i<13;++i) if (app.notes[i] && !app.hold &&
            (int32_t)(now-app.deadlines[i]) >= 0) {
            solar_os_synth_voice_note_off(OWNER, app.notes[i]); app.notes[i]=0;
        }
        return true;
    }
    if (ev->type != SOLAR_OS_EVENT_CHAR) return false;
    unsigned char ch = ev->data.ch;
    if (ch == 'q' || ch == 3 || ch == SOLAR_OS_KEY_APP_EXIT) {
        solar_os_context_finish(ctx, 0, NULL); return true;
    }
    const char *key = ch ? strchr(piano, ch) : NULL;
    if (key) {
        unsigned i = key-piano;
        if (app.notes[i] && app.hold) {
            report(ctx, solar_os_synth_voice_note_off(OWNER, app.notes[i])); app.notes[i]=0;
        } else {
            unsigned hz = frequencies[i];
            if (app.octave < 4) hz >>= 4-app.octave; else hz <<= app.octave-4;
            esp_err_t err = solar_os_synth_voice_note_on(OWNER, hz, 100);
            if (err == ESP_OK) {
                app.notes[i]=hz;
                app.deadlines[i]=(uint32_t)(esp_timer_get_time()/1000)+220;
                report(ctx, sk_audio_output_volume(app.volume));
            }
            report(ctx, err);
        }
        return true;
    }
    if (ch == ' ') silence();
    else if (ch == 'H') { silence(); app.hold = !app.hold; }
    else if (ch == 'z' || ch == 'x') { silence(); app.octave=adjust(app.octave,ch=='z'?-1:1,2,6); }
    else if (ch == '-' || ch == '+' || ch == '=') {
        app.volume=adjust(app.volume,ch=='-'?-5:5,0,100);
        report(ctx, sk_audio_output_volume(app.volume));
    } else if (ch >= '1' && ch <= '5') app.config.waveform=ch-'1';
    else if (ch == '[') app.parameter=(app.parameter+6)%7;
    else if (ch == ']') app.parameter=(app.parameter+1)%7;
    else if (ch == ',' || ch == '.') {
        int direction=ch==','?-1:1;
        switch (app.parameter) {
        case 0: app.config.attack_ms=adjust(app.config.attack_ms,direction*10,0,10000); break;
        case 1: app.config.decay_ms=adjust(app.config.decay_ms,direction*10,0,10000); break;
        case 2: app.config.sustain_percent=adjust(app.config.sustain_percent,direction*5,0,100); break;
        case 3: app.config.release_ms=adjust(app.config.release_ms,direction*10,0,10000); break;
        case 4: app.config.filter.cutoff_hz=adjust(app.config.filter.cutoff_hz,direction*500,40,18000); break;
        case 5: app.config.filter.resonance_percent=adjust(app.config.filter.resonance_percent,direction*5,0,100); break;
        case 6: app.config.oscillator2.mix_percent=adjust(app.config.oscillator2.mix_percent,direction*5,0,100); break;
        }
    } else if (ch != '?') return true;
    report(ctx, solar_os_synth_voice_configure(OWNER, &app.config));
    show(ctx);
    return true;
}
const solar_os_app_t solar_os_synth_app = {
    .name="synth", .summary="Playable terminal synthesizer", .app_class=SOLAR_OS_APP_CLASS_TUI,
    .start=start, .stop=stop, .event=event,
    .state_slot=&state, .state_size=sizeof(app_state_t),
    .state_storage=SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
    .worker_stack_bytes=8192, .tick_interval_ms=10,
};
#endif
