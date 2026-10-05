static void entry(void *) {}
int main() {
    TaskHandle_t a=nullptr,b=nullptr;
    for(int i=0;i<100;++i) {
        alloc_fail=true;
        assert(!solar_os_task_create_pinned_internal(entry,"a",28672,nullptr,1,&a,0,SOLAR_OS_TASK_ROLE_FOREGROUND));
        alloc_fail=false;create_fail=true;
        assert(!solar_os_task_create_pinned_internal(entry,"a",28672,nullptr,1,&a,0,SOLAR_OS_TASK_ROLE_FOREGROUND));
        assert(allocations.empty());create_fail=false;
        assert(solar_os_task_create_pinned_internal(entry,"a",16384,nullptr,1,&a,0,SOLAR_OS_TASK_ROLE_FOREGROUND));
        assert(allocations.at(worker_stack)==16384);
        assert(!solar_os_task_create_pinned_internal(entry,"b",1024,nullptr,1,&b,0,SOLAR_OS_TASK_ROLE_FOREGROUND));
        assert(solar_os_task_create_pinned_internal(entry,"p",40960,nullptr,1,&b,0,SOLAR_OS_TASK_ROLE_BACKGROUND));
        volatile bool done=false;
        assert(!solar_os_task_wait_done(a,&done,10) && allocations.size()==2);
        done=true;
        assert(!solar_os_task_wait_done(a,&done,10) && allocations.size()==2);
        a->state=eSuspended;
        assert(solar_os_task_wait_done(a,&done,10) && allocations.size()==1);
        b->state=eSuspended;
        assert(solar_os_task_wait_done(b,&done,10) && allocations.empty());
        create_fail=true;
        assert(!solar_os_task_create_pinned_internal(entry,"p",40960,nullptr,1,&b,0,SOLAR_OS_TASK_ROLE_BACKGROUND));
        create_fail=false;assert(allocations.empty());
    }
}
