#if SK_SSH
#include <arduino_freertos.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
#include "solar_os_ssh_keys.h"
#include "solar_os_task.h"
}
extern "C" bool sk_console_poll_cancel(bool);
static bool key_busy;
struct KeyGeneration { unsigned bits; bool overwrite; volatile bool done; esp_err_t result; };
static void generate(void *arg) {
    auto *request=static_cast<KeyGeneration *>(arg);
    request->result=solar_os_ssh_keys_generate_rsa(request->bits,request->overwrite);
    request->done=true;
    solar_os_task_delete_internal(nullptr);
}
extern "C" void solar_os_shell_cmd_sshkey(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    io->command_status=1;
    if(key_busy){solar_os_shell_io_writeln(io,"sshkey: key generation is in progress");return;}
    esp_err_t err=ESP_ERR_INVALID_ARG;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {
        solar_os_ssh_key_status_t status{};
        err=solar_os_ssh_keys_get_status(&status);
        if(err==ESP_OK) {
            solar_os_shell_io_printf(io,"private: %s (%s, %lu bytes)\npublic: %s (%s, %lu bytes)\n",
                status.private_key_path,status.private_key_exists?"present":"missing",(unsigned long)status.private_key_size,
                status.public_key_path,status.public_key_exists?"present":"missing",(unsigned long)status.public_key_size);
        }
    } else if(argc==2 && !strcmp(argv[1],"pub")) {
        char public_key[SOLAR_OS_SSH_PUBLIC_KEY_MAX]; size_t written=0;
        err=solar_os_ssh_keys_read_public(public_key,sizeof(public_key),&written);
        if(err==ESP_OK) solar_os_shell_io_writeln(io,public_key);
    } else if(argc==2 && !strcmp(argv[1],"rm")) {
        err=solar_os_ssh_keys_remove_default();
        if(err==ESP_OK) solar_os_shell_io_writeln(io,"sshkey: removed");
    } else if(argc>=2 && argc<=4 && !strcmp(argv[1],"gen")) {
        KeyGeneration request{2048,false,false,ESP_FAIL}; bool have_bits=false;
        for(int i=2;i<argc;++i) {
            if(!strcmp(argv[i],"-f") && !request.overwrite) request.overwrite=true;
            else if(!have_bits && (!strcmp(argv[i],"2048") || !strcmp(argv[i],"3072") || !strcmp(argv[i],"4096"))) {
                request.bits=atoi(argv[i]); have_bits=true;
            } else {solar_os_shell_io_writeln(io,"usage: sshkey gen [-f] [2048|3072|4096]");return;}
        }
        key_busy=true;
        TaskHandle_t worker=nullptr;
        if(solar_os_task_create_pinned_internal(generate,"sshkey",16384,&request,1,&worker,0,SOLAR_OS_TASK_ROLE_FOREGROUND)!=pdPASS) {
            key_busy=false;
            solar_os_shell_io_writeln(io,"sshkey: foreground worker busy or insufficient memory");return;
        }
        solar_os_shell_io_writeln(io,"sshkey: generating key; waiting for RSA operation to finish");
        while(!request.done) { (void)sk_console_poll_cancel(false); vTaskDelay(1); }
        while(!solar_os_task_wait_done(worker,&request.done,1000)) vTaskDelay(1);
        key_busy=false;
        err=request.result;
        if(err==ESP_OK) solar_os_shell_io_writeln(io,"sshkey: generated default RSA key");
    } else {
        solar_os_shell_io_writeln(io,"usage: sshkey [status|pub|rm] | sshkey gen [-f] [2048|3072|4096]");return;
    }
    io->command_status=err==ESP_OK?0:1;
    if(err!=ESP_OK) solar_os_shell_io_printf(io,"sshkey: %s\n",esp_err_to_name(err));
}
#endif
