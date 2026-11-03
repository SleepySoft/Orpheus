#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ORPHEUS_TEST_GENERATED
#include "orpheus_graph.h"
#include "orpheus_control.h"
#else
#include "orpheus_runtime/runtime.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); return 1; } } while (0)
static OrpheusValue boolean_value(int x) { OrpheusValue v={ORPHEUS_VALUE_BOOL,{0}}; v.value.b=x!=0; return v; }
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=9 && orpheus_generated_init(48000,32)==ORPHEUS_OK);
    int first=2; const char* mode=argv[1];
#define SET(n,k,v) orpheus_control_set_value(n,k,v)
#define ID(id,v) orpheus_control_set_value_id(id,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) orpheus_control_write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) orpheus_control_write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc>=11);
    orpheus::Runtime runtime;
    CHECK(runtime.load_plan(orpheus::Plan::load_from_file(argv[1]),argv[2])==ORPHEUS_OK);
    int first=4; const char* mode=argv[3];
#define SET(n,k,v) runtime.set_parameter(n,k,v)
#define ID(id,v) runtime.write_id(id,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) runtime.write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) runtime.write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? runtime.process_task("default",0) : runtime.process_block(32))
#endif
    CHECK((argc-first)%7==0);
    const unsigned blocks[]={2,1,1,2,1,1};
    const char* keys[]={"current_gains","target_gains","coefficients","remaining_blocks"};
    FILE* stream=fopen("sample_gain_states.bin","wb"); CHECK(stream);
    for (unsigned step=0;step<6;++step) {
        for (int i=first;i<argc;i+=7) {
            const char* node=argv[i];
            uint32_t freeze_id=(uint32_t)strtoul(argv[i+1],NULL,10);
            if (step==2) CHECK(SET(node,"freeze",boolean_value(1))==ORPHEUS_OK);
            if (step==3) CHECK(ID(freeze_id,boolean_value(0))==ORPHEUS_OK);
            if (step==1 || step==5) {
                size_t count=(size_t)strtoul(argv[i+6],NULL,10);
                CHECK(count>0 && count<=32);
                for (unsigned k=0;k<4;++k) {
                    float values[33]; int32_t counts[33]; unsigned char before[128],actual[128];
                    for (size_t r=0;r<count;++r) {
                        values[r]=step==5 ? k==0 ? .9f : k==1 ? .4f : 1.0f :
                            k==0 ? .6f+(r%3)*.1f : k==1 ? .2f+(r%4)*.05f : .98f+(r%5)*.003f;
                        counts[r]=step==5 ? INT32_MAX-(int32_t)r : 3;
                    }
                    const void* data=k==3 ? (const void*)counts : (const void*)values;
                    CHECK(READ(node,keys[k],before,count)==ORPHEUS_OK);
                    uint32_t id=(uint32_t)strtoul(argv[i+k+2],NULL,10);
                    CHECK((step==1 ? WRITE(node,keys[k],data,count) : WRITE_ID(id,data,count))==ORPHEUS_OK);
                    CHECK(WRITE_ID(id,data,count+1)!=ORPHEUS_OK);
                    CHECK(READ(node,keys[k],actual,count)==ORPHEUS_OK && !memcmp(before,actual,count*4));
                }
            }
        }
        for (unsigned b=0;b<blocks[step];++b) {
            CHECK(PROCESS()==ORPHEUS_OK);
            for (int i=first;i<argc;i+=7) {
                size_t count=(size_t)strtoul(argv[i+6],NULL,10);
                CHECK(count>0 && count<=32);
                float gains[32]; int32_t counts[32];
                CHECK(READ(argv[i],"current_gains",gains,count)==ORPHEUS_OK);
                CHECK(READ(argv[i],"remaining_blocks",counts,count)==ORPHEUS_OK);
                CHECK(fwrite(gains,4,count,stream)==count && fwrite(counts,4,count,stream)==count);
            }
        }
    }
    CHECK(fclose(stream)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
