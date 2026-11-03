#include <stdio.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=3 && orpheus_generated_init(48000,32)==ORPHEUS_OK);
    int first=2; const char* mode=argv[1];
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc>=5);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])==ORPHEUS_OK);
    int first=4; const char* mode=argv[3];
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? runtime.process_task("default",0) : runtime.process_block(32))
#endif
    FILE* stream=fopen("block_stats.bin","wb"); CHECK(stream);
    for (unsigned block=0;block<6;++block) {
        CHECK(PROCESS()==ORPHEUS_OK);
        for (int i=first;i<argc;++i) {
            const char* keys[]={"rms_db","peak_db"};
            for (unsigned k=0;k<2;++k) {
                OrpheusValue value={ORPHEUS_VALUE_FLOAT,{0}};
                CHECK(GET(argv[i],keys[k],&value)==ORPHEUS_OK);
                CHECK(fwrite(&value.value.f32,4,1,stream)==1);
                char consumer[128]; snprintf(consumer,sizeof(consumer),"%s_%s",argv[i],k==0 ? "rms" : "peak");
                float metrics[32];
                CHECK(READ(consumer,"metrics",metrics,32)==ORPHEUS_OK);
                CHECK(metrics[0]==block+1 && metrics[1]==32 && metrics[6]==1);
                CHECK(!memcmp(&metrics[8+block],&value.value.f32,4));
            }
        }
    }
    CHECK(fclose(stream)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
