#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#ifndef ORPHEUS_TEST_TASK_FUNCTION
#define ORPHEUS_TEST_TASK_FUNCTION orpheus_generated_process_task_default
#endif
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=7);
    const char* mode=argv[1]; unsigned quantum=(unsigned)atoi(argv[2]); int first=3;
    CHECK(orpheus_generated_init(48000,quantum)==ORPHEUS_OK);
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define PROCESS() (!strcmp(mode,"task") ? ORPHEUS_TEST_TASK_FUNCTION(0) : orpheus_generated_process(quantum))
#else
    CHECK(argc>=9);
    const char* mode=argv[3]; unsigned quantum=(unsigned)atoi(argv[4]); int first=5;
    orpheus::Runtime runtime;
    const orpheus::Plan plan=orpheus::Plan::load_from_file(argv[1]);
    CHECK(plan.tasks.size()<=1);
    const std::string& task_id=plan.tasks.empty() ? plan.task_id : plan.tasks.front().id;
    CHECK(runtime.load_plan(plan,argv[2])==ORPHEUS_OK);
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define PROCESS() (!strcmp(mode,"task") ? runtime.process_task(task_id,0) : runtime.process_block(quantum))
#endif
    CHECK((argc-first)%4==0);
    unsigned blocks=3;
    const char* configured_blocks=getenv("ORPHEUS_TEST_BLOCKS");
    if (configured_blocks) {
        char* end=NULL;unsigned long parsed=strtoul(configured_blocks,&end,10);
        CHECK(end!=configured_blocks && *end=='\0' && parsed>=1 && parsed<=100000);
        blocks=(unsigned)parsed;
    }
    unsigned calls_per_block=1;
    const char* configured_calls=getenv("ORPHEUS_TEST_CALLS_PER_BLOCK");
    if (configured_calls) {
        char* end=NULL;unsigned long parsed=strtoul(configured_calls,&end,10);
        CHECK(end!=configured_calls && *end=='\0' && parsed>=1 && parsed<=1024);
        calls_per_block=(unsigned)parsed;
    }
    FILE* file=fopen("halfcomplex.bin","wb"); CHECK(file);
    float values[8192];
    for (unsigned block=0;block<blocks;++block) {
        CHECK(PROCESS()==ORPHEUS_OK);
        for (int i=first;i<argc;i+=4) {
            OrpheusValue value={ORPHEUS_VALUE_INT,{0}};
            CHECK(GET(argv[i],"calls",&value)==ORPHEUS_OK
                  && value.value.i32==(int32_t)((block+1)*calls_per_block));
            CHECK(GET(argv[i],"quantum",&value)==ORPHEUS_OK && value.value.i32==atoi(argv[i+3]));
            unsigned packed=(unsigned)atoi(argv[i+1]),decoded=(unsigned)atoi(argv[i+2]);
            CHECK(packed<=8192 && decoded<=8192);
            CHECK(READ(argv[i],"packed_values",values,packed)==ORPHEUS_OK);
            CHECK(fwrite(values,4,packed,file)==packed);
            CHECK(READ(argv[i],"decoded_values",values,decoded)==ORPHEUS_OK);
            CHECK(fwrite(values,4,decoded,file)==decoded);
        }
    }
    CHECK(fclose(file)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
