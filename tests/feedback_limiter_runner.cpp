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
static OrpheusValue integer_value(int32_t x) { OrpheusValue v={ORPHEUS_VALUE_INT,{0}}; v.value.i32=x; return v; }
static OrpheusValue boolean_value(int x) { OrpheusValue v={ORPHEUS_VALUE_BOOL,{0}}; v.value.b=x!=0; return v; }
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=9 && orpheus_generated_init(48000,32)==ORPHEUS_OK);
    int first=2; const char* mode=argv[1];
#define SET(n,k,v) orpheus_control_set_value(n,k,v)
#define ID(id,v) orpheus_control_set_value_id(id,v)
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
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
#define GET(n,k,v) runtime.get_parameter(n,k,v)
#define READ(n,k,v,c) runtime.get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) runtime.write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) runtime.write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? runtime.process_task("default",0) : runtime.process_block(32))
#endif
    CHECK((argc-first)%7==0);
    const unsigned blocks[]={2,1,1,2,1,1};
    FILE* stream=fopen("limiter_states.bin","wb"); CHECK(stream);
    for (unsigned step=0;step<6;++step) {
        for (int i=first;i<argc;i+=7) {
            const char* node=argv[i];
            uint32_t set_id=(uint32_t)strtoul(argv[i+1],NULL,10), disable_id=(uint32_t)strtoul(argv[i+2],NULL,10);
            if (step==1 || step==4) CHECK(ID(set_id,integer_value(2))==ORPHEUS_OK);
            if (step==2) CHECK(SET(node,"disabled",boolean_value(1))==ORPHEUS_OK);
            if (step==3 || step==5) {
                CHECK(SET(node,"parameter_set",integer_value(1))==ORPHEUS_OK);
                CHECK(ID(disable_id,boolean_value(step==5))==ORPHEUS_OK);
            }
            CHECK(ID(set_id,integer_value(3))!=ORPHEUS_OK);
            CHECK(SET(node,"active_mask",integer_value(0))!=ORPHEUS_OK);
            if (step==3) {
                OrpheusValue channels={ORPHEUS_VALUE_INT,{0}};
                CHECK(GET(node,"channels",&channels)==ORPHEUS_OK);
                size_t count=(size_t)channels.value.i32*2;
                CHECK(count<=64);
                const char* keys[]={"feedback_attack","feedback_release","feedback_k1","feedback_min_factor"};
                for (unsigned k=0;k<4;++k) {
                    float values[65], before[64], actual[64];
                    for (size_t j=0;j<count;++j) values[j]=k==0 ? .01f+(j%3)*.005f :
                        k==1 ? 1.003f+(j%3)*.001f : k==2 ? .8f+(j%5)*.1f : .2f+(j%3)*.05f;
                    CHECK(READ(node,keys[k],before,count)==ORPHEUS_OK);
                    uint32_t id=(uint32_t)strtoul(argv[i+k+3],NULL,10);
                    CHECK((k%2 ? WRITE_ID(id,values,count) : WRITE(node,keys[k],values,count))==ORPHEUS_OK);
                    CHECK(WRITE_ID(id,values,count+1)!=ORPHEUS_OK);
                    CHECK(READ(node,keys[k],actual,count)==ORPHEUS_OK && !memcmp(before,actual,count*sizeof(float)));
                }
            }
        }
        for (unsigned b=0;b<blocks[step];++b) {
            CHECK(PROCESS()==ORPHEUS_OK);
            for (int i=first;i<argc;i+=7) {
                OrpheusValue mask={ORPHEUS_VALUE_INT,{0}}, gain={ORPHEUS_VALUE_FLOAT,{0}};
                CHECK(GET(argv[i],"active_mask",&mask)==ORPHEUS_OK && mask.type==ORPHEUS_VALUE_INT);
                CHECK(GET(argv[i],"gain",&gain)==ORPHEUS_OK && gain.type==ORPHEUS_VALUE_FLOAT);
                CHECK(fwrite(&mask.value.i32,4,1,stream)==1 && fwrite(&gain.value.f32,4,1,stream)==1);
            }
        }
    }
    CHECK(fclose(stream)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
