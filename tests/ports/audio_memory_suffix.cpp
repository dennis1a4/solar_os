static void update_audio() { assert(interrupts); source.update(); capture.update(); }
static void check_empty() {
    assert(!pcm && !captured && !live && allocations.empty());
    assert(!head && !tail && !partial && !capture_enabled);
}
int main() {
    int16_t stereo[256], mono[128];
    for(unsigned i=0;i<256;++i)stereo[i]=i;
    for(unsigned i=0;i<128;++i)incoming.data[i]=int16_t(i+123);
    size_t frames;
    volatile bool stop=false;
    assert(sk_audio_output_start(20)==ESP_ERR_NOT_FOUND);
    assert(sk_audio_capture_start()==ESP_ERR_NOT_FOUND);
    sk_audio_player_begin();assert(ready);check_empty();
    // Run an interrupt immediately at every publish/detach boundary.
    on_enable=update_audio;
    for(unsigned cycle=0;cycle<100;++cycle) {
        assert(sk_audio_output_write(stereo,1)==ESP_ERR_INVALID_STATE);
        assert(sk_audio_synth_write(stereo,1,&stop)==ESP_ERR_INVALID_STATE);
        assert(sk_audio_capture_read(mono,128,&frames)==ESP_ERR_INVALID_STATE);
        fail_alloc=true;
        assert(sk_audio_output_start(20)==ESP_ERR_NO_MEM);
        assert(sk_audio_capture_start()==ESP_ERR_NO_MEM);check_empty();
        fail_alloc=false;i2c_ok=false;
        assert(sk_audio_output_start(20)==ESP_ERR_TIMEOUT);
        assert(sk_audio_capture_start()==ESP_ERR_TIMEOUT);check_empty();
        i2c_ok=true;codec_ok=false;
        assert(sk_audio_output_start(20)==ESP_FAIL);
        assert(sk_audio_capture_start()==ESP_FAIL);check_empty();codec_ok=true;
        assert(sk_audio_output_start(20)==ESP_OK && live==16384);
        app_busy=true;sk_audio_player_tone(true);sk_audio_player_tone(false);
        assert(pcm && live==16384 && !tone_on);app_busy=false;
        diagnostic_busy=true;assert(sk_audio_diagnostic_busy());
        sk_audio_player_tone(true);assert(pcm && !tone_on);
        diagnostic_busy=false;
        assert(sk_audio_output_write(stereo,128)==ESP_OK);update_audio();
        for(unsigned i=0;i<128;++i)assert(transmitted.data[i]==stereo[i*2+1]);
        assert(sk_audio_output_write(stereo,3)==ESP_OK);
        assert(sk_audio_output_finish(true)==ESP_OK);check_empty();
        assert(transmitted.data[0]==1 && transmitted.data[2]==5 && transmitted.data[3]==0);
        // Repeated start replaces its buffer; cancellation/timeout both detach.
        assert(sk_audio_output_start(20)==ESP_OK);
        assert(sk_audio_output_start(20)==ESP_OK && live==16384);
        sk_audio_output_write(stereo,128);cancelled=true;
        assert(sk_audio_output_finish(true)==ESP_ERR_TIMEOUT);cancelled=false;check_empty();
        sk_audio_output_start(20);sk_audio_output_write(stereo,128);run_updates=false;
        assert(sk_audio_output_finish(true)==ESP_FAIL);run_updates=true;check_empty();
        sk_audio_output_start(20);stop=true;
        assert(sk_audio_synth_write(stereo,128,&stop)==ESP_ERR_TIMEOUT);stop=false;
        sk_audio_output_finish(false);check_empty();
        sk_audio_output_start(20);
        assert(sk_audio_capture_start()==ESP_OK && !pcm && live==32768);
        assert(sk_audio_capture_start()==ESP_OK && live==32768);
        input_pending=true;update_audio();
        assert(sk_audio_capture_read(mono,128,&frames)==ESP_OK && frames==128);
        assert(!memcmp(mono,incoming.data,sizeof(mono)));
        // Pending capture interrupt at stop must release its input, not use freed ring.
        input_pending=true;assert(sk_audio_capture_stop()==0);check_empty();
        // A full capture queue reports overflow and still frees on stop.
        assert(sk_audio_capture_start()==ESP_OK);
        for(unsigned i=0;i<capture_slots;++i){input_pending=true;update_audio();}
        assert(sk_audio_capture_read(mono,128,&frames)==ESP_FAIL);
        assert(sk_audio_capture_stop()==1);check_empty();
        // Failed restart releases the previous ring rather than retaining it.
        sk_audio_output_start(20);fail_alloc=true;
        assert(sk_audio_output_start(20)==ESP_ERR_NO_MEM);check_empty();fail_alloc=false;
        sk_audio_capture_start();fail_alloc=true;
        assert(sk_audio_capture_start()==ESP_ERR_NO_MEM);check_empty();fail_alloc=false;
        sk_audio_capture_stop();sk_audio_output_finish(false);check_empty();
        // Standalone tones and clock alarms never reserve either ring.
        sk_audio_player_tone(true);assert(tone_on && !live);ticks+=1001;update_audio();
        assert(!tone_on);check_empty();sk_audio_player_tone(false);check_empty();
        sk_clock_alarm_sound(true);assert(clock_tone_on && !live);
        ticks+=1001;update_audio();assert(!clock_tone_on);check_empty();
    }
}
