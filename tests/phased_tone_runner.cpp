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
static OrpheusValue real_value(float x) { OrpheusValue v={ORPHEUS_VALUE_FLOAT,{0}}; v.value.f32=x; return v; }
static OrpheusValue integer_value(int32_t x) { OrpheusValue v={ORPHEUS_VALUE_INT,{0}}; v.value.i32=x; return v; }
static OrpheusValue boolean_value(int x) { OrpheusValue v={ORPHEUS_VALUE_BOOL,{0}}; v.value.b=x!=0; return v; }
int main(int argc,char** argv) {
#ifdef ORPHEUS_TEST_GENERATED
    CHECK(argc>=8 && orpheus_generated_init(48000,32)==ORPHEUS_OK);
    int first=2; const char* mode=argv[1];
#define SET(n,k,v) orpheus_control_set_value(n,k,v)
#define ID(id,v) orpheus_control_set_value_id(id,v)
#define GET(n,k,v) orpheus_control_get_value(n,k,v)
#define READ(n,k,v,c) orpheus_control_get_bulk(n,k,v,c)
#define WRITE(n,k,v,c) orpheus_control_write_bulk(n,k,v,c)
#define WRITE_ID(id,v,c) orpheus_control_write_bulk_id(id,v,c)
#define PROCESS() (strcmp(mode,"task")==0 ? orpheus_generated_process_task_default(0) : orpheus_generated_process(32))
#else
    CHECK(argc>=10);
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
    CHECK((argc-first)%6==0);
    FILE* stream=fopen("tone_states.bin","wb"); CHECK(stream);
    for (unsigned step=0;step<11;++step) {
        for (int i=first;i<argc;i+=6) {
            const char* node=argv[i];
            uint32_t phase=(uint32_t)strtoul(argv[i+1],NULL,10);
            uint32_t count=(uint32_t)strtoul(argv[i+2],NULL,10);
            uint32_t current=(uint32_t)strtoul(argv[i+3],NULL,10);
            if (step==1 || step==9) {
                CHECK(ID(phase,integer_value(1))==ORPHEUS_OK);
                CHECK(ID(current,real_value(step==1 ? .9f : 1.4f))==ORPHEUS_OK);
                CHECK(ID(count,integer_value(step==1 ? 1 : INT32_MIN))==ORPHEUS_OK);
                CHECK(SET(node,"ramp0_target_gain",real_value(step==1 ? 1.5f : .6f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp0_coefficient",real_value(step==1 ? 1.01f : .99f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_current_gain",real_value(1.25f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_target_gain",real_value(.7f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_coefficient",real_value(.995f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_remaining_blocks",integer_value(step==1 ? 2 : 0))==ORPHEUS_OK);
                CHECK(ID(phase,integer_value(3))!=ORPHEUS_OK);
                CHECK(ID(current,real_value(-1))!=ORPHEUS_OK);
                CHECK(ID(count,real_value(1))!=ORPHEUS_OK);
            }
            if (step==2 || step==6) CHECK(SET(node,"freeze",boolean_value(1))==ORPHEUS_OK);
            if (step==3 || step==7) CHECK(SET(node,"freeze",boolean_value(0))==ORPHEUS_OK);
            if (step==6) {
                CHECK(ID(phase,integer_value(2))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_current_gain",real_value(.8f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_target_gain",real_value(1.2f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_coefficient",real_value(1.01f))==ORPHEUS_OK);
                CHECK(SET(node,"ramp1_remaining_blocks",integer_value(1))==ORPHEUS_OK);
            }
            if (step==10) CHECK(ID(count,integer_value(0))==ORPHEUS_OK);
            if (step==5) {
                float boost[6]={.6f,0,-.2f,.15f,.025f}, cut[6]={.2f,0,0,-.1f,.05f};
                float before[5], actual[5];
                uint32_t boost_id=(uint32_t)strtoul(argv[i+4],NULL,10);
                uint32_t cut_id=(uint32_t)strtoul(argv[i+5],NULL,10);
                CHECK(READ(node,"boost_coefs",before,5)==ORPHEUS_OK);
                CHECK(WRITE(node,"boost_coefs",boost,5)==ORPHEUS_OK);
                CHECK(WRITE_ID(boost_id,boost,6)!=ORPHEUS_OK);
                CHECK(READ(node,"boost_coefs",actual,5)==ORPHEUS_OK && !memcmp(before,actual,sizeof(before)));
                CHECK(READ(node,"cut_coefs",before,5)==ORPHEUS_OK);
                CHECK(WRITE_ID(cut_id,cut,5)==ORPHEUS_OK);
                CHECK(WRITE(node,"cut_coefs",cut,6)!=ORPHEUS_OK);
                CHECK(READ(node,"cut_coefs",actual,5)==ORPHEUS_OK && !memcmp(before,actual,sizeof(before)));
            }
        }
        CHECK(PROCESS()==ORPHEUS_OK);
        for (int i=first;i<argc;i+=6) {
            const char* keys[]={"phase","ramp0_remaining_blocks","ramp1_remaining_blocks","ramp0_current_gain","ramp1_current_gain"};
            for (unsigned k=0;k<5;++k) {
                OrpheusValue value={ORPHEUS_VALUE_INT,{0}};
                CHECK(GET(argv[i],keys[k],&value)==ORPHEUS_OK);
                if (k<3) CHECK(value.type==ORPHEUS_VALUE_INT && fwrite(&value.value.i32,4,1,stream)==1);
                else CHECK(value.type==ORPHEUS_VALUE_FLOAT && fwrite(&value.value.f32,4,1,stream)==1);
            }
        }
    }
    CHECK(fclose(stream)==0);
#ifdef ORPHEUS_TEST_GENERATED
    orpheus_generated_teardown();
#endif
    return 0;
}
